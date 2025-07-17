#ifndef DISPLAY_MODULE_H
#define DISPLAY_MODULE_H

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the display system and show boot animation
 * 
 * This function initializes the entire display system including:
 * - SPI bus initialization
 * - ILI9488 display panel initialization
 * - LVGL graphics library initialization
 * - Boot animation creation and display
 * 
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t display_init_and_show_boot_animation(void);

/**
 * @brief Set display brightness
 * 
 * @param brightness_percentage Brightness level (0-100)
 */
void display_set_brightness(int brightness_percentage);

/**
 * @brief Update boot animation status text
 * 
 * @param status_text The status text to display below the spinner
 */
void display_update_boot_status(const char* status_text);

/**
 * @brief Complete boot animation and transition to main screen
 * 
 * This function stops the boot animation and transitions to the main screen
 * with a fade-in effect
 */
void display_complete_boot_animation(void);

/**
 * @brief Main display task handler
 * 
 * This function should be called in the main loop to handle LVGL updates
 */
void display_task_handler(void);

/**
 * @brief Deinitialize the display system and free resources
 * 
 * This function cleans up all resources allocated by the display system:
 * - Frees LVGL buffer memory
 * - Deletes ESP timer
 * - Deletes LCD panel and I/O handles
 * - Frees SPI bus
 * 
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t display_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // DISPLAY_MODULE_H