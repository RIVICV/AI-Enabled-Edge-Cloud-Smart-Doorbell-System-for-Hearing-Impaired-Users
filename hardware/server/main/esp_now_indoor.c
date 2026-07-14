#include "esp_now_indoor.h"

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "esp_now_indoor";

static esp_now_indoor_recv_cb_t s_recv_cb = NULL;
static bool s_peer_connected = false;

static void esp_now_send_cb(const esp_now_send_info_t *tx_info, esp_now_send_status_t status)
{
    if (tx_info == NULL) return;
    const uint8_t *mac_addr = tx_info->des_addr;
    if (status == ESP_NOW_SEND_SUCCESS) {
        ESP_LOGI(TAG, "Send success to " MACSTR, MAC2STR(mac_addr));
        s_peer_connected = true;
    } else {
        ESP_LOGW(TAG, "Send fail to " MACSTR, MAC2STR(mac_addr));
        s_peer_connected = false;
    }
}

static void esp_now_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len)
{
    if (recv_info == NULL || data == NULL || len <= 0) return;

    const uint8_t *src_mac = recv_info->src_addr;
    uint8_t expected_mac[6] = OUTDOOR_MAC_ADDR;
    if (memcmp(src_mac, expected_mac, 6) != 0) {
        ESP_LOGW(TAG, "Rejected message from unknown device: " MACSTR, MAC2STR(src_mac));
        return;
    }

    ESP_LOGI(TAG, "Received %d bytes from " MACSTR, len, MAC2STR(src_mac));
    s_peer_connected = true;
    if (s_recv_cb != NULL) {
        s_recv_cb(src_mac, data, len);
    }
}

static int scan_wifi_channel(void)
{
    wifi_scan_config_t scan_config = {
        .ssid = (uint8_t *)WIFI_SSID_SCAN,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 100,
        .scan_time.active.max = 300,
    };

    ESP_LOGI(TAG, "Scanning for '%s'...", WIFI_SSID_SCAN);
    esp_err_t ret = esp_wifi_scan_start(&scan_config, true);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Scan start failed: %s", esp_err_to_name(ret));
        return ESPNOW_CHANNEL;
    }

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    ESP_LOGI(TAG, "Found %d AP(s)", ap_count);

    int target_channel = ESPNOW_CHANNEL;
    if (ap_count > 0) {
        wifi_ap_record_t *ap_records = (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * ap_count);
        if (ap_records != NULL) {
            uint16_t records_to_get = ap_count;
            esp_wifi_scan_get_ap_records(&records_to_get, ap_records);
            for (int i = 0; i < records_to_get; i++) {
                ESP_LOGI(TAG, "  AP[%d]: %s, channel=%d, rssi=%d",
                         i, ap_records[i].ssid, ap_records[i].primary, ap_records[i].rssi);
                if (strcmp((char *)ap_records[i].ssid, WIFI_SSID_SCAN) == 0) {
                    target_channel = ap_records[i].primary;
                    ESP_LOGI(TAG, "  -> Target AP '%s' found on channel %d",
                             WIFI_SSID_SCAN, target_channel);
                    break;
                }
            }
            free(ap_records);
        }
    }

    // 释放内部扫描缓冲区
    esp_wifi_scan_get_ap_records(&ap_count, NULL);

    if (target_channel == ESPNOW_CHANNEL) {
        ESP_LOGW(TAG, "Target AP '%s' not found, using default channel %d",
                 WIFI_SSID_SCAN, ESPNOW_CHANNEL);
    }

    return target_channel;
}

esp_err_t esp_now_indoor_init(void)
{
    esp_err_t ret;

    // 注意：室内板不连接任何 WiFi/AP，只通过 ESP-NOW 与室外板通信
    // WiFi 驱动仅用于启动射频硬件，扫描仅用于获取室外板所在信道
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    // 设置室内板自定义 MAC 地址（必须在 start 之前）
    uint8_t custom_mac[6] = INDOOR_MAC_ADDR;
    ret = esp_wifi_set_mac(ESP_IF_WIFI_STA, custom_mac);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Custom MAC set: %02x:%02x:%02x:%02x:%02x:%02x",
                 custom_mac[0], custom_mac[1], custom_mac[2],
                 custom_mac[3], custom_mac[4], custom_mac[5]);
    } else {
        ESP_LOGW(TAG, "Failed to set custom MAC: %s", esp_err_to_name(ret));
    }

    ESP_ERROR_CHECK(esp_wifi_start());

    // 扫描 WiFi 获取室外板所连 AP 的实际信道（扫描 ≠ 连接）
    int ap_channel = scan_wifi_channel();

    ret = esp_wifi_set_channel(ap_channel, WIFI_SECOND_CHAN_NONE);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Set channel failed: %s", esp_err_to_name(ret));
    }
    ESP_LOGI(TAG, "ESP-NOW channel set to %d (AP channel)", ap_channel);

    ret = esp_now_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ESP-NOW init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_now_register_send_cb(esp_now_send_cb);
    if (ret != ESP_OK) return ret;

    ret = esp_now_register_recv_cb(esp_now_recv_cb);
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "ESP-NOW indoor module initialized");
    esp_now_indoor_print_mac();
    return ESP_OK;
}

esp_err_t esp_now_indoor_update_channel(void)
{
    int new_channel = scan_wifi_channel();
    uint8_t current_channel = 0;
    wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;

    esp_wifi_get_channel(&current_channel, &second);

    if (new_channel != current_channel) {
        esp_err_t ret = esp_wifi_set_channel(new_channel, WIFI_SECOND_CHAN_NONE);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Channel updated: %d -> %d", current_channel, new_channel);
        } else {
            ESP_LOGW(TAG, "Channel update failed: %s", esp_err_to_name(ret));
        }
        return ret;
    }
    return ESP_OK;
}

esp_err_t esp_now_indoor_deinit(void)
{
    esp_err_t ret = esp_now_deinit();
    esp_wifi_stop();
    esp_wifi_deinit();
    return ret;
}

esp_err_t esp_now_indoor_add_peer(const uint8_t *mac_addr, const char *name)
{
    if (mac_addr == NULL) return ESP_ERR_INVALID_ARG;
    if (esp_now_is_peer_exist(mac_addr)) return ESP_OK;

    esp_now_peer_info_t peer_info = {0};
    memcpy(peer_info.peer_addr, mac_addr, ESP_NOW_ETH_ALEN);
    peer_info.channel = 0;  // 跟随当前 WiFi 信道
    peer_info.ifidx = ESPNOW_WIFI_IF;
    peer_info.encrypt = false;

    esp_err_t ret = esp_now_add_peer(&peer_info);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Added peer: %s [" MACSTR "]", name ? name : "unknown", MAC2STR(mac_addr));
    }
    return ret;
}

esp_err_t esp_now_indoor_remove_peer(const uint8_t *mac_addr)
{
    if (mac_addr == NULL) return ESP_ERR_INVALID_ARG;
    return esp_now_del_peer(mac_addr);
}

esp_err_t esp_now_indoor_send_data(const uint8_t *mac_addr, const uint8_t *data, uint16_t len)
{
    if (mac_addr == NULL || data == NULL || len == 0) return ESP_ERR_INVALID_ARG;
    if (len > ESPNOW_MAX_DATA_LEN) return ESP_ERR_INVALID_SIZE;
    return esp_now_send(mac_addr, data, len);
}

esp_err_t esp_now_indoor_send_struct(const uint8_t *mac_addr, const esp_now_data_indoor_t *msg)
{
    if (mac_addr == NULL || msg == NULL) return ESP_ERR_INVALID_ARG;

    uint8_t buf[ESPNOW_MAX_DATA_LEN];
    uint16_t total_len = 8 + msg->data_len;
    if (total_len > ESPNOW_MAX_DATA_LEN) return ESP_ERR_INVALID_SIZE;

    buf[0] = msg->device_id;
    buf[1] = msg->msg_type;
    buf[2] = (msg->timestamp >> 24) & 0xFF;
    buf[3] = (msg->timestamp >> 16) & 0xFF;
    buf[4] = (msg->timestamp >> 8) & 0xFF;
    buf[5] = msg->timestamp & 0xFF;
    buf[6] = (msg->data_len >> 8) & 0xFF;
    buf[7] = msg->data_len & 0xFF;

    if (msg->data_len > 0) {
        memcpy(&buf[8], msg->data, msg->data_len);
    }

    return esp_now_indoor_send_data(mac_addr, buf, total_len);
}

void esp_now_indoor_register_recv_cb(esp_now_indoor_recv_cb_t cb)
{
    s_recv_cb = cb;
}

void esp_now_indoor_print_mac(void)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    ESP_LOGI(TAG, "MAC address: " MACSTR, MAC2STR(mac));
}

bool esp_now_indoor_is_peer_connected(void)
{
    return s_peer_connected;
}
