idf_component_register(SRCS "led_control_gatt.c"
                       PRIV_REQUIRES bt nvs_flash esp_driver_gpio
                       INCLUDE_DIRS ".")
