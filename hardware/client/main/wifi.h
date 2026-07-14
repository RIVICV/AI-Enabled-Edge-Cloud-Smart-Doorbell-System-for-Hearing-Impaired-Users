#ifndef WIFI_H
#define WIFI_H

#include "freertos/FreeRTOS.h"
#include "esp_err.h"

// WiFi 配置 —— 从编译时传入，不硬编码
// 如果未定义，则使用默认占位值（编译时会提示）
#ifndef WIFI_SSID
#define WIFI_SSID       "请替换为您的WiFi名称"
#endif

#ifndef WIFI_PASS
#define WIFI_PASS       "请替换为您的WiFi密码"
#endif

// 室外板自定义 MAC 地址
#define OUTDOOR_MAC_ADDR    {0x58, 0xe6, 0xc5, 0x74, 0x6b, 0xc8}

// WiFi 事件位
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

extern EventGroupHandle_t s_wifi_event_group;

void wifi_init(void);

#endif // WIFI_H