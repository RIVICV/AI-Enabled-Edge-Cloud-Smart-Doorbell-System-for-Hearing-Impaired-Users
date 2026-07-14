#ifndef MQTT_H
#define MQTT_H

#include <stdint.h>
#include <stdbool.h>
#include "mqtt_client.h"

// MQTT 配置 —— 从编译时传入，不硬编码
#ifndef MQTT_HOST
#define MQTT_HOST       "请替换为MQTT服务器IP"
#endif

#ifndef MQTT_PORT
#define MQTT_PORT       1883
#endif

#ifndef MQTT_USER
#define MQTT_USER       "请替换为MQTT用户名"
#endif

#ifndef MQTT_PASS
#define MQTT_PASS       "请替换为MQTT密码"
#endif

#define MQTT_CLIENT_ID  "esp32_s3_outdoor"

#define TOPIC_DATA      "esp32/s3/data"
#define TOPIC_DOORBELL  "device/esp32_s3_01/event"
#define TOPIC_SENSOR    "esp32/s3/sensor"
#define TOPIC_EMERGENCY "device/esp32_s3_01/emergency"
#define TOPIC_CMD       "esp32/s3/cmd"

#define MQTT_QUEUE_SIZE     50
#define MQTT_TOPIC_MAX_LEN  64
#define MQTT_PAYLOAD_MAX_LEN 256

extern esp_mqtt_client_handle_t s_mqtt_client;
extern bool s_mqtt_connected;

typedef struct {
    char topic[MQTT_TOPIC_MAX_LEN];
    char payload[MQTT_PAYLOAD_MAX_LEN];
    int qos;
    int retain;
} mqtt_cached_msg_t;

void mqtt_init(void);
void mqtt_publish_rgb(int color_idx, uint8_t r, uint8_t g, uint8_t b, int btn);
void mqtt_publish_doorbell(int msg_id);
void mqtt_publish_sensor(const char *sensor_data);
void mqtt_publish_emergency(int msg_id);
void mqtt_publish_command(int cmd_id, const char *msg);
int mqtt_get_queue_count(void);

#endif