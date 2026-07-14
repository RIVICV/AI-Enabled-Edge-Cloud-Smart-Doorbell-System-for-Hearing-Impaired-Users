#ifndef BUZZER_H
#define BUZZER_H

#include "driver/gpio.h"
#include "esp_err.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define BUZZER_GPIO_PIN          1
#define BUZZER_PWM_CHANNEL       LEDC_CHANNEL_0
#define BUZZER_PWM_TIMER         LEDC_TIMER_0
#define BUZZER_PWM_MODE          LEDC_LOW_SPEED_MODE
#define BUZZER_PWM_RESOLUTION    LEDC_TIMER_13_BIT
#define BUZZER_PWM_FREQ          4000

typedef enum {
    BUZZER_MODE_OFF,
    BUZZER_MODE_SHORT_BEEP,
    BUZZER_MODE_LONG_BEEP,
    BUZZER_MODE_DOORBELL,
    BUZZER_MODE_EMERGENCY,
    BUZZER_MODE_CUSTOM,
} buzzer_mode_t;

esp_err_t buzzer_init(void);
void buzzer_off(void);
void buzzer_beep_short(void);
void buzzer_beep_long(void);
void buzzer_doorbell(void);
void buzzer_emergency(void);

#endif // BUZZER_H
