#include "audio_max98357.h"
#include "driver/i2s.h"

esp_err_t audio_init(void) {
    i2s_config_t config = {
        .mode = I2S_MODE_MASTER | I2S_MODE_TX,
        .sample_rate = 16000,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_I2S_MSB,
        .intr_alloc_flags = 0,
        .dma_buf_count = 4,
        .dma_buf_len = 64,
        .use_apll = false,
    };
    i2s_pin_config_t pin_config = {
        .bck_io_num = 26,
        .ws_io_num = 25,
        .data_out_num = 22,
        .data_in_num = -1,
    };
    ESP_ERROR_CHECK(i2s_driver_install(I2S_NUM_1, &config, 0, NULL));
    return i2s_set_pin(I2S_NUM_1, &pin_config);
}
