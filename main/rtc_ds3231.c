#include "rtc_ds3231.h"
#include "driver/i2c.h"

#define DS3231_ADDR 0x68

esp_err_t rtc_init(void) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = 21,
        .scl_io_num = 22,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_1, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM_1, conf.mode, 0, 0, 0));
    uint8_t data[1] = {0};
    return i2c_master_write_to_device(I2C_NUM_1, DS3231_ADDR, data, 1, 1000 / portTICK_PERIOD_MS);
}
