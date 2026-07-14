#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "esp_timer.h"
#include "wifi.h"
#include "mqtt.h"
#include "esp_now_outdoor.h"
#include "audio_ai.h"
#include "event_fusion.h"
#include "light.h"
#include "command_recognition.h"
#include "oled.h"
#include "font_chinese.h"
#include "buzzer.h"
#include "door_button.h"

static const char *TAG = "MAIN";

static uint8_t indoor_mac[6] = INDOOR_MAC_ADDR;

static uint32_t s_espnow_heartbeat_count = 0;
static uint32_t s_doorbell_count = 0;
static uint32_t s_emergency_count = 0;

#define BOOT_BUTTON_GPIO_NUM         0

static led_strip_handle_t s_led_strip = NULL;

static uint32_t s_last_consumed_audio_seq = 0;

static volatile bool s_initialized = false;

/* ===== 状态机 =====
 * IDLE:      正常运行三通道融合判决，门铃按钮按下立即响蜂鸣器
 * LISTENING: 融合判决通过，启动5秒语音关键词识别窗口
 * PROCESSING:正在处理事件（灯带闪烁、ESP-NOW、MQTT、OLED）
 * CLEANUP:   等待command_recognition停止，恢复audio_ai
 */
typedef enum {
    STATE_IDLE,
    STATE_LISTENING,
    STATE_PROCESSING,
    STATE_CLEANUP,
} sys_state_t;

static volatile sys_state_t s_sys_state = STATE_IDLE;
static uint32_t s_listen_start_ms = 0;
static volatile bool s_cmd_recognized = false;
static command_id_t s_recognized_cmd = CMD_NONE;

#define LISTEN_WINDOW_MS  5000

/* ===== 事件队列 =====
 * 当正在处理事件时，新的访客事件进入队列等待
 */
typedef struct {
    command_id_t cmd_id;
    uint32_t timestamp;
} event_item_t;

#define EVENT_QUEUE_SIZE  5
static event_item_t s_event_queue[EVENT_QUEUE_SIZE];
static int s_queue_head = 0;
static int s_queue_tail = 0;

static int queue_size(void)
{
    int size = s_queue_head - s_queue_tail;
    if (size < 0) size += EVENT_QUEUE_SIZE;
    return size;
}

static bool queue_push(command_id_t cmd_id)
{
    if (queue_size() >= EVENT_QUEUE_SIZE - 1) {
        ESP_LOGW(TAG, "Event queue full, dropping new event");
        return false;
    }
    s_event_queue[s_queue_head].cmd_id = cmd_id;
    s_event_queue[s_queue_head].timestamp = (uint32_t)(esp_timer_get_time() / 1000);
    s_queue_head = (s_queue_head + 1) % EVENT_QUEUE_SIZE;
    ESP_LOGI(TAG, "Event enqueued: cmd_id=%d, queue size=%d", cmd_id, queue_size());
    return true;
}

static bool queue_pop(command_id_t *cmd_id)
{
    if (s_queue_head == s_queue_tail) {
        return false;
    }
    *cmd_id = s_event_queue[s_queue_tail].cmd_id;
    s_queue_tail = (s_queue_tail + 1) % EVENT_QUEUE_SIZE;
    ESP_LOGI(TAG, "Event dequeued: cmd_id=%d, queue size=%d", *cmd_id, queue_size());
    return true;
}

static void button_init(void)
{
    gpio_reset_pin(BOOT_BUTTON_GPIO_NUM);
    gpio_set_direction(BOOT_BUTTON_GPIO_NUM, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BOOT_BUTTON_GPIO_NUM, GPIO_PULLUP_ONLY);
}

static void oled_show_status(const char *line1, const char *line2, const char *line3)
{
    oled_clear_buffer();
    if (line1) oled_show_chinese_string(line1, 0, 0);
    if (line2) oled_show_chinese_string(line2, 0, 32);
    if (line3) oled_show_chinese_string(line3, 0, 64);
    oled_flush();
}

static uint32_t s_led_on_start_ms = 0;
static bool s_led_turned_off_in_cleanup = false;

#define LED_DISPLAY_DURATION_MS  3000
#define OLED_DISPLAY_DELAY_MS    1500

static void trigger_doorbell_event(command_id_t cmd)
{
    int cmd_id = (int)cmd;
    s_doorbell_count++;

    ESP_LOGI(TAG, "  [1/4] LED solid color by cmd_id=%d", cmd_id);
    light_solid_by_cmd(s_led_strip, cmd_id);
    s_led_on_start_ms = (uint32_t)(esp_timer_get_time() / 1000);
    s_led_turned_off_in_cleanup = false;

    ESP_LOGI(TAG, "  [2/4] ESP-NOW to indoor: cmd_id=%d", cmd_id);
    esp_now_data_outdoor_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.device_id = 0x02;
    msg.msg_type = MSG_TYPE_DOORBELL;
    msg.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
    msg.data_len = sprintf((char *)msg.data, "%d", cmd_id);
    esp_now_outdoor_send_struct(indoor_mac, &msg);

    ESP_LOGI(TAG, "  [3/4] MQTT publish: topic=%s", TOPIC_DOORBELL);
    mqtt_publish_command(cmd_id, command_get_name(cmd));

    if (cmd_id == 0) {
        oled_show_status("Visitor", "有访客来访！", "");
    } else if (cmd_id >= 1 && cmd_id <= 4) {
        oled_show_status("Visitor", command_get_name(cmd), "");
    } else {
        oled_show_status("Emergency", command_get_name(cmd), "");
    }
    ESP_LOGI(TAG, "  [4/4] OLED display: %s", command_get_name(cmd));

    ESP_LOGI(TAG, "===== Doorbell event #%lu done =====", (unsigned long)s_doorbell_count);
    ESP_LOGI(TAG, "  cmd_id: %d", cmd_id);
    ESP_LOGI(TAG, "  message: %s", command_get_name(cmd));
}

static void on_command_recognized(command_id_t cmd, float confidence)
{
    ESP_LOGI(TAG, "Cmd recognized: ID=%d, conf=%.2f, msg=%s", cmd, confidence, command_get_name(cmd));
    s_recognized_cmd = cmd;
    s_cmd_recognized = true;
}

static void on_visitor_alert(const fusion_result_t *fusion, const audio_result_t *audio)
{
    ESP_LOGI(TAG, "===== Fusion: VISITOR =====");
    ESP_LOGI(TAG, "Channels: button=%d, pir=%d, audio=%d",
             fusion->button_active, fusion->pir_active, fusion->audio_active);

    const char *audio_name = "none";
    if (audio->valid) {
        audio_name = (audio->event == AUDIO_EVENT_KNOCK) ? "knock" : "doorbell";
        ESP_LOGI(TAG, "Audio triggered: %s, confidence=%.2f", audio_name, audio->confidence);
    }
    oled_show_status("Visitor", "Visitor Detected", "Please speak...");

    /* 三判决成功后，统一进入5秒语音关键词识别窗口
     * command_recognition会重新启动麦克风录音，不复用audio_ai的数据 */
    ESP_LOGI(TAG, "[Visitor] Starting 5s voice recognition window");
    audio_ai_stop();
    s_cmd_recognized = false;
    s_recognized_cmd = CMD_NONE;
    s_sys_state = STATE_LISTENING;
    s_listen_start_ms = (uint32_t)(esp_timer_get_time() / 1000);
    command_recognition_start(on_command_recognized, LISTEN_WINDOW_MS);
}

static void on_emergency_alert(void)
{
    static uint32_t s_last_emergency_time = 0;
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);

    if (now - s_last_emergency_time < 1000) {
        return;
    }
    s_last_emergency_time = now;

    s_emergency_count++;

    light_solid_by_cmd(s_led_strip, 6);
    s_led_on_start_ms = (uint32_t)(esp_timer_get_time() / 1000);
    s_led_turned_off_in_cleanup = false;

    mqtt_publish_emergency(s_emergency_count);

    oled_show_status("Emergency", "有人寻求帮助！", "");

    ESP_LOGI(TAG, "===== Outdoor Emergency Alert =====");
    
    while ((uint32_t)(esp_timer_get_time() / 1000) - s_led_on_start_ms < LED_DISPLAY_DURATION_MS) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    light_all_off(s_led_strip);
    oled_show_status("Smart Doorbell", "System Ready", "Waiting...");
}

static void on_espnow_data_received(const uint8_t *mac_addr, const uint8_t *data, int len)
{
    if (len < 8) {
        return;
    }

    uint8_t msg_type = data[1];
    uint16_t data_len = ((uint16_t)data[6] << 8) | (uint16_t)data[7];

    char payload[256] = {0};
    if (data_len > 0 && len >= 8 + data_len && data_len < sizeof(payload) - 1) {
        memcpy(payload, &data[8], data_len);
        payload[data_len] = '\0';
    }

    switch (msg_type) {
    case MSG_TYPE_DOORBELL: {
        int cmd_id = 0;
        sscanf(payload, "%d", &cmd_id);

        if (cmd_id >= 0 && cmd_id <= 6) {
            if (s_sys_state == STATE_IDLE) {
                trigger_doorbell_event((command_id_t)cmd_id);
            } else {
                queue_push((command_id_t)cmd_id);
            }
        }
        break;
    }

    case MSG_TYPE_SENSOR:
        mqtt_publish_sensor(payload);
        break;

    case MSG_TYPE_EMERGENCY: {
        static uint32_t s_last_indoor_emergency_time = 0;
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        
        if (now - s_last_indoor_emergency_time < 1000) {
            break;
        }
        s_last_indoor_emergency_time = now;
        
        ESP_LOGI(TAG, "===== Indoor Emergency Alert (from ESP-NOW) =====");
        
        light_solid_color(s_led_strip, 255, 0, 0);
        s_led_on_start_ms = (uint32_t)(esp_timer_get_time() / 1000);
        s_led_turned_off_in_cleanup = false;
        
        mqtt_publish_emergency(s_emergency_count);
        
        oled_show_status("Emergency", "室内紧急告警！", "");
        
        while ((uint32_t)(esp_timer_get_time() / 1000) - s_led_on_start_ms < LED_DISPLAY_DURATION_MS) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        
        light_all_off(s_led_strip);
        oled_show_status("Smart Doorbell", "System Ready", "Waiting...");
        
        break;
    }

    case MSG_TYPE_HEARTBEAT:
        break;

    default:
        break;
    }
}

static void espnow_heartbeat_task(void *pvParameters)
{
    vTaskDelay(pdMS_TO_TICKS(5000));

    while (1) {
        esp_now_data_outdoor_t msg;
        memset(&msg, 0, sizeof(msg));

        msg.device_id = 0x02;
        msg.msg_type = MSG_TYPE_HEARTBEAT;
        msg.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
        msg.data_len = sprintf((char *)msg.data,
                               "OUTDOOR_HB:seq=%lu,doorbells=%lu",
                               (unsigned long)s_espnow_heartbeat_count,
                               (unsigned long)s_doorbell_count);

        esp_err_t ret = esp_now_outdoor_send_struct(indoor_mac, &msg);
        if (ret == ESP_OK) {
            s_espnow_heartbeat_count++;
        }

        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

static void button_control_task(void *pvParameters)
{
    TickType_t last_press_tick = 0;
    const TickType_t debounce_ms = pdMS_TO_TICKS(200);

    vTaskDelay(pdMS_TO_TICKS(2000));

    while (1) {
        int btn_level = gpio_get_level(BOOT_BUTTON_GPIO_NUM);

        if (btn_level == 0) {
            if ((xTaskGetTickCount() - last_press_tick) > debounce_ms) {
                last_press_tick = xTaskGetTickCount();
                buzzer_doorbell();
                ESP_LOGI(TAG, "BOOT button pressed -> buzzer");

                while (gpio_get_level(BOOT_BUTTON_GPIO_NUM) == 0) {
                    vTaskDelay(pdMS_TO_TICKS(50));
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static const char *state_name(sys_state_t state)
{
    switch (state) {
        case STATE_IDLE:       return "IDLE";
        case STATE_LISTENING:  return "LISTENING";
        case STATE_PROCESSING: return "PROCESSING";
        case STATE_CLEANUP:    return "CLEANUP";
        default:               return "UNKNOWN";
    }
}

static void fusion_decide_task(void *pvParameters)
{
    bool last_btn_pressed = false;
    sys_state_t last_state = STATE_IDLE;
    uint32_t last_status_print = 0;
    int last_sec_print = -1;

    ESP_LOGI(TAG, ">>> Fusion task started, initial state: %s", state_name(STATE_IDLE));

    audio_result_t init_audio = {0};
    audio_ai_get_latest_result(&init_audio);
    s_last_consumed_audio_seq = init_audio.inference_seq;
    ESP_LOGI(TAG, ">>> Sync audio seq: seq=%lu", (unsigned long)s_last_consumed_audio_seq);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(200));

        if (s_sys_state != last_state) {
            ESP_LOGI(TAG, ">>> 状态切换: %s -> %s", state_name(last_state), state_name(s_sys_state));
            last_state = s_sys_state;
        }

        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);

        /* 每2秒打印一次三通道状态（调试用） */
        if (s_initialized && s_sys_state == STATE_IDLE && now_ms - last_status_print >= 2000) {
            last_status_print = now_ms;
            bool btn_state = event_fusion_get_button_state();
            bool pir_state = event_fusion_get_pir_state();

            audio_result_t audio = {0};
            audio_ai_get_latest_result(&audio);
            const char *audio_state = "none";
            if (audio.valid) {
                audio_state = (audio.event == AUDIO_EVENT_KNOCK) ? "knock" : "doorbell";
            }

            ESP_LOGI(TAG, "[Status] btn:%d pir:%d audio:%s (conf=%.2f)",
                     btn_state, pir_state, audio_state, audio.valid ? audio.confidence : 0.0f);
        }

        switch (s_sys_state) {

        case STATE_IDLE: {
            /* 初始化未完成，跳过判决 */
            if (!s_initialized) {
                break;
            }

            /* 门外门铃按钮按下检测（轮询方式） */
            bool btn_pressed = door_button_is_pressed();
            if (btn_pressed && !last_btn_pressed) {
                buzzer_doorbell();
                ESP_LOGI(TAG, "[IDLE] Doorbell button pressed -> buzzer");
            }
            last_btn_pressed = btn_pressed;

            /* 1. 获取最新音频识别结果 */
            audio_result_t audio = {0};
            audio_ai_get_latest_result(&audio);

            /* 2. 更新音频通道状态（推理序列号去重） */
            bool audio_triggered = false;
            if (audio.valid &&
                (audio.event == AUDIO_EVENT_KNOCK || audio.event == AUDIO_EVENT_DOORBELL) &&
                audio.inference_seq != s_last_consumed_audio_seq) {
                audio_triggered = true;
                s_last_consumed_audio_seq = audio.inference_seq;
                const char *ev_name = (audio.event == AUDIO_EVENT_KNOCK) ? "knock" : "doorbell";
                ESP_LOGI(TAG, "[IDLE] Audio detected: %s (conf=%.2f, seq=%lu)",
                         ev_name, audio.confidence, (unsigned long)audio.inference_seq);
            }
            event_fusion_update_audio(audio_triggered);

            /* 3. 执行融合判决 */
            fusion_result_t fusion = {0};
            event_fusion_decide(&fusion);

            /* 4. 根据判决结果触发不同动作 */
            switch (fusion.event) {
                case FUSION_EVENT_VISITOR:
                    ESP_LOGI(TAG, "[IDLE] Fusion: VISITOR (btn=%d pir=%d audio=%d)",
                             fusion.button_active, fusion.pir_active, fusion.audio_active);
                    on_visitor_alert(&fusion, &audio);
                    break;
                case FUSION_EVENT_EMERGENCY:
                    ESP_LOGI(TAG, "[IDLE] Fusion: EMERGENCY");
                    on_emergency_alert();
                    break;
                case FUSION_EVENT_SINGLE_CHANNEL:
                case FUSION_EVENT_NONE:
                default:
                    break;
            }
            break;
        }

        case STATE_LISTENING: {
            /* 门外门铃按钮按下检测（任何状态下按门铃都响蜂鸣器） */
            bool btn_pressed = door_button_is_pressed();
            if (btn_pressed && !last_btn_pressed) {
                buzzer_doorbell();
                ESP_LOGI(TAG, "[LISTENING] Doorbell button pressed -> buzzer");
            }
            last_btn_pressed = btn_pressed;

            /* 5秒窗口超时检测 */
            uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
            uint32_t elapsed = now - s_listen_start_ms;
            int remain = (LISTEN_WINDOW_MS / 1000) - (int)(elapsed / 1000);

            if (remain >= 0 && remain != last_sec_print) {
                last_sec_print = remain;
                ESP_LOGI(TAG, "[LISTENING] Countdown: %d sec", remain);
            }

            if (elapsed >= LISTEN_WINDOW_MS) {
                ESP_LOGI(TAG, "[LISTENING] 5s window ended");
                command_recognition_stop();
                if (s_cmd_recognized && s_recognized_cmd != CMD_NONE) {
                    ESP_LOGI(TAG, "[LISTENING] Recognized: cmd_id=%d, msg=%s",
                             s_recognized_cmd, command_get_name(s_recognized_cmd));
                    queue_push(s_recognized_cmd);
                } else {
                    ESP_LOGI(TAG, "[LISTENING] No keyword -> normal doorbell(0)");
                    queue_push(CMD_NONE);
                }
                s_sys_state = STATE_PROCESSING;
            }
            break;
        }

        case STATE_PROCESSING: {
            /* 门外门铃按钮按下检测（任何状态下按门铃都响蜂鸣器） */
            bool btn_pressed = door_button_is_pressed();
            if (btn_pressed && !last_btn_pressed) {
                buzzer_doorbell();
                ESP_LOGI(TAG, "[PROCESSING] Doorbell button pressed -> buzzer");
            }
            last_btn_pressed = btn_pressed;

            /* 处理事件队列中的事件 */
            command_id_t cmd;
            if (queue_pop(&cmd)) {
                ESP_LOGI(TAG, "[PROCESSING] Processing event: cmd_id=%d, msg=%s",
                         cmd, command_get_name(cmd));
                trigger_doorbell_event(cmd);
                ESP_LOGI(TAG, "[PROCESSING] Event done: LED+ESP-NOW+MQTT+OLED all triggered");
                vTaskDelay(pdMS_TO_TICKS(OLED_DISPLAY_DELAY_MS));
            }

            /* 检查是否还有待处理事件 */
            if (queue_size() == 0) {
                ESP_LOGI(TAG, "[PROCESSING] Event queue empty, entering cleanup");
                s_sys_state = STATE_CLEANUP;
            }
            break;
        }

        case STATE_CLEANUP: {
            /* 门外门铃按钮按下检测（任何状态下按门铃都响蜂鸣器） */
            bool btn_pressed = door_button_is_pressed();
            if (btn_pressed && !last_btn_pressed) {
                buzzer_doorbell();
                ESP_LOGI(TAG, "[CLEANUP] Doorbell button pressed -> buzzer");
            }
            last_btn_pressed = btn_pressed;

            /* 等待两个条件：command_recognition停止 + LED显示满3秒 */
            uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
            uint32_t led_elapsed = now_ms - s_led_on_start_ms;
            bool cmd_stopped = !command_recognition_is_running();
            bool led_done = (led_elapsed >= LED_DISPLAY_DURATION_MS);

            if (cmd_stopped && led_done) {
                ESP_LOGI(TAG, "[CLEANUP] cmd_recog stopped + LED 3s done -> resume audio_ai");
                audio_ai_start();
                if (!s_led_turned_off_in_cleanup) {
                    light_all_off(s_led_strip);
                }
                s_sys_state = STATE_IDLE;
                oled_show_status("Smart Doorbell", "System Ready", "Waiting...");
                ESP_LOGI(TAG, "========================================");
                ESP_LOGI(TAG, "  Event cycle complete, waiting next visitor");
                ESP_LOGI(TAG, "========================================");
            } else {
                if (led_done && !s_led_turned_off_in_cleanup) {
                    /* LED时间到但识别还没停，先关灯 */
                    light_all_off(s_led_strip);
                    s_led_turned_off_in_cleanup = true;
                    ESP_LOGI(TAG, "[CLEANUP] LED 3s done, waiting cmd_recog stop");
                }
            }
            break;
        }

        } /* end switch */
    }
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    button_init();

    s_led_strip = light_init();
    if (s_led_strip != NULL) {
        light_all_off(s_led_strip);
        ESP_LOGI(TAG, "WS2812 LED strip initialized on GPIO5");
    } else {
        ESP_LOGE(TAG, "Failed to init LED strip!");
    }

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  ESP32-S3 Outdoor Board (Main Control)");
    ESP_LOGI(TAG, "  WiFi: configured (SSID hidden)");
    ESP_LOGI(TAG, "  MQTT: configured (credentials hidden)");
    ESP_LOGI(TAG, "  ESP-NOW Channel: %d", ESPNOW_CHANNEL);
    ESP_LOGI(TAG, "  ESP-NOW Peer: 58:E6:C5:74:6B:C8 (Indoor)");
    ESP_LOGI(TAG, "  WS2812: GPIO5 (30 LEDs)");
    ESP_LOGI(TAG, "  Features: 3-channel fusion -> visitor");
    ESP_LOGI(TAG, "           -> 5s voice keyword recognition");
    ESP_LOGI(TAG, "           -> event queue for multi-events");
    ESP_LOGI(TAG, "  Emergency -> red LED + MQTT emergency event");
    ESP_LOGI(TAG, "========================================");

    wifi_init();

    ESP_LOGI(TAG, ">>> Initializing ESP-NOW...");
    esp_err_t ret_espnow = esp_now_outdoor_init();
    ESP_LOGI(TAG, ">>> ESP-NOW init result: %s", esp_err_to_name(ret_espnow));

    if (ret_espnow == ESP_OK) {
        esp_now_outdoor_register_recv_cb(on_espnow_data_received);

        ret_espnow = esp_now_outdoor_add_peer(indoor_mac, "indoor_board");
        if (ret_espnow != ESP_OK) {
            ESP_LOGW(TAG, "Failed to add indoor peer");
        }

        xTaskCreate(espnow_heartbeat_task, "espnow_hb", 4096, NULL, 5, NULL);
        ESP_LOGI(TAG, "ESP-NOW ready - WiFi will connect in 3s (background)");
    } else {
        ESP_LOGW(TAG, "ESP-NOW init failed (%s), skipping ESP-NOW", esp_err_to_name(ret_espnow));
    }

    mqtt_init();

    ESP_LOGI(TAG, ">>> Initializing HZK Font...");
    esp_err_t ret_hzk = hzk_init();
    if (ret_hzk != ESP_OK) {
        ESP_LOGW(TAG, "HZK font init failed (%s), Chinese display may not work", esp_err_to_name(ret_hzk));
    }

    ESP_LOGI(TAG, ">>> Initializing OLED...");
    esp_err_t ret_oled = oled_init();
    if (ret_oled == ESP_OK) {
        ESP_LOGI(TAG, ">>> OLED initialized");
        oled_clear_buffer();
        oled_show_string("Smart Doorbell", 0, 0);
        oled_show_string("Starting...", 0, 16);
        oled_flush();
    } else {
        ESP_LOGW(TAG, "OLED init failed (%s)", esp_err_to_name(ret_oled));
    }

    ESP_LOGI(TAG, ">>> Initializing Buzzer...");
    esp_err_t ret_buzzer = buzzer_init();
    if (ret_buzzer == ESP_OK) {
        ESP_LOGI(TAG, ">>> Buzzer initialized on GPIO%d", BUZZER_GPIO_PIN);
    } else {
        ESP_LOGW(TAG, "Buzzer init failed (%s)", esp_err_to_name(ret_buzzer));
    }

    ESP_LOGI(TAG, ">>> Initializing Audio AI...");
    ESP_ERROR_CHECK(audio_ai_init());
    ESP_ERROR_CHECK(audio_ai_start());
    ESP_LOGI(TAG, ">>> Audio AI started");
    vTaskDelay(pdMS_TO_TICKS(1000));

    ESP_LOGI(TAG, ">>> Initializing Event Fusion...");
    ESP_ERROR_CHECK(event_fusion_init());
    ESP_LOGI(TAG, ">>> Event Fusion ready");

    ESP_LOGI(TAG, ">>> Initializing Command Recognition...");
    esp_err_t ret_cmd = command_recognition_init();
    if (ret_cmd == ESP_OK) {
        ESP_LOGI(TAG, ">>> Command Recognition ready");
    } else {
        ESP_LOGW(TAG, ">>> Command Recognition init failed (%s)", esp_err_to_name(ret_cmd));
    }

    s_initialized = true;
    ESP_LOGI(TAG, ">>> System init complete");

    xTaskCreate(fusion_decide_task, "fusion", 8192, NULL, 5, NULL);
    xTaskCreate(button_control_task, "btn_ctrl", 4096, NULL, 4, NULL);

    ESP_LOGI(TAG, "Outdoor main control board started!");
    ESP_LOGI(TAG, "Tasks: fusion, btn_ctrl, espnow_hb all created");
    ESP_LOGI(TAG, "OLED: 96x96 SPI, SCL=GPIO15, SDA=GPIO8");

    oled_show_status("Smart Doorbell", "System Ready", "Waiting...");

    ESP_LOGI(TAG, ">>> Starting 3-channel fusion decision");
}
