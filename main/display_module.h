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
 * @param progress Boot progress percentage (0-100)
 */
void display_update_boot_status(const char* status_text, int progress);

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
 * @brief Update the time display on the main screen
 * 
 * @param hours Hours (0-23)
 * @param minutes Minutes (0-59) 
 * @param seconds Seconds (0-59)
 */
void display_update_time(int hours, int minutes, int seconds);

/**
 * @brief Update the date display on the main screen
 * 
 * @param year Year (e.g. 2025)
 * @param month Month (1-12)
 * @param day Day (1-31)
 */
void display_update_date(int year, int month, int day);

/**
 * @brief Show error message in time display area
 * 
 * @param error_message Error message to display
 */
void display_show_time_error(const char* error_message);

/**
 * @brief Update PIR sensor status display
 * 
 * @param pir_status_text PIR status text to display (e.g., "PIR: Yes" or "PIR: No (10s ago)")
 */
void display_update_pir_status(const char* pir_status_text);

/**
 * @brief Update Motion sensor status display
 * 
 * @param motion_status_text Motion status text to display (e.g., "Motion: Shake" or "Motion: None")
 */
void display_update_motion_status(const char* motion_status_text);

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