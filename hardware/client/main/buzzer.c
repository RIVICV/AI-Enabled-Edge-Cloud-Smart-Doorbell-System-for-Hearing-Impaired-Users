#include "buzzer.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"

static const char *TAG = "BUZZER";

#define BUZZER_QUEUE_SIZE      10

typedef struct {
    buzzer_mode_t mode;
    int param;
} buzzer_event_t;

static QueueHandle_t s_buzzer_queue = NULL;
static bool s_buzzer_task_running = false;
static bool s_is_beeping = false;

static void buzzer_set_freq(int freq_hz)
{
    ledc_set_freq(BUZZER_PWM_MODE, BUZZER_PWM_TIMER, freq_hz);
}

static void buzzer_set_duty(bool on)
{
    uint32_t duty = on ? (1 << (BUZZER_PWM_RESOLUTION - 1)) : 0;
    ledc_set_duty(BUZZER_PWM_MODE, BUZZER_PWM_CHANNEL, duty);
    ledc_update_duty(BUZZER_PWM_MODE, BUZZER_PWM_CHANNEL);
    s_is_beeping = on;
}

static void buzzer_task(void *arg)
{
    ESP_LOGI(TAG, "Buzzer task started");

    while (s_buzzer_task_running) {
        buzzer_event_t event;
        if (xQueueReceive(s_buzzer_queue, &event, pdMS_TO_TICKS(100)) == pdTRUE) {
            switch (event.mode) {
                case BUZZER_MODE_OFF:
                    buzzer_set_duty(false);
                    break;

                case BUZZER_MODE_SHORT_BEEP:
                    buzzer_set_freq(4000);
                    buzzer_set_duty(true);
                    vTaskDelay(pdMS_TO_TICKS(300));
                    buzzer_set_duty(false);
                    break;

                case BUZZER_MODE_LONG_BEEP:
                    buzzer_set_freq(4000);
                    buzzer_set_duty(true);
                    vTaskDelay(pdMS_TO_TICKS(800));
                    buzzer_set_duty(false);
                    break;

                case BUZZER_MODE_DOORBELL:
                    buzzer_set_freq(800);
                    buzzer_set_duty(true);
                    vTaskDelay(pdMS_TO_TICKS(300));
                    buzzer_set_duty(false);
                    vTaskDelay(pdMS_TO_TICKS(100));
                    buzzer_set_duty(true);
                    vTaskDelay(pdMS_TO_TICKS(400));
                    buzzer_set_duty(false);
                    break;

                case BUZZER_MODE_EMERGENCY:
                    for (int i = 0; i < 5; i++) {
                        buzzer_set_freq(1000);
                        buzzer_set_duty(true);
                        vTaskDelay(pdMS_TO_TICKS(150));
                        buzzer_set_duty(false);
                        vTaskDelay(pdMS_TO_TICKS(50));
                    }
                    break;

                case BUZZER_MODE_CUSTOM:
                    buzzer_set_freq(event.param > 0 ? event.param : 4000);
                    buzzer_set_duty(true);
                    vTaskDelay(pdMS_TO_TICKS(300));
                    buzzer_set_duty(false);
                    break;

                default:
                    break;
            }
        }
    }

    buzzer_set_duty(false);
    ESP_LOGI(TAG, "Buzzer task exited");
    vTaskDelete(NULL);
}

esp_err_t buzzer_init(void)
{
    ESP_LOGI(TAG, "Initializing passive buzzer on GPIO%d...", BUZZER_GPIO_PIN);

    ledc_timer_config_t timer_conf = {
        .speed_mode = BUZZER_PWM_MODE,
        .timer_num = BUZZER_PWM_TIMER,
        .duty_resolution = BUZZER_PWM_RESOLUTION,
        .freq_hz = BUZZER_PWM_FREQ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t ret = ledc_timer_config(&timer_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LEDC timer config failed");
        return ret;
    }

    ledc_channel_config_t channel_conf = {
        .speed_mode = BUZZER_PWM_MODE,
        .channel = BUZZER_PWM_CHANNEL,
        .timer_sel = BUZZER_PWM_TIMER,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = BUZZER_GPIO_PIN,
        .duty = 0,
        .hpoint = 0,
    };
    ret = ledc_channel_config(&channel_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LEDC channel config failed");
        return ret;
    }

    s_buzzer_queue = xQueueCreate(BUZZER_QUEUE_SIZE, sizeof(buzzer_event_t));
    if (!s_buzzer_queue) {
        ESP_LOGE(TAG, "Queue create failed");
        return ESP_ERR_NO_MEM;
    }

    s_buzzer_task_running = true;
    xTaskCreate(buzzer_task, "buzzer_task", 2048, NULL, 5, NULL);

    vTaskDelay(pdMS_TO_TICKS(50));
    buzzer_set_duty(false);

    ESP_LOGI(TAG, "Passive buzzer initialized successfully");
    return ESP_OK;
}

static void buzzer_send_event(buzzer_mode_t mode, int param)
{
    if (!s_buzzer_queue) {
        ESP_LOGW(TAG, "Buzzer not initialized");
        return;
    }

    buzzer_event_t event = { .mode = mode, .param = param };
    xQueueSendToBack(s_buzzer_queue, &event, pdMS_TO_TICKS(10));
}

void buzzer_off(void)
{
    buzzer_send_event(BUZZER_MODE_OFF, 0);
}

void buzzer_beep_short(void)
{
    buzzer_send_event(BUZZER_MODE_SHORT_BEEP, 0);
}

void buzzer_beep_long(void)
{
    buzzer_send_event(BUZZER_MODE_LONG_BEEP, 0);
}

void buzzer_doorbell(void)
{
    buzzer_send_event(BUZZER_MODE_DOORBELL, 0);
}

void buzzer_emergency(void)
{
    buzzer_send_event(BUZZER_MODE_EMERGENCY, 0);
}