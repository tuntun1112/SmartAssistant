idf_component_register(
    SRCS
        "main.c"
        "wifi_connect.c"
        "camera_ov2640.c"
        "display_ili9488.c"
        "mpu6050.c"
        "microphone_inmp441.c"
        "audio_max98357.c"
        "rtc_ds3231.c"
        "pir_hcsr505.c"
    INCLUDE_DIRS "."
    REQUIRES esp32-camera driver
)
