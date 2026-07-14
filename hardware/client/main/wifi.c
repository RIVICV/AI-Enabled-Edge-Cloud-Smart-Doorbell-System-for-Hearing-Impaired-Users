#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "wifi.h"

static const char *TAG = "WIFI";

EventGroupHandle_t s_wifi_event_group = NULL;
static int s_retry_num = 0;
static TaskHandle_t s_wifi_reconnect_task = NULL;
static volatile bool s_wifi_connected = false;
static volatile bool s_wifi_started = false;

static void wifi_reconnect_task(void *pvParameters)
{
    ESP_LOGI(TAG, "WiFi reconnect task started");

    // 延迟 3 秒再开始连接，让 ESP-NOW 先稳定
    ESP_LOGI(TAG, "Waiting 3s before WiFi connect (for ESP-NOW stability)...");
    vTaskDelay(pdMS_TO_TICKS(3000));

    // 第一次连接
    if (!s_wifi_started) {
        s_wifi_started = true;
        ESP_LOGI(TAG, "First WiFi connect attempt...");
        esp_wifi_connect();
    }

    while (1) {
        if (s_wifi_connected) {
            // 已连接，等待断开通知
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
            continue;
        }

        // 每 15 秒尝试重连一次，避免占用信道影响 ESP-NOW
        ESP_LOGW(TAG, "WiFi disconnected, retrying in 15s (attempt %d)...", s_retry_num + 1);
        vTaskDelay(pdMS_TO_TICKS(15000));

        if (s_wifi_connected) {
            continue;
        }

        s_retry_num++;
        ESP_LOGI(TAG, "WiFi reconnect attempt %d...", s_retry_num);
        esp_wifi_connect();
    }
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi STA started, will connect after delay");
        // 不在这里连接，由重连任务统一管理连接时机

    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_wifi_connected = false;
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        ESP_LOGW(TAG, "WiFi disconnected");

        // 通知重连任务开始重连
        if (s_wifi_reconnect_task != NULL) {
            xTaskNotifyGive(s_wifi_reconnect_task);
        }

    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "WiFi connected, IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_wifi_connected = true;
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

        // 获取当前 WiFi 信道（ESP-NOW peer channel=0 会自动跟随）
        uint8_t primary = 0;
        wifi_second_chan_t second = 0;
        if (esp_wifi_get_channel(&primary, &second) == ESP_OK) {
            ESP_LOGI(TAG, "WiFi connected on channel %d", primary);
        }

        // 通知重连任务已连接
        if (s_wifi_reconnect_task != NULL) {
            xTaskNotifyGive(s_wifi_reconnect_task);
        }
    }
}

void wifi_init(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    // 设置室外板自定义 MAC 地址（必须在 start 之前设置）
    uint8_t custom_mac[6] = OUTDOOR_MAC_ADDR;
    esp_err_t mac_ret = esp_wifi_set_mac(ESP_IF_WIFI_STA, custom_mac);
    if (mac_ret == ESP_OK) {
        ESP_LOGI(TAG, "Custom MAC set: %02x:%02x:%02x:%02x:%02x:%02x",
                 custom_mac[0], custom_mac[1], custom_mac[2],
                 custom_mac[3], custom_mac[4], custom_mac[5]);
    } else {
        ESP_LOGW(TAG, "Failed to set custom MAC: %s", esp_err_to_name(mac_ret));
    }

    // 创建重连任务，优先级低，避免影响 ESP-NOW
    xTaskCreate(wifi_reconnect_task, "wifi_reconn", 4096, NULL, 2, &s_wifi_reconnect_task);

    ESP_LOGI(TAG, "WiFi init done, connecting in background...");

    // 启动 WiFi（不阻塞）
    ESP_ERROR_CHECK(esp_wifi_start());
}