#include "emergency_button.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

static const char *TAG = "EMERGENCY_BTN";

static bool s_stable_pressed = false;
static bool s_falling_edge = false;
static bool s_rising_edge = false;
static uint64_t s_press_start_us = 0;
static esp_timer_handle_t s_debounce_timer = NULL;
static bool s_timer_level = true;
static bool s_initialized = false;

static void IRAM_ATTR emergency_button_isr_handler(void *arg);
static void debounce_timer_callback(void *arg);
static void update_button_state(bool level);

esp_err_t emergency_button_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "Emergency button already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing emergency button on GPIO%d...", EMERGENCY_BUTTON_GPIO_PIN);

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << EMERGENCY_BUTTON_GPIO_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO config failed");
        return ret;
    }

    ret = gpio_install_isr_service(0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "GPIO ISR service install failed");
        return ret;
    }

    ret = gpio_isr_handler_add(EMERGENCY_BUTTON_GPIO_PIN, emergency_button_isr_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO ISR handler add failed");
        return ret;
    }

    esp_timer_create_args_t timer_args = {
        .callback = debounce_timer_callback,
        .name = "btn_debounce"
    };
    ret = esp_timer_create(&timer_args, &s_debounce_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Debounce timer create failed");
        return ret;
    }

    int level = gpio_get_level(EMERGENCY_BUTTON_GPIO_PIN);
    s_stable_pressed = (level == 0);

    if (s_stable_pressed) {
        s_press_start_us = esp_timer_get_time();
    }

    s_initialized = true;

    ESP_LOGI(TAG, "Emergency button initialized successfully! Initial state: %s",
             s_stable_pressed ? "PRESSED" : "RELEASED");
    return ESP_OK;
}

static void IRAM_ATTR emergency_button_isr_handler(void *arg)
{
    int level = gpio_get_level(EMERGENCY_BUTTON_GPIO_PIN);
    s_timer_level = (level == 0);
    esp_timer_start_once(s_debounce_timer, EMERGENCY_BUTTON_DEBOUNCE_MS * 1000);
}

static void debounce_timer_callback(void *arg)
{
    int level = gpio_get_level(EMERGENCY_BUTTON_GPIO_PIN);
    bool current_level = (level == 0);

    if (current_level == s_timer_level) {
        update_button_state(current_level);
    }
}

static void update_button_state(bool level)
{
    bool prev_pressed = s_stable_pressed;

    if (level != prev_pressed) {
        if (level == true) {
            s_falling_edge = true;
            s_rising_edge = false;
            s_press_start_us = esp_timer_get_time();
            ESP_LOGI(TAG, "Button PRESSED");
        } else {
            s_rising_edge = true;
            s_falling_edge = false;
            ESP_LOGI(TAG, "Button RELEASED");
        }
        s_stable_pressed = level;
    }
}

bool emergency_button_is_pressed(void)
{
    return s_stable_pressed;
}

bool emergency_button_get_raw_level(void)
{
    return (gpio_get_level(EMERGENCY_BUTTON_GPIO_PIN) == 0);
}

uint32_t emergency_button_get_press_duration_ms(void)
{
    if (!s_stable_pressed) {
        return 0;
    }
    return (uint32_t)((esp_timer_get_time() - s_press_start_us) / 1000);
}

bool emergency_button_is_falling_edge(void)
{
    return s_falling_edge;
}

bool emergency_button_is_rising_edge(void)
{
    return s_rising_edge;
}

void emergency_button_clear_edge(void)
{
    s_falling_edge = false;
    s_rising_edge = false;
}

void emergency_button_poll(void)
{
}

const char* emergency_button_get_state_str(void)
{
    return s_stable_pressed ? "PRESSED" : "RELEASED";
}
