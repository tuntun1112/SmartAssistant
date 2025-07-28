#ifndef PROJECT_CONFIG_H
#define PROJECT_CONFIG_H

#include <driver/gpio.h>
#include <hal/i2c_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file project_config.h
 * @brief Centralized configuration for Smart Assistant project
 * 
 * This file contains all hardware-specific configurations, GPIO mappings,
 * I2C addresses, task priorities, and timing constants. It serves as the
 * single source of truth for system configuration.
 */

// =============================================================================
// GPIO Pin Mappings (as defined in README.yaml)
// =============================================================================

// Display (ILI9488) GPIO pins
#define CONFIG_DISPLAY_SPI_CLOCK        GPIO_NUM_11
#define CONFIG_DISPLAY_SPI_MOSI         GPIO_NUM_10
#define CONFIG_DISPLAY_SPI_MISO         GPIO_NUM_13
#define CONFIG_DISPLAY_TFT_CS           GPIO_NUM_3
#define CONFIG_DISPLAY_TFT_RESET        GPIO_NUM_46
#define CONFIG_DISPLAY_TFT_DC           GPIO_NUM_9
#define CONFIG_DISPLAY_TFT_BACKLIGHT    GPIO_NUM_12

// I2C Bus 0 Configuration (DS3231 RTC)
#define CONFIG_I2C0_SDA_GPIO            GPIO_NUM_8
#define CONFIG_I2C0_SCL_GPIO            GPIO_NUM_18
#define CONFIG_I2C0_PORT                I2C_NUM_0
#define CONFIG_I2C0_FREQ_HZ             100000

// I2C Bus 1 Configuration (MPU6050 Motion Sensor)
#define CONFIG_I2C1_SDA_GPIO            GPIO_NUM_5
#define CONFIG_I2C1_SCL_GPIO            GPIO_NUM_6
#define CONFIG_I2C1_PORT                I2C_NUM_1
#define CONFIG_I2C1_FREQ_HZ             400000  // MPU6050 supports faster speed

// Legacy compatibility (for existing code)
#define CONFIG_I2C_SDA_GPIO             CONFIG_I2C0_SDA_GPIO
#define CONFIG_I2C_SCL_GPIO             CONFIG_I2C0_SCL_GPIO
#define CONFIG_I2C_PORT                 CONFIG_I2C0_PORT
#define CONFIG_I2C_FREQ_HZ              CONFIG_I2C0_FREQ_HZ

// PIR Sensor Configuration
#define CONFIG_PIR_OUTPUT_GPIO          GPIO_NUM_7

// =============================================================================
// I2C Device Addresses
// =============================================================================

#define CONFIG_DS3231_I2C_ADDR          0x68
#define CONFIG_MPU6050_I2C_ADDR         0x69  // AD0 pin HIGH to avoid conflict with DS3231

// =============================================================================
// Task Priorities (Higher number = Higher priority)
// =============================================================================

#define CONFIG_TASK_PRIORITY_MPU6050    5   // Highest - motion detection is time-sensitive
#define CONFIG_TASK_PRIORITY_PIR        4   // Medium - presence detection
#define CONFIG_TASK_PRIORITY_DISPLAY    3   // Lower - UI updates can tolerate some delay

// =============================================================================
// Task Stack Sizes
// =============================================================================

#define CONFIG_TASK_STACK_MPU6050       4096
#define CONFIG_TASK_STACK_PIR           2048
#define CONFIG_TASK_STACK_DISPLAY       4096

// =============================================================================
// Motion Detection Configuration
// =============================================================================

// MPU6050 motion detection thresholds
#define CONFIG_MPU6050_TAP_Z_THRESHOLD      0.2f    // Z-axis threshold for tap detection
#define CONFIG_MPU6050_SHAKE_THRESHOLD      0.18f   // g-force threshold for shake
#define CONFIG_MPU6050_TAP_DEBOUNCE_MS      180     // Tap response time
#define CONFIG_MPU6050_TAP_DISPLAY_MS       800     // Tap display duration
#define CONFIG_MPU6050_SHAKE_MIN_DURATION_MS 500    // Shake confirmation time
#define CONFIG_MPU6050_SHAKE_DISPLAY_MS     800     // Shake display duration
#define CONFIG_MPU6050_SHAKE_TIMEOUT_MS     500     // Shake reset timeout

// Sensor polling intervals
#define CONFIG_MPU6050_POLL_INTERVAL_MS     50      // 20Hz update rate
#define CONFIG_PIR_POLL_INTERVAL_MS         500     // 2Hz update rate
#define CONFIG_MAIN_LOOP_INTERVAL_MS        10      // Main display update rate

// =============================================================================
// Display Configuration
// =============================================================================

#define CONFIG_DISPLAY_WIDTH            480
#define CONFIG_DISPLAY_HEIGHT           320
#define CONFIG_DISPLAY_REFRESH_HZ       40000000
#define CONFIG_DISPLAY_BUFFER_LINES     25
#define CONFIG_DISPLAY_DEFAULT_BRIGHTNESS 80       // Percentage (0-100)

// LVGL Configuration
#define CONFIG_LVGL_UPDATE_PERIOD_MS    5

// =============================================================================
// Wi-Fi Configuration
// =============================================================================

#define CONFIG_WIFI_SSID                "DIDI HOME 2.4G For智能居家"
#define CONFIG_WIFI_PASSWORD            "19411460"
#define CONFIG_WIFI_MAXIMUM_RETRY       5           // More retries with adaptive intervals
#define CONFIG_WIFI_CONNECT_TIMEOUT_MS  15000       // Reduced to 15 seconds
#define CONFIG_WIFI_TASK_STACK_SIZE     4096
#define CONFIG_WIFI_TASK_PRIORITY       2           // Lower than sensors but higher than display
#define CONFIG_WIFI_INITIAL_RETRY_MS    2000        // First retry after 2 seconds
#define CONFIG_WIFI_MAX_RETRY_MS        10000       // Max retry interval 10 seconds
#define CONFIG_WIFI_SCAN_TIMEOUT_MS     5000        // Quick scan timeout

// =============================================================================
// Time Module Configuration  
// =============================================================================

#define CONFIG_TIME_UPDATE_INTERVAL_MS  1000       // 1 second clock updates
#define CONFIG_TIME_I2C_TIMEOUT_MS      1000       // I2C transaction timeout

#ifdef __cplusplus
}
#endif

#endif // PROJECT_CONFIG_H