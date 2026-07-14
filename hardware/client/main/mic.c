#include <string.h>
#include "mic.h"
#include "driver/i2s.h"
#include "freertos/FreeRTOS.h"
#include "hal/gpio_types.h"

#define MIC_I2S_PORT       I2S_NUM_0
#define MIC_BCLK_PIN       GPIO_NUM_17
#define MIC_WS_PIN         GPIO_NUM_18
#define MIC_DOUT_PIN       GPIO_NUM_16

#define MIC_SAMPLE_RATE    16000
#define MIC_DMA_BUF_COUNT  4
#define MIC_DMA_BUF_LEN    1024

static bool s_initialized = false;

esp_err_t mic_init(void)
{
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = MIC_SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = MIC_DMA_BUF_COUNT,
        .dma_buf_len = MIC_DMA_BUF_LEN,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0,
    };

    esp_err_t ret = i2s_driver_install(MIC_I2S_PORT, &i2s_config, 0, NULL);
    if (ret != ESP_OK) {
        return ret;
    }

    i2s_pin_config_t pin_config = {
        .bck_io_num   = MIC_BCLK_PIN,
        .ws_io_num    = MIC_WS_PIN,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num  = MIC_DOUT_PIN,
    };

    ret = i2s_set_pin(MIC_I2S_PORT, &pin_config);
    if (ret != ESP_OK) {
        return ret;
    }

    s_initialized = true;
    return ESP_OK;
}

int mic_read_samples(int16_t *buffer, int num_samples, TickType_t timeout)
{
    if (!s_initialized) {
        return 0;
    }

    size_t bytes_read = 0;
    size_t bytes_to_read = num_samples * sizeof(int16_t);

    esp_err_t ret = i2s_read(MIC_I2S_PORT, buffer, bytes_to_read, &bytes_read, timeout);
    if (ret == ESP_OK) {
        return (int)(bytes_read / sizeof(int16_t));
    }
    return 0;
}

void mic_flush(void)
{
    if (!s_initialized) {
        return;
    }
    int16_t temp[1024];
    size_t bytes_read = 0;
    int max_flush = 10; /* 最多刷新10次，避免无限循环 */
    while (max_flush-- > 0) {
        esp_err_t ret = i2s_read(MIC_I2S_PORT, temp, sizeof(temp), &bytes_read, 0);
        if (ret != ESP_OK || bytes_read == 0) {
            break;
        }
    }
}
