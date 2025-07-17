#include <esp_log.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "display_module.h"

static const char *TAG = "SmartAssistant";

void app_main(void)
{
    ESP_LOGI(TAG, "Smart Assistant starting...");
    
    // Initialize display system and show boot animation
    esp_err_t ret = display_init_and_show_boot_animation();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize display system: %s", esp_err_to_name(ret));
        ESP_LOGE(TAG, "System initialization failed, restarting in 5 seconds...");
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_restart();
    }
    
    // Simulate boot process with status updates and progress
    display_update_boot_status("Display initialized...", 10);
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Give LVGL time to update the display
    for (int i = 0; i < 5; i++) {
        display_task_handler();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    display_update_boot_status("Checking hardware...", 30);
    for (int i = 0; i < 15; i++) {
        display_task_handler();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    display_update_boot_status("Loading configuration...", 50);
    for (int i = 0; i < 10; i++) {
        display_task_handler();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    display_update_boot_status("Connecting to WiFi...", 70);
    for (int i = 0; i < 20; i++) {
        display_task_handler();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    display_update_boot_status("Starting services...", 90);
    for (int i = 0; i < 10; i++) {
        display_task_handler();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    display_update_boot_status("System ready!", 100);
    for (int i = 0; i < 10; i++) {
        display_task_handler();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    // Complete boot animation and show main screen
    display_complete_boot_animation();
    
    ESP_LOGI(TAG, "System initialized successfully");

    // Main application loop
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(10));
        display_task_handler();
    }
}