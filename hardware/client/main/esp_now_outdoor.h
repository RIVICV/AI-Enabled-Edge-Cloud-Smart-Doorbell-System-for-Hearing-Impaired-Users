#ifndef ESP_NOW_OUTDOOR_H
#define ESP_NOW_OUTDOOR_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_now.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ESPNOW_MAX_DATA_LEN     250

// ESP-NOW 通信密钥（建议通过编译时传入）
#ifndef ESPNOW_PMK
#define ESPNOW_PMK              "pmk1234567890123"
#endif

#define ESPNOW_WIFI_MODE        WIFI_MODE_STA
#define ESPNOW_WIFI_IF          ESP_IF_WIFI_STA
#define ESPNOW_CHANNEL          11

// MAC 地址 —— 从编译时传入
#ifndef INDOOR_MAC_ADDR
#define INDOOR_MAC_ADDR         {0xe0, 0x72, 0xa1, 0xd2, 0x8b, 0x48}
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
} esp_now_data_outdoor_t;

typedef void (*esp_now_outdoor_recv_cb_t)(const uint8_t *mac_addr, const uint8_t *data, int len);

esp_err_t esp_now_outdoor_init(void);
esp_err_t esp_now_outdoor_deinit(void);
esp_err_t esp_now_outdoor_add_peer(const uint8_t *mac_addr, const char *name);
esp_err_t esp_now_outdoor_remove_peer(const uint8_t *mac_addr);
esp_err_t esp_now_outdoor_send_data(const uint8_t *mac_addr, const uint8_t *data, uint16_t len);
esp_err_t esp_now_outdoor_send_struct(const uint8_t *mac_addr, const esp_now_data_outdoor_t *msg);
void esp_now_outdoor_register_recv_cb(esp_now_outdoor_recv_cb_t cb);
void esp_now_outdoor_print_mac(void);
void esp_now_outdoor_sync_channel(uint8_t channel);

#ifdef __cplusplus
}
#endif

#endif