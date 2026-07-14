#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "esp_timer.h"
#include "esp_now_indoor.h"
#include "vibrator.h"
#include "emergency_button.h"

static const char *TAG = "MAIN";

static uint8_t outdoor_mac[6] = OUTDOOR_MAC_ADDR;
static TaskHandle_t s_vibrator_task_handle = NULL;
static volatile int s_vibrator_cmd = 0;

#define BOOT_BUTTON_GPIO_NUM         0

static void button_init(void)
{
    gpio_reset_pin(BOOT_BUTTON_GPIO_NUM);
    gpio_set_direction(BOOT_BUTTON_GPIO_NUM, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BOOT_BUTTON_GPIO_NUM, GPIO_PULLUP_ONLY);
}

static void on_espnow_data_received(const uint8_t *mac_addr, const uint8_t *data, int len)
{
    if (len < 8) return;

    uint8_t device_id = data[0];
    uint8_t msg_type = data[1];
    uint16_t data_len = (data[6] << 8) | data[7];

    ESP_LOGI(TAG, "===== ESP-NOW Received =====");
    ESP_LOGI(TAG, "  device_id: 0x%02x, msg_type: %d, data_len: %d", device_id, msg_type, data_len);

    // 解析 payload（data[8] 开始是有效载荷）
    char payload[ESPNOW_MAX_DATA_LEN - 8] = {0};
    if (data_len > 0 && data_len < sizeof(payload)) {
        memcpy(payload, &data[8], data_len);
        payload[data_len] = '\0';
        ESP_LOGI(TAG, "  payload: %s", payload);
    }

    switch (msg_type) {
    case MSG_TYPE_CONTROL:
        ESP_LOGI(TAG, ">>> 收到室外板判决消息！执行反应...");
        ESP_LOGI(TAG, ">>> 内容: %s", payload);
        if (s_vibrator_task_handle != NULL) {
            xTaskNotifyGive(s_vibrator_task_handle);
        }
        break;

    case MSG_TYPE_DOORBELL:
        ESP_LOGI(TAG, ">>> 收到室外板门铃/命令消息！");
        ESP_LOGI(TAG, ">>> 内容: %s", payload);

        int cmd_id = 0;
        sscanf(payload, "%d", &cmd_id);
        s_vibrator_cmd = cmd_id;
        ESP_LOGI(TAG, ">>> 解析命令ID: %d", cmd_id);

        if (s_vibrator_task_handle != NULL) {
            xTaskNotifyGive(s_vibrator_task_handle);
        }
        break;

    case MSG_TYPE_HEARTBEAT:
        ESP_LOGI(TAG, ">>> 收到室外板心跳");
        break;

    case MSG_TYPE_EMERGENCY:
        ESP_LOGI(TAG, ">>> 收到室外板紧急消息: %s", payload);
        break;

    default:
        ESP_LOGW(TAG, "未知消息类型: %d", msg_type);
        break;
    }
}

static void connection_check_task(void *pvParameters)
{
    bool last_state = false;
    int disconnect_count = 0;

    while (1) {
        bool current_state = esp_now_indoor_is_peer_connected();

        if (current_state != last_state) {
            if (current_state) {
                ESP_LOGI(TAG, "========================================");
                ESP_LOGI(TAG, "  [连接成功] ESP-NOW 与室外板通信正常！");
                ESP_LOGI(TAG, "  室外板 MAC: 58:E6:C5:74:6B:C8");
                ESP_LOGI(TAG, "  室内板 MAC: E0:72:A1:D2:8B:48");
                ESP_LOGI(TAG, "========================================");
            } else {
                ESP_LOGW(TAG, "========================================");
                ESP_LOGW(TAG, "  [连接断开] ESP-NOW 与室外板通信中断！");
                ESP_LOGW(TAG, "========================================");
            }
            last_state = current_state;
        }

        // 如果连续多次检测不到通信，重新扫描信道
        if (!current_state) {
            disconnect_count++;
            if (disconnect_count >= 6) { // 约 3 秒无通信
                ESP_LOGW(TAG, "通信中断，尝试重新扫描 WiFi 信道...");
                esp_now_indoor_update_channel();
                disconnect_count = 0;
            }
        } else {
            disconnect_count = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

static void emergency_button_task(void *pvParameters)
{
    uint32_t press_count = 0;

    while (1) {
        if (emergency_button_is_falling_edge()) {
            emergency_button_clear_edge();

            press_count++;
            ESP_LOGI(TAG, "========================================");
            ESP_LOGI(TAG, "  [紧急按键按下] 发送求助信号！");
            ESP_LOGI(TAG, "  按下次数: %lu", (unsigned long)press_count);
            ESP_LOGI(TAG, "========================================");

            vibrator_short();

            esp_now_data_indoor_t msg;
            memset(&msg, 0, sizeof(msg));
            msg.device_id = 0x01;
            msg.msg_type = MSG_TYPE_EMERGENCY;
            msg.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
            msg.data_len = sprintf((char *)msg.data, "EMERGENCY:count=%lu", (unsigned long)press_count);

            esp_err_t ret = esp_now_indoor_send_struct(outdoor_mac, &msg);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "紧急消息已发送到室外板");
            } else {
                ESP_LOGW(TAG, "紧急消息发送失败: %s", esp_err_to_name(ret));
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void vibrator_task(void *pvParameters)
{
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        
        int cmd = s_vibrator_cmd;
        ESP_LOGI(TAG, ">>> 触发马达震动 (cmd=%d)", cmd);
        vibrator_by_cmd(cmd);
        ESP_LOGI(TAG, ">>> 马达震动完成");
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
    vibrator_init();
    emergency_button_init();

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  ESP32-S3 Indoor Board (室内板)");
    ESP_LOGI(TAG, "  MAC: E0:72:A1:D2:8B:48");
    ESP_LOGI(TAG, "  ESP-NOW Peer: 58:E6:C5:74:6B:C8 (室外板)");
    ESP_LOGI(TAG, "  ESP-NOW 信道: %d", ESPNOW_CHANNEL);
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  功能:");
    ESP_LOGI(TAG, "    - 接收室外板判决消息 -> 震动");
    ESP_LOGI(TAG, "    - 紧急按键按下 -> 发送求助信号 + 震动");
    ESP_LOGI(TAG, "    - 检测 ESP-NOW 连接状态");
    ESP_LOGI(TAG, "========================================");

    ret = esp_now_indoor_init();
    if (ret == ESP_OK) {
        esp_now_indoor_register_recv_cb(on_espnow_data_received);
        ret = esp_now_indoor_add_peer(outdoor_mac, "outdoor_board");
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to add outdoor peer");
        }

        xTaskCreate(connection_check_task, "conn_check", 2048, NULL, 4, NULL);
        xTaskCreate(emergency_button_task, "emergency_btn", 4096, NULL, 5, NULL);
        xTaskCreate(vibrator_task, "vibrator", 4096, NULL, 5, &s_vibrator_task_handle);
    } else {
        ESP_LOGE(TAG, "ESP-NOW init failed");
    }

    ESP_LOGI(TAG, "室内板启动完成，等待 ESP-NOW 连接...");
}
