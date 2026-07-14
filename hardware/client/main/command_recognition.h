/**
 * @file command_recognition.h
 * @brief ESP-SR 命令词识别模块（分时复用架构）
 *
 * 基于 ESP-SR 的 AFE + MultiNet 实现 6 个中文命令词离线识别：
 *   - 外卖 (wai mai)   → "你的外卖到了！"
 *   - 快递 (kuai di)   → "你的快递到了！"
 *   - 物业 (wu ye)     → "物业来访！"
 *   - 维修 (wei xiu)   → "上门维修到了！"
 *   - 救命 (jiu ming)  → "有人寻求帮助！"
 *   - 紧急 (jin ji)    → "门外有紧急事件！"
 * 未识别到有效命令时显示："有访客来访！"
 *
 * 架构：
 *   常态：audio_ai 跑 TFLite 敲门/门铃检测，mic.c 独占 I2S
 *   触发：融合判决 VISITOR 事件 → 暂停 audio_ai → 启动本模块监听窗口
 *   监听：mic → AFE(NS+VAD) → MultiNet 识别命令词
 *   回常态：识别成功/超时 → 停止本模块 → 恢复 audio_ai
 *
 * 不使用 WakeNet 语音唤醒，用敲门/门铃融合事件当"软唤醒"。
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CMD_NONE     = 0,
    CMD_TAKEOUT  = 1,
    CMD_EXPRESS  = 2,
    CMD_PROPERTY = 3,
    CMD_REPAIR   = 4,
    CMD_HELP     = 5,
    CMD_URGENT   = 6,
} command_id_t;

typedef void (*command_callback_t)(command_id_t cmd, float confidence);

esp_err_t command_recognition_init(void);

esp_err_t command_recognition_start(command_callback_t callback, uint32_t timeout_ms);

esp_err_t command_recognition_stop(void);

bool command_recognition_is_running(void);

const char *command_get_name(command_id_t cmd);

#ifdef __cplusplus
}
#endif
