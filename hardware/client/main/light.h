#ifndef LIGHT_H
#define LIGHT_H

#include "led_strip.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"

led_strip_handle_t light_init(void);

void light_doorbell_ring(led_strip_handle_t strip);

void light_emergency(led_strip_handle_t strip);

void light_flash(led_strip_handle_t strip, int times, int on_ms, int off_ms);

void light_flash_color(led_strip_handle_t strip, int times, int on_ms, int off_ms,
                       uint8_t r, uint8_t g, uint8_t b);

void light_by_cmd(led_strip_handle_t strip, int cmd_id);

void light_all_on(led_strip_handle_t strip);
void light_all_off(led_strip_handle_t strip);

void light_solid_color(led_strip_handle_t strip, uint8_t r, uint8_t g, uint8_t b);
void light_solid_by_cmd(led_strip_handle_t strip, int cmd_id);

#endif // LIGHT_H