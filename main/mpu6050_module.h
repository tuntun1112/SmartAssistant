#ifndef MPU6050_MODULE_H
#define MPU6050_MODULE_H

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Motion detection status
 */
typedef struct {
    bool shake_detected;        // Shake gesture detected
    bool tap_detected;          // Tap gesture detected
    uint32_t last_motion_time;  // Last motion detection time (seconds since boot)
} motion_status_t;

/**
 * @brief Initialize MPU6050 sensor module
 * 
 * This function initializes the MPU6050 sensor using official espressif/mpu6050 driver
 * and sets up motion detection algorithms for shake and tap detection
 * 
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t mpu6050_module_init(void);

/**
 * @brief Get motion detection status
 * 
 * @param motion Pointer to store motion detection status
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t mpu6050_get_motion_status(motion_status_t *motion);

/**
 * @brief Get formatted motion status string for display
 * 
 * @param buffer Buffer to store the formatted string (should be at least 32 bytes)
 * @param buffer_size Size of the buffer
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t mpu6050_get_status_string(char *buffer, size_t buffer_size);

/**
 * @brief Check if shake gesture is detected
 * 
 * @return true if shake is detected, false otherwise
 */
bool mpu6050_is_shake_detected(void);

/**
 * @brief Check if tap gesture is detected
 * 
 * @return true if tap is detected, false otherwise
 */
bool mpu6050_is_tap_detected(void);

// Tilt detection removed - not suitable for flat-mounted sensor

/**
 * @brief Deinitialize MPU6050 sensor module
 * 
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t mpu6050_module_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // MPU6050_MODULE_H