#ifndef TIME_MODULE_H
#define TIME_MODULE_H

#include <esp_err.h>
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Time module status
 */
typedef enum {
    TIME_STATUS_OK,              // Time is valid and synced
    TIME_STATUS_RTC_ERROR,       // RTC hardware error
    TIME_STATUS_NOT_SET,         // Time not set yet
    TIME_STATUS_SYNC_FAILED      // Failed to sync time
} time_status_t;

/**
 * @brief Time information structure
 */
typedef struct {
    int year;        // Year (e.g. 2025)
    int month;       // Month (1-12)
    int day;         // Day (1-31)
    int hour;        // Hour (0-23)
    int minute;      // Minute (0-59)
    int second;      // Second (0-59)
    int weekday;     // Day of week (0=Sunday, 6=Saturday)
    time_status_t status;
} time_info_t;

/**
 * @brief Initialize the time module
 * 
 * This function initializes:
 * - DS3231 RTC module communication
 * - System time synchronization
 * - Time update timer
 * 
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t time_module_init(void);

/**
 * @brief Get current time information
 * 
 * @param time_info Pointer to time_info_t structure to fill
 * @return ESP_OK if time is valid, ESP_FAIL if error
 */
esp_err_t time_module_get_time(time_info_t *time_info);

/**
 * @brief Set the current time
 * 
 * @param year Year (e.g. 2025)
 * @param month Month (1-12)
 * @param day Day (1-31)
 * @param hour Hour (0-23)
 * @param minute Minute (0-59)
 * @param second Second (0-59)
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t time_module_set_time(int year, int month, int day, int hour, int minute, int second);

/**
 * @brief Get time status string for display
 * 
 * @return String describing current time status
 */
const char* time_module_get_status_string(void);

/**
 * @brief Start periodic time updates to display
 * 
 * This will automatically update the display every second
 * 
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t time_module_start_display_updates(void);

/**
 * @brief Stop periodic time updates
 * 
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t time_module_stop_display_updates(void);

/**
 * @brief Deinitialize time module and free resources
 * 
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t time_module_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // TIME_MODULE_H