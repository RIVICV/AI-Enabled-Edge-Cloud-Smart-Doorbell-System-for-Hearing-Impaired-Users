#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "mqtt.h"

static const char *TAG = "MQTT";

esp_mqtt_client_handle_t s_mqtt_client = NULL;
bool s_mqtt_connected = false;

static QueueHandle_t s_mqtt_queue = NULL;

static void mqtt_flush_queue(void);
static void mqtt_status_task(void *pvParameters);

#define MQTT_STATUS_PRINT_INTERVAL_MS  10000

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected to %s:%d", MQTT_HOST, MQTT_PORT);
        s_mqtt_connected = true;

        int msg_id = esp_mqtt_client_subscribe(s_mqtt_client, TOPIC_CMD, 1);
        ESP_LOGI(TAG, "Subscribed to %s, msg_id=%d", TOPIC_CMD, msg_id);

        mqtt_flush_queue();
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT disconnected");
        s_mqtt_connected = false;
        if (s_mqtt_queue != NULL) {
            int count = uxQueueMessagesWaiting(s_mqtt_queue);
            if (count > 0) {
                ESP_LOGW(TAG, "Current cache queue: %d messages pending", count);
            }
        }
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT subscribed, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT unsubscribed, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT published, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT data received:");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT error");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
            ESP_LOGE(TAG, "Connection refused, reason code: %d", event->error_handle->connect_return_code);
        }
        break;

    default:
        break;
    }
}

static void mqtt_flush_queue(void)
{
    if (s_mqtt_queue == NULL) return;

    int count = uxQueueMessagesWaiting(s_mqtt_queue);
    if (count == 0) {
        ESP_LOGI(TAG, "No cached messages to flush");
        return;
    }

    ESP_LOGI(TAG, "Flushing %d cached MQTT messages...", count);

    mqtt_cached_msg_t msg;
    int sent = 0;
    int failed = 0;

    while (xQueueReceive(s_mqtt_queue, &msg, 0) == pdPASS) {
        int msg_id = esp_mqtt_client_enqueue(s_mqtt_client,
                                             msg.topic, msg.payload,
                                             strlen(msg.payload),
                                             msg.qos, msg.retain,
                                             pdMS_TO_TICKS(200));
        if (msg_id >= 0) {
            sent++;
            ESP_LOGI(TAG, "  Flushed [%s]: %s", msg.topic, msg.payload);
        } else {
            failed++;
            ESP_LOGW(TAG, "  Flush failed [%s]: %s", msg.topic, msg.payload);
        }
    }

    ESP_LOGI(TAG, "Flush complete: sent=%d, failed=%d", sent, failed);
}

static bool mqtt_enqueue_msg(const char *topic, const char *payload, int qos, int retain)
{
    if (s_mqtt_queue == NULL || topic == NULL || payload == NULL) {
        return false;
    }

    mqtt_cached_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    strncpy(msg.topic, topic, MQTT_TOPIC_MAX_LEN - 1);
    strncpy(msg.payload, payload, MQTT_PAYLOAD_MAX_LEN - 1);
    msg.qos = qos;
    msg.retain = retain;

    if (xQueueSend(s_mqtt_queue, &msg, pdMS_TO_TICKS(100)) != pdPASS) {
        ESP_LOGW(TAG, "MQTT queue full, dropping message: %s", topic);
        return false;
    }

    int count = uxQueueMessagesWaiting(s_mqtt_queue);
    ESP_LOGI(TAG, "Message cached: topic=%s, queue=%d/%d", topic, count, MQTT_QUEUE_SIZE);
    return true;
}

void mqtt_init(void)
{
    s_mqtt_queue = xQueueCreate(MQTT_QUEUE_SIZE, sizeof(mqtt_cached_msg_t));
    if (s_mqtt_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create MQTT queue");
        return;
    }

    char uri[128];
    snprintf(uri, sizeof(uri), "mqtt://%s:%d", MQTT_HOST, MQTT_PORT);

    ESP_LOGI(TAG, "MQTT connecting to %s", uri);
    ESP_LOGI(TAG, "MQTT Auth: client_id=%s, username=%s", MQTT_CLIENT_ID, MQTT_USER);
    ESP_LOGI(TAG, "MQTT cache queue size: %d", MQTT_QUEUE_SIZE);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = uri,
        .credentials.client_id = MQTT_CLIENT_ID,
        .credentials.username = MQTT_USER,
        .credentials.authentication.password = MQTT_PASS,
    };

    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(s_mqtt_client,
        ESP_EVENT_ANY_ID, mqtt_event_handler, NULL));
    ESP_ERROR_CHECK(esp_mqtt_client_start(s_mqtt_client));

    xTaskCreate(mqtt_status_task, "mqtt_status", 2048, NULL, 3, NULL);
}

static void mqtt_status_task(void *pvParameters)
{
    TickType_t last_print = 0;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        if (s_mqtt_queue == NULL) continue;

        int count = uxQueueMessagesWaiting(s_mqtt_queue);
        
        if (!s_mqtt_connected && count > 0) {
            TickType_t now = xTaskGetTickCount();
            if ((now - last_print) >= pdMS_TO_TICKS(MQTT_STATUS_PRINT_INTERVAL_MS)) {
                last_print = now;
                ESP_LOGW(TAG, "Offline cache: %d/%d messages pending",
                         count, MQTT_QUEUE_SIZE);
            }
        }
    }
}

void mqtt_publish_rgb(int color_idx, uint8_t r, uint8_t g, uint8_t b, int btn)
{
    char payload[128];
    snprintf(payload, sizeof(payload),
             "{\"r\":%d,\"g\":%d,\"b\":%d,\"color\":%d,\"btn\":%d}",
             r, g, b, color_idx + 1, btn);

    if (s_mqtt_connected && s_mqtt_client != NULL) {
        int msg_id = esp_mqtt_client_enqueue(s_mqtt_client, TOPIC_DATA,
                                              payload, strlen(payload),
                                              1, 0, pdMS_TO_TICKS(500));
        if (msg_id >= 0) {
            ESP_LOGI(TAG, "RGB published: %s", payload);
            return;
        }
    }

    if (mqtt_enqueue_msg(TOPIC_DATA, payload, 1, 0)) {
        ESP_LOGW(TAG, "MQTT offline, RGB message cached");
    }
}

void mqtt_publish_doorbell(int msg_id)
{
    char payload[128];
    snprintf(payload, sizeof(payload), "{\"event\":\"doorbell\",\"cmd_id\":0,\"msg\":\"有访客来访！\"}");

    if (s_mqtt_connected && s_mqtt_client != NULL) {
        int pub_msgid = esp_mqtt_client_enqueue(s_mqtt_client, TOPIC_DOORBELL,
                                                payload, strlen(payload),
                                                1, 0, pdMS_TO_TICKS(1000));
        if (pub_msgid >= 0) {
            ESP_LOGI(TAG, "Doorbell sent: %s, msg_id=%d", payload, pub_msgid);
            return;
        }
    }

    if (mqtt_enqueue_msg(TOPIC_DOORBELL, payload, 1, 0)) {
        ESP_LOGW(TAG, "MQTT offline, doorbell message cached");
    }
}

void mqtt_publish_sensor(const char *sensor_data)
{
    if (sensor_data == NULL) return;

    if (s_mqtt_connected && s_mqtt_client != NULL) {
        int pub_msgid = esp_mqtt_client_enqueue(s_mqtt_client, TOPIC_SENSOR,
                                                sensor_data, strlen(sensor_data),
                                                0, 0, pdMS_TO_TICKS(1000));
        if (pub_msgid >= 0) {
            ESP_LOGI(TAG, "Sensor published: %s", sensor_data);
            return;
        }
    }

    if (mqtt_enqueue_msg(TOPIC_SENSOR, sensor_data, 0, 0)) {
        ESP_LOGW(TAG, "MQTT offline, sensor data cached");
    }
}

void mqtt_publish_emergency(int msg_id)
{
    char payload[64];
    snprintf(payload, sizeof(payload), "{\"event\":\"emergency\",\"msg_id\":%d}", msg_id);

    if (s_mqtt_connected && s_mqtt_client != NULL) {
        int pub_msgid = esp_mqtt_client_enqueue(s_mqtt_client, TOPIC_EMERGENCY,
                                                payload, strlen(payload),
                                                1, 0, pdMS_TO_TICKS(1000));
        if (pub_msgid >= 0) {
            ESP_LOGI(TAG, "🚨 EMERGENCY sent: %s, msg_id=%d", payload, pub_msgid);
            return;
        }
    }

    if (mqtt_enqueue_msg(TOPIC_EMERGENCY, payload, 1, 0)) {
        ESP_LOGW(TAG, "EMERGENCY cached, will send when WiFi is back");
    }
}

void mqtt_publish_command(int cmd_id, const char *msg)
{
    char payload[128];
    snprintf(payload, sizeof(payload), "{\"event\":\"doorbell\",\"cmd_id\":%d,\"msg\":\"%s\"}", cmd_id, msg);

    if (s_mqtt_connected && s_mqtt_client != NULL) {
        int pub_msgid = esp_mqtt_client_enqueue(s_mqtt_client, TOPIC_DOORBELL,
                                                payload, strlen(payload),
                                                1, 0, pdMS_TO_TICKS(1000));
        if (pub_msgid >= 0) {
            ESP_LOGI(TAG, "Command sent: %s, msg_id=%d", payload, pub_msgid);
            return;
        }
    }

    if (mqtt_enqueue_msg(TOPIC_DOORBELL, payload, 1, 0)) {
        ESP_LOGW(TAG, "Command cached, will send when WiFi is back");
    }
}

int mqtt_get_queue_count(void)
{
    if (s_mqtt_queue == NULL) return 0;
    return uxQueueMessagesWaiting(s_mqtt_queue);
}
