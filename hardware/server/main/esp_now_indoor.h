#ifndef ESP_NOW_INDOOR_H
#define ESP_NOW_INDOOR_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_now.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ESPNOW_MAX_DATA_LEN     250
#define ESPNOW_PMK              "pmk1234567890123"
#define ESPNOW_LMK              "lmk1234567890123"
#define ESPNOW_WIFI_MODE        WIFI_MODE_STA
#define ESPNOW_WIFI_IF          ESP_IF_WIFI_STA
#define ESPNOW_CHANNEL          11

#define WIFI_SSID_SCAN          "***"

// 室内板 MAC 地址 —— 从编译时传入
#ifndef INDOOR_MAC_ADDR
#define INDOOR_MAC_ADDR     {0xe0, 0x72, 0xa1, 0xd2, 0x8b, 0x48}
#endif

#ifndef OUTDOOR_MAC_ADDR
#define OUTDOOR_MAC_ADDR    {0x58, 0xe6, 0xc5, 0x74, 0x6b, 0xc8}
#endif
typedef enum {
    MSG_TYPE_DATA = 0,
    MSG_TYPE_ACK = 1,
    MSG_TYPE_HEARTBEAT = 2,
    MSG_TYPE_CONFIG = 3,
    MSG_TYPE_DOORBELL = 4,
    MSG_TYPE_SENSOR = 5,
    MSG_TYPE_CONTROL = 6,
    MSG_TYPE_EMERGENCY = 7,
    MSG_TYPE_INDOOR_DATA = 8,
} esp_now_msg_type_t;

typedef struct {
    uint8_t device_id;
    uint8_t msg_type;
    uint32_t timestamp;
    uint8_t data[ESPNOW_MAX_DATA_LEN - 8];
    uint16_t data_len;
} esp_now_data_indoor_t;

typedef void (*esp_now_indoor_recv_cb_t)(const uint8_t *mac_addr, const uint8_t *data, int len);

esp_err_t esp_now_indoor_init(void);
esp_err_t esp_now_indoor_deinit(void);
esp_err_t esp_now_indoor_update_channel(void);
esp_err_t esp_now_indoor_add_peer(const uint8_t *mac_addr, const char *name);
esp_err_t esp_now_indoor_remove_peer(const uint8_t *mac_addr);
esp_err_t esp_now_indoor_send_data(const uint8_t *mac_addr, const uint8_t *data, uint16_t len);
esp_err_t esp_now_indoor_send_struct(const uint8_t *mac_addr, const esp_now_data_indoor_t *msg);
void esp_now_indoor_register_recv_cb(esp_now_indoor_recv_cb_t cb);
void esp_now_indoor_print_mac(void);
bool esp_now_indoor_is_peer_connected(void);

#ifdef __cplusplus
}
#endif
#endif
