#include "esp_now_outdoor.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "esp_now_outdoor";

static esp_now_outdoor_recv_cb_t s_recv_cb = NULL;
static bool s_espnow_initialized = false;
static uint8_t s_peer_mac[6] = {0};
static bool s_peer_added = false;

static void esp_now_send_cb(const esp_now_send_info_t *tx_info, esp_now_send_status_t status)
{
    if (tx_info == NULL) {
        ESP_LOGE(TAG, "Send cb arg error");
        return;
    }

    const uint8_t *mac_addr = tx_info->des_addr;

    if (status == ESP_NOW_SEND_SUCCESS) {
        ESP_LOGI(TAG, "✅ Send success to " MACSTR, MAC2STR(mac_addr));
    } else {
        ESP_LOGW(TAG, "❌ Send fail to " MACSTR, MAC2STR(mac_addr));
    }
}

static void esp_now_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len)
{
    if (recv_info == NULL || data == NULL || len <= 0) {
        ESP_LOGE(TAG, "Receive cb arg error");
        return;
    }

    ESP_LOGI(TAG, "Received %d bytes from " MACSTR, len, MAC2STR(recv_info->src_addr));

    if (s_recv_cb != NULL) {
        s_recv_cb(recv_info->src_addr, data, len);
    }
}

esp_err_t esp_now_outdoor_init(void)
{
    ESP_LOGI(TAG, "Starting ESP-NOW outdoor init...");

    esp_err_t ret;
    int retry = 0;

    // 等待 WiFi 驱动就绪
    while (1) {
        wifi_mode_t mode;
        ret = esp_wifi_get_mode(&mode);
        if (ret == ESP_OK && mode != WIFI_MODE_NULL) {
            ESP_LOGI(TAG, "WiFi driver ready, mode: %d", mode);
            break;
        }

        retry++;
        if (retry > 10) {
            ESP_LOGE(TAG, "WiFi driver not ready after %d retries", retry);
            return ESP_ERR_TIMEOUT;
        }

        ESP_LOGI(TAG, "Waiting WiFi driver... (%d/10)", retry);
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    // 设置 ESP-NOW 信道为 11（和室内板一致）
    ret = esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Set channel failed: %s (may already be set)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "ESP-NOW channel: %d", ESPNOW_CHANNEL);
    }

    ret = esp_now_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ESP-NOW init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "ESP-NOW initialized");

    ret = esp_now_register_send_cb(esp_now_send_cb);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Register send cb failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Send callback registered");

    ret = esp_now_register_recv_cb(esp_now_recv_cb);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Register recv cb failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Receive callback registered");

    s_espnow_initialized = true;
    ESP_LOGI(TAG, "ESP-NOW outdoor module initialized successfully");
    esp_now_outdoor_print_mac();

    return ESP_OK;
}

esp_err_t esp_now_outdoor_deinit(void)
{
    esp_err_t ret = esp_now_deinit();
    s_espnow_initialized = false;

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ESP-NOW deinit failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "ESP-NOW outdoor module deinitialized");
    return ESP_OK;
}

esp_err_t esp_now_outdoor_add_peer(const uint8_t *mac_addr, const char *name)
{
    if (mac_addr == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (esp_now_is_peer_exist(mac_addr)) {
        ESP_LOGW(TAG, "Peer " MACSTR " already exists", MAC2STR(mac_addr));
        return ESP_OK;
    }

    esp_now_peer_info_t peer_info;
    memset(&peer_info, 0, sizeof(peer_info));
    memcpy(peer_info.peer_addr, mac_addr, ESP_NOW_ETH_ALEN);
    peer_info.channel = 0;  // 跟随当前 WiFi 信道，自动适配 AP 信道变化
    peer_info.ifidx = ESPNOW_WIFI_IF;
    peer_info.encrypt = false;

    esp_err_t ret = esp_now_add_peer(&peer_info);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Add peer failed: %s", esp_err_to_name(ret));
        return ret;
    }

    memcpy(s_peer_mac, mac_addr, ESP_NOW_ETH_ALEN);
    s_peer_added = true;

    ESP_LOGI(TAG, "Added peer: %s [" MACSTR "]",
             name ? name : "unknown", MAC2STR(mac_addr));

    return ESP_OK;
}

esp_err_t esp_now_outdoor_remove_peer(const uint8_t *mac_addr)
{
    if (mac_addr == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = esp_now_del_peer(mac_addr);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Remove peer failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Removed peer " MACSTR, MAC2STR(mac_addr));
    return ESP_OK;
}

esp_err_t esp_now_outdoor_send_data(const uint8_t *mac_addr, const uint8_t *data, uint16_t len)
{
    if (!s_espnow_initialized) {
        ESP_LOGE(TAG, "ESP-NOW not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (mac_addr == NULL || data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (len > ESP_NOW_MAX_DATA_LEN) {
        ESP_LOGE(TAG, "Data length too large: %d > %d", len, ESP_NOW_MAX_DATA_LEN);
        return ESP_ERR_INVALID_SIZE;
    }

    // 检查 peer 是否存在
    if (!esp_now_is_peer_exist(mac_addr)) {
        ESP_LOGE(TAG, "Peer " MACSTR " not exists", MAC2STR(mac_addr));
        return ESP_ERR_NOT_FOUND;
    }

    // 小延迟确保 WiFi 状态稳定
    vTaskDelay(pdMS_TO_TICKS(10));

    esp_err_t ret = esp_now_send(mac_addr, data, len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Send failed: %s", esp_err_to_name(ret));
    }

    return ret;
}

esp_err_t esp_now_outdoor_send_struct(const uint8_t *mac_addr, const esp_now_data_outdoor_t *msg)
{
    if (!s_espnow_initialized) {
        ESP_LOGE(TAG, "ESP-NOW not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (mac_addr == NULL || msg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t buf[ESPNOW_MAX_DATA_LEN];
    uint16_t total_len = 8 + msg->data_len;

    if (total_len > ESPNOW_MAX_DATA_LEN) {
        ESP_LOGE(TAG, "Message too large: %d > %d", total_len, ESPNOW_MAX_DATA_LEN);
        return ESP_ERR_INVALID_SIZE;
    }

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

    return esp_now_outdoor_send_data(mac_addr, buf, total_len);
}

void esp_now_outdoor_register_recv_cb(esp_now_outdoor_recv_cb_t cb)
{
    s_recv_cb = cb;
}

void esp_now_outdoor_print_mac(void)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    ESP_LOGI(TAG, "MAC address: " MACSTR, MAC2STR(mac));
}

void esp_now_outdoor_sync_channel(uint8_t channel)
{
    if (!s_espnow_initialized) {
        ESP_LOGW(TAG, "ESP-NOW not initialized, skip channel sync");
        return;
    }

    if (channel == 0 || channel > 14) {
        ESP_LOGW(TAG, "Invalid channel: %d", channel);
        return;
    }

    ESP_LOGI(TAG, "Syncing ESP-NOW to WiFi channel %d...", channel);

    // 删除旧 peer（如果不删除，新信道不会生效）
    if (s_peer_added) {
        ESP_LOGI(TAG, "Removing old peer before channel change...");
        esp_now_del_peer(s_peer_mac);
        s_peer_added = false;
    }

    // 设置 WiFi 信道
    esp_err_t ret = esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Set WiFi channel failed: %s", esp_err_to_name(ret));
    }

    // 重新添加 peer（使用新信道）
    if (s_peer_mac[0] != 0 || s_peer_mac[1] != 0) {
        esp_now_peer_info_t peer_info = {0};
        memcpy(peer_info.peer_addr, s_peer_mac, ESP_NOW_ETH_ALEN);
        peer_info.channel = channel;
        peer_info.ifidx = ESPNOW_WIFI_IF;
        peer_info.encrypt = false;

        ret = esp_now_add_peer(&peer_info);
        if (ret == ESP_OK) {
            s_peer_added = true;
            ESP_LOGI(TAG, "Peer re-added with new channel %d", channel);
        } else {
            ESP_LOGE(TAG, "Re-add peer failed: %s", esp_err_to_name(ret));
        }
    }

    ESP_LOGI(TAG, "ESP-NOW channel synced to %d", channel);
}