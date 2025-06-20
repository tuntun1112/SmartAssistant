#include "pir_hcsr505.h"
#include "driver/gpio.h"

#define PIR_PIN 4

esp_err_t pir_init(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << PIR_PIN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&io_conf);
}
