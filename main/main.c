#include <esp_log.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "display_module.h"
#include "time_module.h"
#include "pir_module.h"

static const char *TAG = "SmartAssistant";

static esp_err_t run_boot_sequence(void)
{
    ESP_LOGI(TAG, "Starting boot sequence...");
    
    esp_err_t ret = display_init_and_show_boot_animation();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize display system: %s", esp_err_to_name(ret));
        return ret;
    }
    
    display_update_boot_status("Display initialized...", 10);
    vTaskDelay(pdMS_TO_TICKS(500));
    
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
    
    display_update_boot_status("Initializing time module...", 60);
    esp_err_t time_ret = time_module_init();
    if (time_ret != ESP_OK) {
        ESP_LOGW(TAG, "Time module initialization failed, continuing without RTC");
    }
    for (int i = 0; i < 10; i++) {
        display_task_handler();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    display_update_boot_status("Initializing PIR sensor...", 65);
    esp_err_t pir_ret = pir_module_init();
    if (pir_ret != ESP_OK) {
        ESP_LOGW(TAG, "PIR module initialization failed, continuing without PIR sensor");
    }
    for (int i = 0; i < 5; i++) {
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
    
    display_complete_boot_animation();
    
    // Start time display updates
    esp_err_t time_update_ret = time_module_start_display_updates();
    if (time_update_ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to start time display updates");
    }
    
    ESP_LOGI(TAG, "Boot sequence completed successfully");
    
    return ESP_OK;
}

static void run_main_screen(void)
{
    ESP_LOGI(TAG, "Starting main screen...");
    
    uint32_t pir_update_counter = 0;
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10));
        display_task_handler();
        
        // Update PIR status every 500ms (50 * 10ms)
        pir_update_counter++;
        if (pir_update_counter >= 50) {
            pir_update_counter = 0;
            
            char pir_status_str[32];
            if (pir_get_status_string(pir_status_str, sizeof(pir_status_str)) == ESP_OK) {
                display_update_pir_status(pir_status_str);
            }
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Smart Assistant starting...");
    
    if (run_boot_sequence() != ESP_OK) {
        ESP_LOGE(TAG, "System initialization failed, restarting in 5 seconds...");
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_restart();
    }
    
    run_main_screen();
}