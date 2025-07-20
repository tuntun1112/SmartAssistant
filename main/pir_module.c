#include "pir_module.h"
#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <string.h>
#include <stdio.h>

static const char *TAG = "PIRModule";

// PIR sensor GPIO pin (as defined in README.yaml)
// HC-SR505 PIR sensor output - GPIO_NUM_7
static const gpio_num_t PIR_GPIO_PIN = GPIO_NUM_7;

// PIR status
static pir_status_t pir_status = {
    .motion_detected = false,
    .last_motion_time = 0,
    .no_motion_duration = 0
};

// Task and timer handles
static TaskHandle_t pir_task_handle = NULL;
static bool pir_module_initialized = false;

/**
 * @brief Get current time in seconds since boot
 */
static uint32_t get_time_seconds(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000000);
}

/**
 * @brief PIR sensor monitoring task
 */
static void pir_monitoring_task(void *pvParameters)
{
    ESP_LOGI(TAG, "PIR monitoring task started");
    
    while (1) {
        // Read current PIR sensor state
        int gpio_level = gpio_get_level(PIR_GPIO_PIN);
        bool current_motion = (gpio_level == 1);
        uint32_t current_time = get_time_seconds();
        
        // Update PIR status
        if (current_motion) {
            if (!pir_status.motion_detected) {
                // Motion just detected
                ESP_LOGI(TAG, "Motion detected!");
                pir_status.motion_detected = true;
                pir_status.last_motion_time = current_time;
                pir_status.no_motion_duration = 0;
            }
        } else {
            if (pir_status.motion_detected) {
                // Motion just stopped
                ESP_LOGI(TAG, "Motion stopped");
                pir_status.motion_detected = false;
                pir_status.last_motion_time = current_time;
            }
            // Update no motion duration
            if (pir_status.last_motion_time > 0) {
                pir_status.no_motion_duration = current_time - pir_status.last_motion_time;
            }
        }
        
        // Check PIR sensor every 500ms
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

esp_err_t pir_module_init(void)
{
    ESP_LOGI(TAG, "Initializing PIR sensor module...");
    
    if (pir_module_initialized) {
        ESP_LOGW(TAG, "PIR module already initialized");
        return ESP_OK;
    }
    
    // Configure PIR GPIO pin as input
    gpio_config_t pir_config = {
        .pin_bit_mask = (1ULL << PIR_GPIO_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    
    esp_err_t ret = gpio_config(&pir_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure PIR GPIO: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Initialize PIR status
    pir_status.motion_detected = false;
    pir_status.last_motion_time = 0;
    pir_status.no_motion_duration = 0;
    
    // Create PIR monitoring task
    BaseType_t task_ret = xTaskCreate(
        pir_monitoring_task,
        "pir_monitor",
        2048,
        NULL,
        5,
        &pir_task_handle
    );
    
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create PIR monitoring task");
        return ESP_FAIL;
    }
    
    pir_module_initialized = true;
    ESP_LOGI(TAG, "PIR sensor module initialized successfully on GPIO %d", PIR_GPIO_PIN);
    
    return ESP_OK;
}

esp_err_t pir_get_status(pir_status_t *status)
{
    if (!pir_module_initialized || status == NULL) {
        return ESP_FAIL;
    }
    
    // Copy current status (thread-safe read)
    memcpy(status, &pir_status, sizeof(pir_status_t));
    return ESP_OK;
}

esp_err_t pir_get_status_string(char *buffer, size_t buffer_size)
{
    if (!pir_module_initialized || buffer == NULL || buffer_size < 32) {
        return ESP_FAIL;
    }
    
    if (pir_status.motion_detected) {
        snprintf(buffer, buffer_size, "PIR: Yes");
    } else {
        if (pir_status.no_motion_duration == 0) {
            snprintf(buffer, buffer_size, "PIR: No");
        } else {
            snprintf(buffer, buffer_size, "PIR: No (%lus ago)", (unsigned long)pir_status.no_motion_duration);
        }
    }
    
    return ESP_OK;
}

bool pir_is_motion_detected(void)
{
    return pir_module_initialized ? pir_status.motion_detected : false;
}

uint32_t pir_get_time_since_last_motion(void)
{
    return pir_module_initialized ? pir_status.no_motion_duration : 0;
}

esp_err_t pir_module_deinit(void)
{
    ESP_LOGI(TAG, "Deinitializing PIR sensor module...");
    
    if (!pir_module_initialized) {
        ESP_LOGW(TAG, "PIR module not initialized");
        return ESP_OK;
    }
    
    // Delete PIR monitoring task
    if (pir_task_handle != NULL) {
        vTaskDelete(pir_task_handle);
        pir_task_handle = NULL;
    }
    
    // Reset GPIO pin
    gpio_reset_pin(PIR_GPIO_PIN);
    
    pir_module_initialized = false;
    ESP_LOGI(TAG, "PIR sensor module deinitialized");
    
    return ESP_OK;
}
