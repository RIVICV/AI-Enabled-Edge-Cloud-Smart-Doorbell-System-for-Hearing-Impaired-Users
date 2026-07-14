#ifndef OLED_H
#define OLED_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OLED_SCK_PIN        GPIO_NUM_15
#define OLED_MOSI_PIN       GPIO_NUM_8
#define OLED_RES_PIN        GPIO_NUM_48
#define OLED_DC_PIN         GPIO_NUM_47
#define OLED_CS_PIN         GPIO_NUM_45

#define OLED_WIDTH          96
#define OLED_HEIGHT         96

esp_err_t oled_init(void);
void oled_clear(void);
void oled_fill(void);
void oled_show_string(const char *str, int x, int y);

void oled_show_chinese_char(uint16_t gb2312_code, int x, int y);
void oled_show_chinese_string(const char *str, int x, int y);

/* 帧缓冲区操作：先绘制到缓冲区，最后 flush 一次性刷新屏幕 */
void oled_clear_buffer(void);
void oled_flush(void);

#ifdef __cplusplus
}
#endif

#endif // OLED_H