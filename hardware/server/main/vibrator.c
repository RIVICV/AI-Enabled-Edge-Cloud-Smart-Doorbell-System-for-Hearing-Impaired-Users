#include <stdio.h>
#include "vibrator.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "VIBRATOR";
static SemaphoreHandle_t s_vibrator_mutex = NULL;

#define VIBRATOR_PWM_TIMER     LEDC_TIMER_0
#define VIBRATOR_PWM_CHANNEL   LEDC_CHANNEL_0
#define VIBRATOR_PWM_MODE      LEDC_LOW_SPEED_MODE
#define VIBRATOR_PWM_RESOLUTION LEDC_TIMER_8_BIT
#define VIBRATOR_PWM_FREQ      1500
#define VIBRATOR_DUTY_LEVEL    200

esp_err_t vibrator_init(void)
{
    ESP_LOGI(TAG, "Initializing vibrator on GPIO%d...", VIBRATOR_GPIO_PIN);

    s_vibrator_mutex = xSemaphoreCreateMutex();
    if (s_vibrator_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create vibrator mutex");
        return ESP_ERR_NO_MEM;
    }

    ledc_timer_config_t timer_conf = {
        .speed_mode = VIBRATOR_PWM_MODE,
        .timer_num = VIBRATOR_PWM_TIMER,
        .duty_resolution = VIBRATOR_PWM_RESOLUTION,
        .freq_hz = VIBRATOR_PWM_FREQ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t ret = ledc_timer_config(&timer_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PWM timer config failed");
        return ret;
    }

    ledc_channel_config_t channel_conf = {
        .speed_mode = VIBRATOR_PWM_MODE,
        .channel = VIBRATOR_PWM_CHANNEL,
        .timer_sel = VIBRATOR_PWM_TIMER,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = VIBRATOR_GPIO_PIN,
        .duty = 0,
        .hpoint = 0,
    };
    ret = ledc_channel_config(&channel_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PWM channel config failed");
        return ret;
    }

    ledc_fade_func_install(0);

    ESP_LOGI(TAG, "Vibrator initialized successfully!");
    return ESP_OK;
}

void vibrator_start(void)
{
    ledc_set_duty(VIBRATOR_PWM_MODE, VIBRATOR_PWM_CHANNEL, VIBRATOR_DUTY_LEVEL);
    ledc_update_duty(VIBRATOR_PWM_MODE, VIBRATOR_PWM_CHANNEL);
    ESP_LOGI(TAG, "Vibrator ON (duty=%d, freq=%dHz, GPIO=%d)", 
             VIBRATOR_DUTY_LEVEL, VIBRATOR_PWM_FREQ, VIBRATOR_GPIO_PIN);
}

void vibrator_stop(void)
{
    ledc_set_duty(VIBRATOR_PWM_MODE, VIBRATOR_PWM_CHANNEL, 0);
    ledc_update_duty(VIBRATOR_PWM_MODE, VIBRATOR_PWM_CHANNEL);
    ESP_LOGI(TAG, "Vibrator OFF");
}

void vibrator_once(int duration_ms)
{
    if (s_vibrator_mutex == NULL) return;

    if (xSemaphoreTake(s_vibrator_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        vibrator_start();
        vTaskDelay(pdMS_TO_TICKS(duration_ms));
        vibrator_stop();
        xSemaphoreGive(s_vibrator_mutex);
    } else {
        ESP_LOGW(TAG, "Vibrator busy, skip");
    }
}

void vibrator_short(void)
{
    ESP_LOGI(TAG, "Vibrator: short buzz (300ms)");
    vibrator_once(300);
}

void vibrator_long(void)
{
    ESP_LOGI(TAG, "Vibrator: long buzz (800ms)");
    vibrator_once(800);
}

void vibrator_start_with_intensity(uint8_t intensity)
{
    if (intensity > 255) intensity = 255;
    ledc_set_duty(VIBRATOR_PWM_MODE, VIBRATOR_PWM_CHANNEL, intensity);
    ledc_update_duty(VIBRATOR_PWM_MODE, VIBRATOR_PWM_CHANNEL);
    ESP_LOGI(TAG, "Vibrator ON, intensity: %d", intensity);
}

void vibrator_heartbeat(void)
{
    if (s_vibrator_mutex == NULL) return;

    if (xSemaphoreTake(s_vibrator_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        ESP_LOGI(TAG, "Vibrator: heartbeat pattern (short-short-long)");
        
        vibrator_start();
        vTaskDelay(pdMS_TO_TICKS(300));
        vibrator_stop();
        vTaskDelay(pdMS_TO_TICKS(200));
        
        vibrator_start();
        vTaskDelay(pdMS_TO_TICKS(300));
        vibrator_stop();
        vTaskDelay(pdMS_TO_TICKS(200));
        
        vibrator_start();
        vTaskDelay(pdMS_TO_TICKS(800));
        vibrator_stop();
        
        xSemaphoreGive(s_vibrator_mutex);
    } else {
        ESP_LOGW(TAG, "Vibrator busy, skip heartbeat");
    }
}

static void vibrator_pattern(int count, int on_ms, int off_ms)
{
    if (s_vibrator_mutex == NULL) return;

    if (xSemaphoreTake(s_vibrator_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (int i = 0; i < count; i++) {
            vibrator_start();
            vTaskDelay(pdMS_TO_TICKS(on_ms));
            vibrator_stop();
            if (i < count - 1) {
                vTaskDelay(pdMS_TO_TICKS(off_ms));
            }
        }
        xSemaphoreGive(s_vibrator_mutex);
    } else {
        ESP_LOGW(TAG, "Vibrator busy, skip");
    }
}

void vibrator_by_cmd(int cmd_id)
{
    switch (cmd_id) {
    case 0:
        ESP_LOGI(TAG, "Vibrator: CMD_NONE - 普通门铃（1次短震）");
        vibrator_pattern(1, 500, 0);
        break;
    case 1:
        ESP_LOGI(TAG, "Vibrator: CMD_TAKEOUT - 外卖（2次短震）");
        vibrator_pattern(2, 300, 200);
        break;
    case 2:
        ESP_LOGI(TAG, "Vibrator: CMD_EXPRESS - 快递（3次短震）");
        vibrator_pattern(3, 300, 200);
        break;
    case 3:
        ESP_LOGI(TAG, "Vibrator: CMD_PROPERTY - 物业（1次长震）");
        vibrator_pattern(1, 1000, 0);
        break;
    case 4:
        ESP_LOGI(TAG, "Vibrator: CMD_REPAIR - 维修（短-长）");
        vibrator_pattern(1, 200, 200);
        vTaskDelay(pdMS_TO_TICKS(200));
        vibrator_pattern(1, 800, 0);
        break;
    case 5:
        ESP_LOGI(TAG, "Vibrator: CMD_HELP - 救命（连续急促震动）");
        vibrator_pattern(5, 150, 100);
        break;
    case 6:
        ESP_LOGI(TAG, "Vibrator: CMD_URGENT - 紧急（长震+短震+长震）");
        vibrator_pattern(1, 800, 200);
        vibrator_pattern(2, 200, 200);
        vibrator_pattern(1, 800, 0);
        break;
    default:
        ESP_LOGW(TAG, "Vibrator: unknown cmd_id=%d, use default", cmd_id);
        vibrator_pattern(1, 500, 0);
        break;
    }
}
