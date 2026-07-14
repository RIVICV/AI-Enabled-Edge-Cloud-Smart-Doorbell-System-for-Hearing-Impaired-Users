#ifndef EMERGENCY_BUTTON_H
#define EMERGENCY_BUTTON_H

#include "esp_err.h"
#include "stdbool.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EMERGENCY_BUTTON_GPIO_PIN     GPIO_NUM_8
#define EMERGENCY_BUTTON_DEBOUNCE_MS 50

esp_err_t emergency_button_init(void);
bool emergency_button_is_pressed(void);
bool emergency_button_get_raw_level(void);
uint32_t emergency_button_get_press_duration_ms(void);
bool emergency_button_is_falling_edge(void);
bool emergency_button_is_rising_edge(void);
void emergency_button_clear_edge(void);
void emergency_button_poll(void);
const char* emergency_button_get_state_str(void);

#ifdef __cplusplus
}
#endif

#endif
