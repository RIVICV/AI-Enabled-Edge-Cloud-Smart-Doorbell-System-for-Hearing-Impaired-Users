#include "font_chinese.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_partition.h"
#include <string.h>
#include <stdint.h>

static const char *TAG = "HZK";
static const esp_partition_t *hzk_partition = NULL;

esp_err_t hzk_init(void)
{
    hzk_partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_ANY,
        "hzk"
    );
    
    if (hzk_partition == NULL) {
        ESP_LOGE(TAG, "HZK partition not found!");
        return ESP_ERR_NOT_FOUND;
    }
    
    ESP_LOGI(TAG, "HZK partition found!");
    ESP_LOGI(TAG, "  Address: 0x%08X", hzk_partition->address);
    ESP_LOGI(TAG, "  Size: %d bytes (%d KB)", 
             hzk_partition->size, hzk_partition->size / 1024);
    
    return ESP_OK;
}

esp_err_t hzk_get_char(uint16_t gb2312_code, uint8_t *out_data)
{
    if (hzk_partition == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (out_data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    uint8_t zone = (gb2312_code >> 8) & 0xFF;
    uint8_t pos = gb2312_code & 0xFF;
    
    if (zone < 0xA1 || zone > 0xF7 || pos < 0xA1 || pos > 0xFE) {
        ESP_LOGW(TAG, "Invalid GB2312 code: 0x%04X", gb2312_code);
        memset(out_data, 0, 32);
        return ESP_ERR_INVALID_ARG;
    }
    
    uint8_t zone_idx = zone - 0xA1;
    uint8_t pos_idx = pos - 0xA1;
    
    uint32_t offset = (zone_idx * 94 + pos_idx) * 32;
    
    if (offset + 32 > hzk_partition->size) {
        ESP_LOGW(TAG, "Offset 0x%08X exceeds partition size", offset);
        memset(out_data, 0, 32);
        return ESP_ERR_INVALID_SIZE;
    }
    
    esp_err_t err = esp_partition_read(hzk_partition, offset, out_data, 32);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read font at offset 0x%08X", offset);
        return err;
    }
    
    return ESP_OK;
}

int is_utf8_chinese(const uint8_t *str)
{
    if (str == NULL) return 0;
    return (str[0] & 0xF0) == 0xE0;
}