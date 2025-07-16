#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_ili9488.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "display_config.h"

static const char *TAG = "SmartAssistant";

static esp_lcd_panel_handle_t panel_handle = NULL;
static esp_lcd_panel_io_handle_t io_handle = NULL;

static esp_err_t init_display(void)
{
    ESP_LOGI(TAG, "Initializing SPI bus");
    
    // Configure SPI bus
    spi_bus_config_t bus_config = {
        .sclk_io_num = TFT_SCLK_PIN,
        .mosi_io_num = TFT_MOSI_PIN,
        .miso_io_num = TFT_MISO_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t),
    };
    
    ESP_ERROR_CHECK(spi_bus_initialize(SPI_HOST_ID, &bus_config, SPI_DMA_CH_AUTO));
    
    ESP_LOGI(TAG, "Installing panel IO");
    
    // Configure panel IO
    esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = TFT_CS_PIN,
        .dc_gpio_num = TFT_DC_PIN,
        .spi_mode = 0,
        .pclk_hz = SPI_FREQ_MHZ * 1000 * 1000,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI_HOST_ID, &io_config, &io_handle));
    
    ESP_LOGI(TAG, "Installing ILI9488 panel driver");
    
    // Configure panel
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = TFT_RST_PIN,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        // SPI interface using RGB565
        .bits_per_pixel = 16,
    };
    
    // Calculate partial buffer size for RGB565 (16-bit) transfers
    // Use 1/16 of screen height for safer DMA memory allocation
    size_t buffer_size = DISPLAY_WIDTH * (DISPLAY_HEIGHT / 16) * 2;
    
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9488(io_handle, &panel_config, buffer_size, &panel_handle));
    
    ESP_LOGI(TAG, "Resetting panel");
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    
    ESP_LOGI(TAG, "Initializing panel");
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    
    ESP_LOGI(TAG, "Turning on display");
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));
    
    return ESP_OK;
}

static esp_err_t init_backlight(void)
{
    ESP_LOGI(TAG, "Initializing backlight");
    
    // Configure backlight GPIO
    gpio_config_t backlight_config = {
        .pin_bit_mask = (1ULL << TFT_BL_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    
    ESP_ERROR_CHECK(gpio_config(&backlight_config));
    
    // Turn on backlight
    ESP_ERROR_CHECK(gpio_set_level(TFT_BL_PIN, 1));
    
    return ESP_OK;
}

static void test_display_colors(void)
{
    ESP_LOGI(TAG, "Testing display with colors");
    
    // Create smaller color buffer using RGB565 format (2 bytes per pixel)
    size_t buffer_pixels = DISPLAY_WIDTH * DISPLAY_HEIGHT;
    uint16_t *color_buffer = malloc(buffer_pixels * sizeof(uint16_t));
    if (color_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate color buffer");
        return;
    }
    
    // Test colors (RGB565 format: 16-bit colors)
    uint16_t colors[] = {
        0xF800,    // Red
        0x07E0,    // Green
        0x001F,    // Blue
        0xFFE0,    // Yellow
        0xF81F,    // Magenta
        0x07FF,    // Cyan
        0xFFFF,    // White
        0x0000     // Black
    };
    
    for (int i = 0; i < 8; i++) {
        ESP_LOGI(TAG, "Displaying color %d", i);
        
        // Fill buffer with current color (RGB565 format)
        for (size_t j = 0; j < buffer_pixels; j++) {
            color_buffer[j] = colors[i];
        }
        
        // Draw to display
        esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, color_buffer);
        
        // Wait 1 second
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    free(color_buffer);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Smart Assistant Alpha V1 Starting...");
    
    // Initialize backlight
    ESP_ERROR_CHECK(init_backlight());
    
    // Initialize display
    ESP_ERROR_CHECK(init_display());
    
    ESP_LOGI(TAG, "Display initialized successfully");
    
    // Test display with colors
    test_display_colors();
    
    ESP_LOGI(TAG, "Smart Assistant initialization complete");
    
    // Main loop
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}