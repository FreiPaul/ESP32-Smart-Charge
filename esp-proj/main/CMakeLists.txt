idf_component_register(SRCS "charger_main.c"
                            "charger_gatt.c"
                            "charger_fsm.c"
                            "charger_time.c"
                            "charger_hardware.c"
                       PRIV_REQUIRES bt nvs_flash esp_driver_gpio esp_adc
                       INCLUDE_DIRS ".")
