#ifndef UTF8_TO_GB2312_H
#define UTF8_TO_GB2312_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint16_t utf8_to_gb2312(const uint8_t *utf8);

#ifdef __cplusplus
}
#endif

#endif // UTF8_TO_GB2312_H