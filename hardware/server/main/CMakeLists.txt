idf_component_register(SRCS "main.c" "emergency_button.c" "esp_now_indoor.c" "vibrator.c"
    PRIV_REQUIRES esp_driver_gpio esp_wifi nvs_flash esp_timer
    INCLUDE_DIRS "." REQUIRES "driver")
