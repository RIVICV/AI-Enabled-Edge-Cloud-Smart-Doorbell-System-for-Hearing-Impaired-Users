/**
 * @file command_recognition.c
 * @brief ESP-SR 命令词识别实现（AFE + MultiNet 分时复用）
 */
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "command_recognition.h"
#include "mic.h"

#include "esp_afe_sr_iface.h"
#include "esp_afe_config.h"
#include "esp_afe_sr_models.h"
#include "esp_mn_iface.h"
#include "esp_mn_models.h"
#include "esp_wn_iface.h"
#include "esp_wn_models.h"
#include "model_path.h"
#include "esp_mn_speech_commands.h"

static const char *TAG = "CMD_REC";

static const esp_afe_sr_iface_t *s_afe_handle = NULL;
static esp_afe_sr_data_t  *s_afe_data   = NULL;
static esp_mn_iface_t     *s_multinet   = NULL;
static model_iface_data_t *s_mn_model   = NULL;
static srmodel_list_t     *s_models     = NULL;

static TaskHandle_t s_feed_task  = NULL;
static TaskHandle_t s_fetch_task = NULL;
static SemaphoreHandle_t s_stop_mutex = NULL;

static command_callback_t s_callback  = NULL;
static uint32_t  s_timeout_ms = 0;
static uint32_t  s_start_time = 0;
static volatile bool s_running = false;

static void feed_task(void *arg)
{
    int feed_chunksize = s_afe_handle->get_feed_chunksize(s_afe_data);
    int16_t *buff = malloc(feed_chunksize * sizeof(int16_t));
    if (!buff) {
        ESP_LOGE(TAG, "feed buffer malloc failed");
        s_running = false;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "feed_task started, chunksize=%d", feed_chunksize);

    int feed_count = 0;
    int zero_read_count = 0;
    while (s_running) {
        int total = 0;
        while (total < feed_chunksize && s_running) {
            int n = mic_read_samples(buff + total, feed_chunksize - total,
                                     pdMS_TO_TICKS(100));
            if (n <= 0) {
                zero_read_count++;
                if (zero_read_count <= 3 || zero_read_count % 10 == 0) {
                    ESP_LOGW(TAG, "mic_read_samples returned %d (zero_read_count=%d)", n, zero_read_count);
                }
                break;
            }
            zero_read_count = 0;
            total += n;
        }
        if (total == feed_chunksize) {
            s_afe_handle->feed(s_afe_data, buff);
            feed_count++;
        }
    }

    ESP_LOGI(TAG, "feed_task exited (total feed=%d)", feed_count);
    free(buff);
    vTaskDelete(NULL);
}

static void fetch_task(void *arg)
{
    ESP_LOGI(TAG, "fetch_task started, timeout=%lu ms", (unsigned long)s_timeout_ms);

    bool already_detected = false;
    int fetch_fail_count = 0;

    while (s_running) {
        afe_fetch_result_t *res = s_afe_handle->fetch(s_afe_data);
        if (!res || res->ret_value == ESP_FAIL) {
            fetch_fail_count++;
            if (fetch_fail_count <= 3) {
                ESP_LOGW(TAG, "AFE fetch failed (%d consecutive)", fetch_fail_count);
            }
            if (fetch_fail_count >= 20) {
                ESP_LOGE(TAG, "AFE fetch failed %d times, giving up", fetch_fail_count);
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        fetch_fail_count = 0;

        esp_mn_state_t state = s_multinet->detect(s_mn_model, res->data);

        if (state == ESP_MN_STATE_DETECTED) {
            esp_mn_results_t *result = s_multinet->get_results(s_mn_model);
            if (result->num > 0) {
                int phrase_id = result->phrase_id[0];
                float prob = result->prob[0];
                ESP_LOGI(TAG, "Command detected: id=%d, prob=%.3f", phrase_id, prob);

                /* 只回调第一次识别到的有效命令，后续继续监听但不重复回调 */
                if (!already_detected && s_callback) {
                    command_id_t cmd = CMD_NONE;
                    switch (phrase_id) {
                        case 1: cmd = CMD_TAKEOUT;   break;
                        case 2: cmd = CMD_EXPRESS;   break;
                        case 3: cmd = CMD_PROPERTY;  break;
                        case 4: cmd = CMD_REPAIR;    break;
                        case 5: cmd = CMD_HELP;      break;
                        case 6: cmd = CMD_URGENT;    break;
                        default: break;
                    }
                    if (cmd != CMD_NONE) {
                        already_detected = true;
                        s_callback(cmd, prob);
                        ESP_LOGI(TAG, "First command reported, keep listening until timeout");
                    }
                }
            }
        } else if (state == ESP_MN_STATE_TIMEOUT) {
            ESP_LOGI(TAG, "MultiNet internal timeout");
        }

        /* 5秒固定窗口，超时后退出 */
        uint32_t now = esp_timer_get_time() / 1000;
        if (now - s_start_time > s_timeout_ms) {
            ESP_LOGI(TAG, "Listening window timeout (%lu ms), exiting", (unsigned long)s_timeout_ms);
            break;
        }
    }

    s_running = false;
    ESP_LOGI(TAG, "fetch_task exited");
    vTaskDelete(NULL);
}

esp_err_t command_recognition_init(void)
{
    if (s_afe_handle != NULL) {
        ESP_LOGW(TAG, "Already initialized, skipping");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing ESP-SR command recognition engine...");

    s_models = esp_srmodel_init("model");
    if (s_models == NULL) {
        ESP_LOGE(TAG, "ESP-SR model load failed, check if model partition is flashed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "ESP-SR model loaded successfully, total=%d", s_models->num);

    afe_config_t *afe_config = afe_config_init("M", s_models,
                                                AFE_TYPE_SR, AFE_MODE_HIGH_PERF);
    if (afe_config == NULL) {
        ESP_LOGE(TAG, "AFE config creation failed");
        return ESP_FAIL;
    }

    afe_config->wakenet_init = false;
    afe_config->wakenet_model_name = NULL;

    s_afe_handle = esp_afe_handle_from_config(afe_config);
    if (s_afe_handle == NULL) {
        ESP_LOGE(TAG, "AFE handle creation failed");
        afe_config_free(afe_config);
        return ESP_FAIL;
    }

    s_afe_data = s_afe_handle->create_from_config(afe_config);
    afe_config_free(afe_config);
    if (s_afe_data == NULL) {
        ESP_LOGE(TAG, "AFE instance creation failed");
        return ESP_FAIL;
    }

    char *mn_name = esp_srmodel_filter(s_models, ESP_MN_PREFIX, ESP_MN_CHINESE);
    if (mn_name == NULL) {
        ESP_LOGE(TAG, "Chinese MultiNet model not found, check menuconfig");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "MultiNet model: %s", mn_name);

    s_multinet = esp_mn_handle_from_name(mn_name);
    if (s_multinet == NULL) {
        ESP_LOGE(TAG, "MultiNet handle creation failed");
        return ESP_FAIL;
    }

    s_mn_model = s_multinet->create(mn_name, 5760);
    if (s_mn_model == NULL) {
        ESP_LOGE(TAG, "MultiNet model instance creation failed");
        return ESP_FAIL;
    }

    esp_mn_commands_alloc(s_multinet, s_mn_model);
    esp_mn_commands_add(1, "wai mai");
    esp_mn_commands_add(2, "kuai di");
    esp_mn_commands_add(3, "wu ye");
    esp_mn_commands_add(4, "wei xiu");
    esp_mn_commands_add(5, "jiu ming");
    esp_mn_commands_add(6, "jin ji");
    esp_mn_error_t *err = esp_mn_commands_update();
    if (err != NULL) {
        ESP_LOGW(TAG, "Some commands failed to parse, but available commands applied");
    }

    if (s_stop_mutex == NULL) {
        s_stop_mutex = xSemaphoreCreateMutex();
    }

    ESP_LOGI(TAG, "Command recognition engine initialized");
    ESP_LOGI(TAG, "  Commands: 外卖/快递/物业/维修/救命/紧急");
    ESP_LOGI(TAG, "  AFE feed chunksize: %d", s_afe_handle->get_feed_chunksize(s_afe_data));
    ESP_LOGI(TAG, "  AFE fetch chunksize: %d", s_afe_handle->get_fetch_chunksize(s_afe_data));

    return ESP_OK;
}

esp_err_t command_recognition_start(command_callback_t callback, uint32_t timeout_ms)
{
    if (s_afe_handle == NULL || s_multinet == NULL) {
        ESP_LOGE(TAG, "Not initialized, call command_recognition_init() first");
        return ESP_ERR_INVALID_STATE;
    }
    if (s_running) {
        ESP_LOGW(TAG, "Already running, ignoring");
        return ESP_OK;
    }

    xSemaphoreTake(s_stop_mutex, portMAX_DELAY);
    s_callback = callback;
    s_timeout_ms = timeout_ms;
    s_start_time = esp_timer_get_time() / 1000;
    s_running = true;

    s_multinet->clean(s_mn_model);
    s_afe_handle->reset_buffer(s_afe_data);
    mic_flush();

    BaseType_t ret;
    ret = xTaskCreatePinnedToCore(feed_task, "cmd_feed", 6 * 1024, NULL, 8, &s_feed_task, 1);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "feed_task creation failed");
        s_running = false;
        xSemaphoreGive(s_stop_mutex);
        return ESP_ERR_NO_MEM;
    }

    ret = xTaskCreatePinnedToCore(fetch_task, "cmd_fetch", 6 * 1024, NULL, 8, &s_fetch_task, 1);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "fetch_task creation failed");
        s_running = false;
        if (s_feed_task) {
            vTaskDelete(s_feed_task);
            s_feed_task = NULL;
        }
        xSemaphoreGive(s_stop_mutex);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Command listening window started (timeout=%lu ms)", (unsigned long)timeout_ms);
    xSemaphoreGive(s_stop_mutex);
    return ESP_OK;
}

esp_err_t command_recognition_stop(void)
{
    if (!s_running) {
        return ESP_OK;
    }

    xSemaphoreTake(s_stop_mutex, portMAX_DELAY);
    s_running = false;
    xSemaphoreGive(s_stop_mutex);

    /* 等待 feed_task 和 fetch_task 完全退出 */
    int wait_count = 0;
    while (wait_count < 50) {
        eTaskState feed_state = s_feed_task ? eTaskGetState(s_feed_task) : eDeleted;
        eTaskState fetch_state = s_fetch_task ? eTaskGetState(s_fetch_task) : eDeleted;

        if ((feed_state == eDeleted || feed_state == eInvalid) &&
            (fetch_state == eDeleted || fetch_state == eInvalid)) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
        wait_count++;
    }

    xSemaphoreTake(s_stop_mutex, portMAX_DELAY);
    s_feed_task = NULL;
    s_fetch_task = NULL;
    s_callback = NULL;
    ESP_LOGI(TAG, "Command listening stopped (waited %d*100ms)", wait_count);
    xSemaphoreGive(s_stop_mutex);
    return ESP_OK;
}

bool command_recognition_is_running(void)
{
    return s_running;
}

const char *command_get_name(command_id_t cmd)
{
    switch (cmd) {
        case CMD_TAKEOUT:   return "您的外卖到了！";
        case CMD_EXPRESS:   return "您的快递到了！";
        case CMD_PROPERTY:  return "物业来访！";
        case CMD_REPAIR:    return "上门维修到了！";
        case CMD_HELP:      return "有人寻求帮助！";
        case CMD_URGENT:    return "门外有紧急事件！";
        default:            return "有访客来访！";
    }
}
