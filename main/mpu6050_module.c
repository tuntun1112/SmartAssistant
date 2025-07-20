#include "mpu6050_module.h"
#include "mpu6050.h"
#include <driver/i2c.h>
#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

static const char *TAG = "MPU6050Module";

// I2C configuration (shared with DS3231)
static const i2c_port_t I2C_PORT = I2C_NUM_0;

// Motion detection thresholds - USER CUSTOMIZED
static const float TAP_Z_THRESHOLD = 0.4f;      // Z-axis threshold for tap (user requested)
static const float SHAKE_THRESHOLD = 0.18f;     // g-force threshold for shake activity
static const uint32_t TAP_DEBOUNCE_MS = 180;    // Tap response time
static const uint32_t TAP_DISPLAY_MS = 800;     // Tap display duration (0.8 seconds)
static const uint32_t SHAKE_MIN_DURATION_MS = 500; // Shake confirmation time
static const uint32_t SHAKE_DISPLAY_MS = 800;   // Shake minimum display duration (0.8 seconds)
static const uint32_t SHAKE_TIMEOUT_MS = 500;   // Longer reset time for shake

// Module state
static mpu6050_handle_t mpu6050_handle = NULL;
static motion_status_t motion_status = {0};
static TaskHandle_t motion_task_handle = NULL;
static bool module_initialized = false;

// STATE MACHINE VARIABLES - Clean separation of concerns
static uint32_t last_shake_activity_time = 0;  // Last time shake activity detected
static uint32_t last_tap_time = 0;              // Last tap event time
static uint32_t tap_display_start = 0;          // When tap was first detected (for display timer)
static uint32_t shake_start_time = 0;           // When continuous shake started
static uint32_t shake_display_start = 0;        // When shake was confirmed (for display timer)
static bool is_shaking = false;                 // STATE: Are we currently in shake mode?
static float baseline_magnitude = 1.0f;         // Baseline acceleration magnitude (gravity)

/**
 * @brief Get current time in milliseconds since boot
 */
static uint32_t get_time_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/**
 * @brief Get current time in seconds since boot
 */
static uint32_t get_time_seconds(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000000);
}

/**
 * @brief Calculate acceleration magnitude
 */
static float calculate_magnitude(float x, float y, float z)
{
    return sqrtf(x * x + y * y + z * z);
}

/**
 * @brief Detect shake activity - more sensitive horizontal shake detection
 * STATE MACHINE: Detects X/Y axis movement for flat-mounted sensor
 */
static bool detect_shake_activity(float ax, float ay, float az)
{
    // For flat-mounted sensor, focus on X/Y axis movements (horizontal shake)
    float xy_magnitude = sqrtf(ax * ax + ay * ay);
    
    // Also check overall magnitude deviation
    float total_magnitude = calculate_magnitude(ax, ay, az);
    float deviation = fabsf(total_magnitude - baseline_magnitude);
    
    // Detect shake if either:
    // 1. Strong X/Y movement (horizontal shaking)
    // 2. Overall movement above threshold
    return (xy_magnitude > SHAKE_THRESHOLD) || (deviation > SHAKE_THRESHOLD);
}

/**
 * @brief Detect tap gesture - Z-axis focused for flat-mounted sensor
 * STATE MACHINE: Detects vertical tapping motion (up/down on flat surface)
 */
static bool detect_tap(float ax, float ay, float az)
{
    uint32_t current_time = get_time_ms();
    
    // Don't detect tap if currently shaking
    if (is_shaking) {
        return false;
    }
    
    // Z-axis tap detection for flat-mounted sensor
    // Look for sudden changes in Z-axis (vertical tapping)
    float z_deviation = fabsf(az - baseline_magnitude);
    
    // Very sensitive Z-axis tap detection
    if (z_deviation > TAP_Z_THRESHOLD) {
        if (current_time - last_tap_time > TAP_DEBOUNCE_MS) {
            last_tap_time = current_time;
            return true;
        }
    }
    
    return false;
}

// Tilt detection removed - not suitable for flat-mounted sensor

/**
 * @brief Motion detection task
 */
static void motion_detection_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Motion detection task started");
    
    while (1) {
        if (mpu6050_handle) {
            mpu6050_acce_value_t accel_data;
            esp_err_t ret = mpu6050_get_acce(mpu6050_handle, &accel_data);
            
            if (ret == ESP_OK) {
                uint32_t current_time = get_time_ms();
                
                // === STATE MACHINE IMPLEMENTATION ===
                
                // 1. Check for instantaneous activities
                bool shake_activity = detect_shake_activity(accel_data.acce_x, accel_data.acce_y, accel_data.acce_z);
                bool tap_event = detect_tap(accel_data.acce_x, accel_data.acce_y, accel_data.acce_z);
                
                // 2. SHAKE STATE MANAGEMENT
                if (shake_activity) {
                    // Start shake timer if not already started
                    if (shake_start_time == 0) {
                        shake_start_time = current_time;
                    }
                    last_shake_activity_time = current_time;
                    
                    // Confirm SHAKE if continuous activity for required duration
                    if (!is_shaking && (current_time - shake_start_time >= SHAKE_MIN_DURATION_MS)) {
                        is_shaking = true;
                        motion_status.shake_detected = true;
                        motion_status.tap_detected = false; // Shake overrides tap
                        motion_status.last_motion_time = get_time_seconds();
                        shake_display_start = current_time; // Start display timer
                        ESP_LOGI(TAG, "SHAKE confirmed!");
                    }
                } else {
                    // No shake activity - check for timeout
                    if (shake_start_time > 0 && (current_time - last_shake_activity_time > SHAKE_TIMEOUT_MS)) {
                        // Reset shake detection state
                        shake_start_time = 0;
                        is_shaking = false;
                        ESP_LOGI(TAG, "SHAKE activity ended");
                        
                        // BUT keep displaying for minimum time if not elapsed
                        if (shake_display_start > 0 && (current_time - shake_display_start < SHAKE_DISPLAY_MS)) {
                            // Keep showing SHAKE until minimum display time
                            motion_status.shake_detected = true;
                        } else {
                            // Clear display after minimum time
                            motion_status.shake_detected = false;
                            shake_display_start = 0;
                        }
                    }
                }
                
                // 5. SHAKE DISPLAY TIMEOUT (minimum 0.8 seconds)
                if (motion_status.shake_detected && shake_display_start > 0 && !is_shaking) {
                    if (current_time - shake_display_start >= SHAKE_DISPLAY_MS) {
                        motion_status.shake_detected = false;
                        shake_display_start = 0;
                        ESP_LOGI(TAG, "SHAKE display timeout");
                    }
                }
                
                // 3. TAP EVENT HANDLING
                if (tap_event) {
                    motion_status.tap_detected = true;
                    motion_status.last_motion_time = get_time_seconds();
                    tap_display_start = current_time; // Start 0.8s display timer
                    ESP_LOGI(TAG, "TAP detected!");
                }
                
                // 4. TAP DISPLAY TIMEOUT (exactly 0.8 seconds)
                if (motion_status.tap_detected && tap_display_start > 0) {
                    if (current_time - tap_display_start >= TAP_DISPLAY_MS) {
                        motion_status.tap_detected = false;
                        tap_display_start = 0;
                        ESP_LOGI(TAG, "TAP display timeout");
                    }
                }
            }
        }
        
        // Check sensor every 50ms (20Hz)
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

esp_err_t mpu6050_module_init(void)
{
    ESP_LOGI(TAG, "Initializing MPU6050 module...");
    
    if (module_initialized) {
        ESP_LOGW(TAG, "MPU6050 module already initialized");
        return ESP_OK;
    }
    
    // Skip I2C initialization - assume it's already initialized by time_module
    ESP_LOGI(TAG, "Using existing I2C bus (shared with DS3231)");
    
    // Create MPU6050 device handle (use address 0x69 to avoid conflict with DS3231)
    mpu6050_handle = mpu6050_create(I2C_PORT, MPU6050_I2C_ADDRESS_1);
    if (mpu6050_handle == NULL) {
        ESP_LOGE(TAG, "Failed to create MPU6050 device handle");
        return ESP_FAIL;
    }
    
    // Wake up MPU6050
    esp_err_t ret = mpu6050_wake_up(mpu6050_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to wake up MPU6050: %s", esp_err_to_name(ret));
        mpu6050_delete(mpu6050_handle);
        mpu6050_handle = NULL;
        return ret;
    }
    
    // Configure accelerometer and gyroscope ranges
    ret = mpu6050_config(mpu6050_handle, ACCE_FS_4G, GYRO_FS_500DPS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure MPU6050: %s", esp_err_to_name(ret));
        mpu6050_delete(mpu6050_handle);
        mpu6050_handle = NULL;
        return ret;
    }
    
    // Initialize motion status
    memset(&motion_status, 0, sizeof(motion_status_t));
    
    // Create motion detection task
    BaseType_t task_ret = xTaskCreate(
        motion_detection_task,
        "mpu6050_motion",
        4096,
        NULL,
        5,
        &motion_task_handle
    );
    
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create motion detection task");
        mpu6050_delete(mpu6050_handle);
        mpu6050_handle = NULL;
        return ESP_FAIL;
    }
    
    module_initialized = true;
    ESP_LOGI(TAG, "MPU6050 module initialized successfully");
    
    return ESP_OK;
}

esp_err_t mpu6050_get_motion_status(motion_status_t *motion)
{
    if (!module_initialized || motion == NULL) {
        return ESP_FAIL;
    }
    
    // Copy current status (thread-safe read)
    memcpy(motion, &motion_status, sizeof(motion_status_t));
    return ESP_OK;
}

esp_err_t mpu6050_get_status_string(char *buffer, size_t buffer_size)
{
    if (!module_initialized || buffer == NULL || buffer_size < 32) {
        snprintf(buffer, buffer_size, "MPU: Error");
        return ESP_FAIL;
    }
    
    if (!mpu6050_handle) {
        snprintf(buffer, buffer_size, "MPU: Offline");
        return ESP_OK;
    }
    
    if (motion_status.tap_detected) {
        snprintf(buffer, buffer_size, "MPU: Tap");
    } else if (motion_status.shake_detected) {
        snprintf(buffer, buffer_size, "MPU: Shake");
    } else {
        snprintf(buffer, buffer_size, "MPU: Ready");
    }
    
    return ESP_OK;
}

bool mpu6050_is_shake_detected(void)
{
    return module_initialized ? motion_status.shake_detected : false;
}

bool mpu6050_is_tap_detected(void)
{
    return module_initialized ? motion_status.tap_detected : false;
}

// Tilt detection function removed

esp_err_t mpu6050_module_deinit(void)
{
    ESP_LOGI(TAG, "Deinitializing MPU6050 module...");
    
    if (!module_initialized) {
        ESP_LOGW(TAG, "MPU6050 module not initialized");
        return ESP_OK;
    }
    
    // Delete motion detection task
    if (motion_task_handle != NULL) {
        vTaskDelete(motion_task_handle);
        motion_task_handle = NULL;
    }
    
    // Delete MPU6050 device
    if (mpu6050_handle != NULL) {
        mpu6050_delete(mpu6050_handle);
        mpu6050_handle = NULL;
    }
    
    // Don't delete I2C driver - shared with time_module
    
    module_initialized = false;
    ESP_LOGI(TAG, "MPU6050 module deinitialized");
    
    return ESP_OK;
}