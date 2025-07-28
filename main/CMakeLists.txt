idf_component_register(SRCS "wifi_module.c" "display_module.c" 
                           "main.c"
                           "time_module.c"
                           "pir_module.c"
                           "mpu6050_module.c"
                           "fonts/chinese_font_16.c"
                    INCLUDE_DIRS "."
                    REQUIRES espressif__mpu6050 esp_wifi esp_netif esp_event nvs_flash)