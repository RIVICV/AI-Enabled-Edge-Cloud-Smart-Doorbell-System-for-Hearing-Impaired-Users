#include "pir.h"
#include <stdbool.h>

esp_err_t pir_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIR_GPIO_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    return gpio_config(&io_conf);
}

bool pir_is_detected(void)
{
    return gpio_get_level(PIR_GPIO_PIN) == 1;
}
