#include "event_fusion.h"

#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "door_button.h"
#include "pir.h"

#define BUTTON_HOLD_MS          2000
#define PIR_HOLD_MS             3000
#define AUDIO_HOLD_MS           3000
#define EMERGENCY_HOLD_MS       5000
#define VISITOR_COOLDOWN_MS     3000

typedef struct {
    bool button_active;
    bool pir_active;
    bool audio_active;
    bool emergency_active;

    uint32_t button_trigger_ms;
    uint32_t pir_trigger_ms;
    uint32_t audio_trigger_ms;
    uint32_t emergency_trigger_ms;
} fusion_state_t;

static fusion_state_t s_state = {0};
static uint32_t s_last_visitor_ms = 0;
static bool s_emergency_processed = false;

static void update_button_channel(void)
{
    uint32_t now = esp_timer_get_time() / 1000ULL;
    if (door_button_is_pressed()) {
        if (!s_state.button_active) {
            s_state.button_active = true;
            s_state.button_trigger_ms = now;
        }
        s_state.button_trigger_ms = now;
    } else if (s_state.button_active && (now - s_state.button_trigger_ms > BUTTON_HOLD_MS)) {
        s_state.button_active = false;
    }
}

static void update_pir_channel(void)
{
    uint32_t now = esp_timer_get_time() / 1000ULL;
    if (pir_is_detected()) {
        s_state.pir_trigger_ms = now;
        s_state.pir_active = true;
    } else if (now - s_state.pir_trigger_ms > PIR_HOLD_MS) {
        s_state.pir_active = false;
    }
}

static bool is_audio_active(void)
{
    uint32_t now = esp_timer_get_time() / 1000ULL;
    if (s_state.audio_active && (now - s_state.audio_trigger_ms <= AUDIO_HOLD_MS)) {
        return true;
    }
    s_state.audio_active = false;
    return false;
}

static bool is_emergency_active(void)
{
    uint32_t now = esp_timer_get_time() / 1000ULL;
    if (s_state.emergency_active && (now - s_state.emergency_trigger_ms <= EMERGENCY_HOLD_MS)) {
        return true;
    }
    s_state.emergency_active = false;
    return false;
}

esp_err_t event_fusion_init(void)
{
    ESP_ERROR_CHECK(door_button_init());
    ESP_ERROR_CHECK(pir_init());

    memset(&s_state, 0, sizeof(s_state));

    ESP_LOGI("FUSION", "等待传感器预热...");
    vTaskDelay(pdMS_TO_TICKS(5000));
    ESP_LOGI("FUSION", "传感器预热完成");

    return ESP_OK;
}

void event_fusion_update_audio(bool audio_triggered)
{
    if (audio_triggered && !s_state.audio_active) {
        s_state.audio_active = true;
        s_state.audio_trigger_ms = esp_timer_get_time() / 1000ULL;
    }
}

void event_fusion_trigger_emergency(void)
{
    s_state.emergency_active = true;
    s_state.emergency_trigger_ms = esp_timer_get_time() / 1000ULL;
    s_emergency_processed = false;
}

esp_err_t event_fusion_decide(fusion_result_t *result)
{
    if (!result) return ESP_ERR_INVALID_ARG;

    memset(result, 0, sizeof(fusion_result_t));

    update_button_channel();
    update_pir_channel();

    bool audio = is_audio_active();
    bool pir = s_state.pir_active;
    bool button = s_state.button_active;
    bool emergency = is_emergency_active();

    result->button_active = button;
    result->pir_active = pir;
    result->audio_active = audio;
    result->emergency_active = emergency;
    result->timestamp_ms = esp_timer_get_time() / 1000ULL;

    if (emergency && !s_emergency_processed) {
        result->event = FUSION_EVENT_EMERGENCY;
        s_emergency_processed = true;
        return ESP_OK;
    }

    int active_count = (button ? 1 : 0) + (pir ? 1 : 0) + (audio ? 1 : 0);

    if (active_count >= 2) {
        uint32_t now = esp_timer_get_time() / 1000ULL;
        if (now - s_last_visitor_ms >= VISITOR_COOLDOWN_MS) {
            result->event = FUSION_EVENT_VISITOR;
            s_last_visitor_ms = now;
        } else {
            result->event = FUSION_EVENT_NONE;
        }
    } else if (active_count == 1) {
        result->event = FUSION_EVENT_SINGLE_CHANNEL;
    } else {
        result->event = FUSION_EVENT_NONE;
    }

    return ESP_OK;
}

bool event_fusion_get_emergency_state(void)
{
    return is_emergency_active();
}

bool event_fusion_get_button_state(void)
{
    return s_state.button_active;
}

bool event_fusion_get_pir_state(void)
{
    return s_state.pir_active;
}
