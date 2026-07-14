idf_component_register(
    SRCS "audio_ai.cpp" "model_data.c" "pir.c" "mic.c" "event_fusion.c" "door_button.c" "main.c" "wifi.c" "mqtt.c" "esp_now_outdoor.c" "light.c" "command_recognition.c" "oled.c" "font_ascii.c" "font_chinese.c" "utf8_to_gb2312.c" "buzzer.c"
    PRIV_REQUIRES driver esp_driver_gpio esp_driver_rmt esp_timer esp-tflite-micro esp-dsp esp_wifi mqtt nvs_flash esp_netif mbedtls spi_flash
    INCLUDE_DIRS "."
)