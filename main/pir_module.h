#ifndef PIR_MODULE_H
#define PIR_MODULE_H

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief PIR sensor status
 */
typedef struct {
    bool motion_detected;           // Current PIR signal state
    uint32_t last_motion_time;      // Last time motion was detected (in seconds since boot)
    uint32_t no_motion_duration;   // Duration since last motion (in seconds)
} pir_status_t;

/**
 * @brief Initialize PIR sensor module
 * 
 * This function initializes the PIR sensor GPIO and sets up interrupt handling
 * 
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t pir_module_init(void);

/**
 * @brief Get current PIR sensor status
 * 
 * @param status Pointer to store the current PIR status
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t pir_get_status(pir_status_t *status);

/**
 * @brief Get formatted PIR status string for display
 * 
 * @param buffer Buffer to store the formatted string (should be at least 32 bytes)
 * @param buffer_size Size of the buffer
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t pir_get_status_string(char *buffer, size_t buffer_size);

/**
 * @brief Check if motion is currently detected
 * 
 * @return true if motion is detected, false otherwise
 */
bool pir_is_motion_detected(void);

/**
 * @brief Get time since last motion in seconds
 * 
 * @return Time in seconds since last motion was detected
 */
uint32_t pir_get_time_since_last_motion(void);

/**
 * @brief Deinitialize PIR sensor module
 * 
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t pir_module_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // PIR_MODULE_H
