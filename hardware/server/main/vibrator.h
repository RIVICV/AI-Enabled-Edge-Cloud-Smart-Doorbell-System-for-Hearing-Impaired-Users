#ifndef VIBRATOR_H
#define VIBRATOR_H

#include "esp_err.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VIBRATOR_GPIO_PIN     GPIO_NUM_7

esp_err_t vibrator_init(void);
void vibrator_start(void);
void vibrator_stop(void);
void vibrator_once(int duration_ms);
void vibrator_short(void);
void vibrator_long(void);
void vibrator_start_with_intensity(uint8_t intensity);
void vibrator_heartbeat(void);

/*
 * 命令ID震动模式对照表
 * 命令ID    命令类型    震动模式
 * 0         普通门铃    1次短震（500ms）
 * 1         外卖        2次短震（300ms+200ms间隔）
 * 2         快递        3次短震（300ms+200ms间隔）
 * 3         物业        1次长震（1000ms）
 * 4         维修        短-长（200ms+200ms+800ms）
 * 5         救命        连续急促5次（150ms+100ms间隔）
 * 6         紧急        长-短-短-长（SOS模式）
 */
typedef enum {
    VIB_CMD_NONE     = 0,
    VIB_CMD_TAKEOUT  = 1,
    VIB_CMD_EXPRESS  = 2,
    VIB_CMD_PROPERTY = 3,
    VIB_CMD_REPAIR   = 4,
    VIB_CMD_HELP     = 5,
    VIB_CMD_URGENT   = 6,
} vibrator_cmd_t;

void vibrator_by_cmd(int cmd_id);

#ifdef __cplusplus
}
#endif

#endif
