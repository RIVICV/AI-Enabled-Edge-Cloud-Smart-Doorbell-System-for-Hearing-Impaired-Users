#include "light.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// VCC（红色）-5V，DI（绿色）-GPIO 5，GND（白色）
// 驱动WS2812灯带时，是否会启用ESP32-S3的DMA（直接内存访问）功能
// 需要同时使用Wi-Fi/蓝牙和驱动较长的WS2812灯带时，改为 1
#define LED_STRIP_USE_DMA      0

// 灯带的灯珠数量
#define LED_STRIP_LED_COUNT    30
#define LED_STRIP_GPIO_PIN     GPIO_NUM_5   // GPIO5
#define LED_STRIP_RMT_RES_HZ   (10 * 1000 * 1000)

static const char *TAG = "LIGHT";

// 初始化灯带
led_strip_handle_t light_init(void)
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_STRIP_GPIO_PIN,
        .max_leds = LED_STRIP_LED_COUNT,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags = {
            .invert_out = false,
        }
    };

    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = LED_STRIP_RMT_RES_HZ,
        .mem_block_symbols = 0,
        .flags = {
            .with_dma = LED_STRIP_USE_DMA,
        }
    };

    led_strip_handle_t led_strip;
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    ESP_LOGI(TAG, "Created LED strip object with RMT backend");
    return led_strip;
}

// 核心功能：门铃闪烁提醒（白色频闪）
// 参数：
//   strip   - 灯带句柄
//   times   - 闪烁次数
//   on_ms   - 亮的时间（毫秒）
//   off_ms  - 灭的时间（毫秒）
// 颜色：白色（255,255,255）
void light_flash(led_strip_handle_t strip, int times, int on_ms, int off_ms)
{
    light_flash_color(strip, times, on_ms, off_ms, 255, 255, 255);
}

void light_flash_color(led_strip_handle_t strip, int times, int on_ms, int off_ms,
                       uint8_t r, uint8_t g, uint8_t b)
{
    if (strip == NULL) {
        ESP_LOGE(TAG, "LED strip not initialized");
        return;
    }

    ESP_LOGI(TAG, "Flash color: R=%d G=%d B=%d, %d times, ON=%dms, OFF=%dms",
             r, g, b, times, on_ms, off_ms);

    for (int i = 0; i < times; i++) {
        for (int j = 0; j < LED_STRIP_LED_COUNT; j++) {
            ESP_ERROR_CHECK(led_strip_set_pixel(strip, j, r, g, b));
        }
        ESP_ERROR_CHECK(led_strip_refresh(strip));
        vTaskDelay(pdMS_TO_TICKS(on_ms));

        ESP_ERROR_CHECK(led_strip_clear(strip));
        ESP_ERROR_CHECK(led_strip_refresh(strip));
        vTaskDelay(pdMS_TO_TICKS(off_ms));
    }

    ESP_ERROR_CHECK(led_strip_clear(strip));
    ESP_ERROR_CHECK(led_strip_refresh(strip));
    ESP_LOGI(TAG, "Flash sequence complete");
}

void light_by_cmd(led_strip_handle_t strip, int cmd_id)
{
    switch (cmd_id) {
    case 0:
        /* 普通门铃: 白色, 闪5次 */
        light_flash_color(strip, 5, 200, 200, 255, 255, 255);
        break;
    case 1:
        /* 外卖: 黄色, 闪4次 */
        light_flash_color(strip, 4, 250, 200, 255, 255, 0);
        break;
    case 2:
        /* 快递: 蓝色, 闪4次 */
        light_flash_color(strip, 4, 250, 200, 0, 100, 255);
        break;
    case 3:
        /* 物业: 绿色, 闪3次 */
        light_flash_color(strip, 3, 300, 200, 0, 255, 0);
        break;
    case 4:
        /* 维修: 橙色, 闪4次 */
        light_flash_color(strip, 4, 250, 200, 255, 140, 0);
        break;
    case 5:
        /* 帮助: 紫色, 闪5次 */
        light_flash_color(strip, 5, 200, 150, 180, 0, 255);
        break;
    case 6:
        /* 紧急: 红色急闪, 闪10次 */
        light_flash_color(strip, 10, 100, 100, 255, 0, 0);
        break;
    default:
        light_flash_color(strip, 5, 200, 200, 255, 255, 255);
        break;
    }
}

// 预留接口：门铃事件触发闪烁
// 用法：当检测到门铃按键按下时，调用此函数
void light_doorbell_ring(led_strip_handle_t strip)
{
    // 门铃响：闪5次，每次200ms（亮200ms、灭200ms，节奏感更强）
    light_flash(strip, 5, 200, 200);
}

// 预留接口：紧急事件触发闪烁
// 用法：当检测到紧急呼叫按键按下时，调用此函数
void light_emergency(led_strip_handle_t strip)
{
    // 紧急呼叫：闪10次，每次100ms（更快更急促）
    light_flash(strip, 10, 100, 100);
}

// 常亮（白色）
void light_all_on(led_strip_handle_t strip)
{
    if (strip == NULL) return;
    for (int j = 0; j < LED_STRIP_LED_COUNT; j++) {
        led_strip_set_pixel(strip, j, 255, 255, 255);
    }
    led_strip_refresh(strip);
    ESP_LOGI(TAG, "All ON");
}

// 全灭
void light_all_off(led_strip_handle_t strip)
{
    if (strip == NULL) return;
    led_strip_clear(strip);
    led_strip_refresh(strip);
    ESP_LOGI(TAG, "All OFF");
}

void light_solid_color(led_strip_handle_t strip, uint8_t r, uint8_t g, uint8_t b)
{
    if (strip == NULL) {
        ESP_LOGE(TAG, "LED strip not initialized");
        return;
    }
    for (int j = 0; j < LED_STRIP_LED_COUNT; j++) {
        led_strip_set_pixel(strip, j, r, g, b);
    }
    led_strip_refresh(strip);
    ESP_LOGI(TAG, "Solid color: R=%d G=%d B=%d", r, g, b);
}

void light_solid_by_cmd(led_strip_handle_t strip, int cmd_id)
{
    switch (cmd_id) {
    case 0:
        /* 普通访客: 白色 */
        light_solid_color(strip, 255, 255, 255);
        break;
    case 1:
        /* 外卖: 黄色 */
        light_solid_color(strip, 255, 200, 0);
        break;
    case 2:
        /* 快递: 蓝色 */
        light_solid_color(strip, 0, 100, 255);
        break;
    case 3:
        /* 物业: 绿色 */
        light_solid_color(strip, 0, 255, 80);
        break;
    case 4:
        /* 维修: 橙色 */
        light_solid_color(strip, 255, 140, 0);
        break;
    case 5:
        /* 帮助: 紫色 */
        light_solid_color(strip, 180, 0, 255);
        break;
    case 6:
        /* 紧急: 红色 */
        light_solid_color(strip, 255, 0, 0);
        break;
    default:
        light_solid_color(strip, 255, 255, 255);
        break;
    }
}