#include "oled.h"
#include <string.h>
#include "esp_log.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "font_ascii.h"
#include "font_chinese.h"
#include "utf8_to_gb2312.h"

static const char *TAG = "OLED";
static spi_device_handle_t spi_handle = NULL;
static SemaphoreHandle_t s_oled_mutex = NULL;

/* 帧缓冲区: 12 pages x 96 columns = 1152 字节 */
#define OLED_PAGES  (OLED_HEIGHT / 8)
static uint8_t s_framebuf[OLED_PAGES][OLED_WIDTH];

static void oled_flush_internal(void);

static void oled_spi_write(uint8_t data, bool is_cmd)
{
    gpio_set_level(OLED_DC_PIN, is_cmd ? 0 : 1);

    spi_transaction_t trans = {
        .length = 8,
        .tx_buffer = &data,
    };

    spi_device_transmit(spi_handle, &trans);
}

static void oled_write_cmd(uint8_t cmd)
{
    oled_spi_write(cmd, true);
}

static void oled_write_data_bulk(const uint8_t *data, size_t len)
{
    gpio_set_level(OLED_DC_PIN, 1);

    spi_transaction_t trans = {
        .length = len * 8,
        .tx_buffer = data,
    };

    spi_device_transmit(spi_handle, &trans);
}

static void oled_set_page_col(int page, int col)
{
    int col_offset = 16;
    col = col + col_offset;
    if (col > 127) col = 127;

    oled_write_cmd(0xB0 + page);
    oled_write_cmd(col & 0x0F);
    oled_write_cmd(0x10 | (col >> 4));
}

static esp_err_t oled_spi_init(void)
{
    spi_bus_config_t bus_config = {
        .mosi_io_num = OLED_MOSI_PIN,
        .miso_io_num = -1,
        .sclk_io_num = OLED_SCK_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };

    esp_err_t ret = spi_bus_initialize(SPI2_HOST, &bus_config, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) return ret;

    spi_device_interface_config_t dev_config = {
        .clock_speed_hz = 4000000,
        .mode = 0,
        .spics_io_num = OLED_CS_PIN,
        .queue_size = 8,
        .flags = SPI_DEVICE_HALFDUPLEX,
    };

    ret = spi_bus_add_device(SPI2_HOST, &dev_config, &spi_handle);
    if (ret != ESP_OK) return ret;

    gpio_set_direction(OLED_DC_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(OLED_RES_PIN, GPIO_MODE_OUTPUT);

    return ESP_OK;
}

esp_err_t oled_init(void)
{
    ESP_LOGI(TAG, "Initializing OLED (SPI mode)...");

    s_oled_mutex = xSemaphoreCreateMutex();
    if (s_oled_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create OLED mutex");
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = oled_spi_init();
    if (ret != ESP_OK) return ret;

    gpio_set_level(OLED_RES_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(OLED_RES_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(100));

    oled_write_cmd(0xAE);
    oled_write_cmd(0xD5);
    oled_write_cmd(0x80);
    oled_write_cmd(0xA8);
    oled_write_cmd(0x5F);
    oled_write_cmd(0xD3);
    oled_write_cmd(0x00);
    oled_write_cmd(0x40);
    oled_write_cmd(0x8D);
    oled_write_cmd(0x14);
    oled_write_cmd(0x20);
    oled_write_cmd(0x02);
    oled_write_cmd(0xA0);
    oled_write_cmd(0xC8);
    oled_write_cmd(0xDA);
    oled_write_cmd(0x12);
    oled_write_cmd(0x81);
    oled_write_cmd(0xFF);
    oled_write_cmd(0xD9);
    oled_write_cmd(0xF1);
    oled_write_cmd(0xDB);
    oled_write_cmd(0x18);
    oled_write_cmd(0xA4);
    oled_write_cmd(0xA6);
    oled_write_cmd(0x2E);
    oled_write_cmd(0xAF);

    /* 清空帧缓冲区并刷到屏幕 */
    memset(s_framebuf, 0, sizeof(s_framebuf));
    oled_flush();

    ESP_LOGI(TAG, "OLED initialized successfully!");
    return ESP_OK;
}

void oled_clear(void)
{
    xSemaphoreTake(s_oled_mutex, portMAX_DELAY);
    memset(s_framebuf, 0, sizeof(s_framebuf));
    oled_flush_internal();
    xSemaphoreGive(s_oled_mutex);
}

void oled_fill(void)
{
    xSemaphoreTake(s_oled_mutex, portMAX_DELAY);
    memset(s_framebuf, 0xFF, sizeof(s_framebuf));
    oled_flush_internal();
    xSemaphoreGive(s_oled_mutex);
}

void oled_flush(void)
{
    xSemaphoreTake(s_oled_mutex, portMAX_DELAY);
    oled_flush_internal();
    xSemaphoreGive(s_oled_mutex);
}

/* 内部刷新，调用前必须已持有 mutex */
static void oled_flush_internal(void)
{
    for (int page = 0; page < OLED_PAGES; page++) {
        oled_set_page_col(page, 0);
        oled_write_data_bulk(s_framebuf[page], OLED_WIDTH);
    }
}

void oled_show_char(char c, int x, int y)
{
    if (x >= OLED_WIDTH - 5 || y >= OLED_HEIGHT - 7) return;

    int page = y / 8;
    int col = x;
    int idx = c - 0x20;
    if (idx < 0 || idx >= 95) idx = 0;

    /* 写5列像素到帧缓冲区 */
    for (int i = 0; i < 5; i++) {
        if (col + i < OLED_WIDTH) {
            s_framebuf[page][col + i] = font5x7[idx][i];
        }
    }
    /* 第6列为空白分隔 */
    if (col + 5 < OLED_WIDTH) {
        s_framebuf[page][col + 5] = 0x00;
    }
}

void oled_show_string(const char *str, int x, int y)
{
    while (*str) {
        oled_show_char(*str, x, y);
        x += 6;
        if (x > OLED_WIDTH - 6) break;
        str++;
    }
}

void oled_show_chinese_char(uint16_t gb2312_code, int x, int y)
{
    if (x > OLED_WIDTH - 16 || y > OLED_HEIGHT - 16) return;

    uint8_t font_data[32];
    esp_err_t err = hzk_get_char(gb2312_code, font_data);

    if (err != ESP_OK) {
        return;
    }

    int page_start = y / 8;
    for (int page = 0; page < 2; page++) {
        int p = page_start + page;
        if (p >= OLED_PAGES) break;
        for (int col = 0; col < 16; col++) {
            if (x + col < OLED_WIDTH) {
                s_framebuf[p][x + col] = font_data[page * 16 + col];
            }
        }
    }
}

void oled_show_chinese_string(const char *str, int x, int y)
{
    if (str == NULL) return;

    while (*str) {
        if (is_utf8_chinese((const uint8_t*)str)) {
            uint16_t gb_code = utf8_to_gb2312((const uint8_t*)str);
            if (gb_code != 0) {
                oled_show_chinese_char(gb_code, x, y);
            }
            str += 3;
            x += 16;
        } else {
            oled_show_char(*str, x, y + 4);
            str++;
            x += 8;
        }
    }
}

/* ===== 高级接口：缓冲区操作 ===== */

void oled_clear_buffer(void)
{
    memset(s_framebuf, 0, sizeof(s_framebuf));
}
