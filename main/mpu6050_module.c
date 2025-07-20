#include "mpu6050_module.h"
#include "project_config.h"
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
 * @brief Detect shake activity - FIXED for tilt resistance
 * STATE MACHINE: Detects CHANGES in acceleration, not absolute values
 */
static bool detect_shake_activity(float ax, float ay, float az)
{
    static float prev_ax = 0, prev_ay = 0, prev_az = 0;
    static bool first_run = true;
    
    if (first_run) {
        prev_ax = ax;
        prev_ay = ay; 
        prev_az = az;
        first_run = false;
        return false;
    }
    
    // Calculate CHANGE in acceleration (not absolute values)
    float delta_x = fabsf(ax - prev_ax);
    float delta_y = fabsf(ay - prev_ay);
    float delta_z = fabsf(az - prev_az);
    
    // Update previous values
    prev_ax = ax;
    prev_ay = ay;
    prev_az = az;
    
    // Detect shake based on acceleration CHANGES
    float total_change = sqrtf(delta_x * delta_x + delta_y * delta_y + delta_z * delta_z);
    
    return total_change > CONFIG_MPU6050_SHAKE_THRESHOLD;
}

/**
 * @brief Detect tap gesture - FIXED for tilt resistance  
 * STATE MACHINE: Detects sudden Z-axis CHANGES, not absolute values
 */
static bool detect_tap(float ax, float ay, float az)
{
    static float prev_tap_z = 1.0f;
    static bool tap_first_run = true;
    uint32_t current_time = get_time_ms();
    
    // Don't detect tap if currently shaking
    if (is_shaking) {
        return false;
    }
    
    if (tap_first_run) {
        prev_tap_z = az;
        tap_first_run = false;
        return false;
    }
    
    // Detect CHANGE in Z-axis (not absolute value vs baseline)
    float z_change = fabsf(az - prev_tap_z);
    prev_tap_z = az;
    
    // Very sensitive Z-axis change detection
    if (z_change > CONFIG_MPU6050_TAP_Z_THRESHOLD) {
        if (current_time - last_tap_time > CONFIG_MPU6050_TAP_DEBOUNCE_MS) {
            last_tap_time = current_time;
            return true;
        }
    }
    
    return false;
}


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
                    if (!is_shaking && (current_time - shake_start_time >= CONFIG_MPU6050_SHAKE_MIN_DURATION_MS)) {
                        is_shaking = true;
                        motion_status.shake_detected = true;
                        motion_status.tap_detected = false; // Shake overrides tap
                        motion_status.last_motion_time = get_time_seconds();
                        shake_display_start = current_time; // Start display timer
                        ESP_LOGD(TAG, "Shake motion confirmed");
                    }
                } else {
                    // No shake activity - check for timeout
                    if (shake_start_time > 0 && (current_time - last_shake_activity_time > CONFIG_MPU6050_SHAKE_TIMEOUT_MS)) {
                        // Reset shake detection state
                        shake_start_time = 0;
                        is_shaking = false;
                        ESP_LOGD(TAG, "Shake activity timeout");
                        
                        // BUT keep displaying for minimum time if not elapsed
                        if (shake_display_start > 0 && (current_time - shake_display_start < CONFIG_MPU6050_SHAKE_DISPLAY_MS)) {
                            // Keep showing SHAKE until minimum display time
                            motion_status.shake_detected = true;
                        } else {
                            // Clear display after minimum time
                            motion_status.shake_detected = false;
                            shake_display_start = 0;
                        }
                    }
                }
                
                // 5. SHAKE DISPLAY TIMEOUT (minimum configured seconds)
                if (motion_status.shake_detected && shake_display_start > 0 && !is_shaking) {
                    if (current_time - shake_display_start >= CONFIG_MPU6050_SHAKE_DISPLAY_MS) {
                        motion_status.shake_detected = false;
                        shake_display_start = 0;
                        ESP_LOGD(TAG, "Shake display timeout");
                    }
                }
                
                // 3. TAP EVENT HANDLING
                if (tap_event) {
                    motion_status.tap_detected = true;
                    motion_status.last_motion_time = get_time_seconds();
                    tap_display_start = current_time; // Start 0.8s display timer
                    ESP_LOGD(TAG, "Tap motion detected");
                }
                
                // 4. TAP DISPLAY TIMEOUT (configured duration)
                if (motion_status.tap_detected && tap_display_start > 0) {
                    if (current_time - tap_display_start >= CONFIG_MPU6050_TAP_DISPLAY_MS) {
                        motion_status.tap_detected = false;
                        tap_display_start = 0;
                        ESP_LOGD(TAG, "Tap display timeout");
                    }
                }
            }
        }
        
        // Check sensor at configured interval
        vTaskDelay(pdMS_TO_TICKS(CONFIG_MPU6050_POLL_INTERVAL_MS));
    }
}

esp_err_t mpu6050_module_init(void)
{
    ESP_LOGI(TAG, "Initializing MPU6050 module...");
    
    if (module_initialized) {
        ESP_LOGW(TAG, "MPU6050 module already initialized");
        return ESP_OK;
    }
    
    // Initialize dedicated I2C bus for MPU6050
    ESP_LOGI(TAG, "Initializing dedicated I2C bus for MPU6050 on pins SDA:%d, SCL:%d", CONFIG_I2C1_SDA_GPIO, CONFIG_I2C1_SCL_GPIO);
    
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = CONFIG_I2C1_SDA_GPIO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = CONFIG_I2C1_SCL_GPIO,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = CONFIG_I2C1_FREQ_HZ,
    };
    
    esp_err_t ret = i2c_param_config(CONFIG_I2C1_PORT, &conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure I2C for MPU6050: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = i2c_driver_install(CONFIG_I2C1_PORT, conf.mode, 0, 0, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install I2C driver for MPU6050: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Create MPU6050 device handle using the configurable I2C address
    mpu6050_handle = mpu6050_create(CONFIG_I2C1_PORT, CONFIG_MPU6050_I2C_ADDR);
    if (mpu6050_handle == NULL) {
        ESP_LOGE(TAG, "Failed to create MPU6050 device handle");
        return ESP_FAIL;
    }
    
    // Wake up MPU6050
    ret = mpu6050_wake_up(mpu6050_handle);
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
        CONFIG_TASK_STACK_MPU6050,
        NULL,
        CONFIG_TASK_PRIORITY_MPU6050,
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
    
    // Delete dedicated I2C driver for MPU6050
    i2c_driver_delete(CONFIG_I2C1_PORT);
    
    module_initialized = false;
    ESP_LOGI(TAG, "MPU6050 module deinitialized");
    
    return ESP_OK;
}