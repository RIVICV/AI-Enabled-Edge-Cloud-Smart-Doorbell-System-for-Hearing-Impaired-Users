#ifndef FONT_CHINESE_H
#define FONT_CHINESE_H

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t hzk_init(void);
esp_err_t hzk_get_char(uint16_t gb2312_code, uint8_t *out_data);
int is_utf8_chinese(const uint8_t *str);

#ifdef __cplusplus
}
#endif

#endif