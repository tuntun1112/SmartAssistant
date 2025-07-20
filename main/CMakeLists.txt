idf_component_register(SRCS "display_module.c" 
                           "main.c"
                           "time_module.c"
                           "pir_module.c"
                           "mpu6050_module.c"
                           "fonts/chinese_font_16.c"
                    INCLUDE_DIRS "."
                    REQUIRES espressif__mpu6050)