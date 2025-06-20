// for wifi
#include "wifi_connect.h"

#include "camera_ov2640.h"
#include "display_ili9488.h"
#include "mpu6050.h"
#include "microphone_inmp441.h"
#include "audio_max98357.h"
#include "rtc_ds3231.h"
#include "pir_hcsr505.h"

void app_main(void) {
    //=====================================(WIFI)
    wifi_connect("ESPTEST", "12345678"); // 連線 WiFi (SSID/PASSWORD)
    //=====================================

    camera_init();
    display_init();
    mpu6050_init();
    microphone_init();
    audio_init();
    rtc_init();
    pir_init();
}