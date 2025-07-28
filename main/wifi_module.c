#include "wifi_module.h"
#include "project_config.h"
#include <string.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <nvs_flash.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

static const char *TAG = "WiFiModule";

// Event group bits
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

// Global variables
static EventGroupHandle_t s_wifi_event_group;
static wifi_info_t s_wifi_info = {0};
static int s_retry_num = 0;
static bool s_wifi_initialized = false;
static TaskHandle_t s_wifi_task_handle = NULL;
static bool s_connection_requested = false;
static uint32_t s_retry_delay_ms = CONFIG_WIFI_INITIAL_RETRY_MS;

// Event handler
static void event_handler(void* arg, esp_event_base_t event_base,
                         int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Wi-Fi started, attempting to connect to SSID: %s", CONFIG_WIFI_SSID);
        esp_wifi_connect();
        s_wifi_info.status = WIFI_STATUS_CONNECTING;
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t* disconnected = (wifi_event_sta_disconnected_t*) event_data;
        ESP_LOGW(TAG, "Wi-Fi disconnected, reason: %d", disconnected->reason);
        
        // Log common disconnection reasons
        switch(disconnected->reason) {
            case WIFI_REASON_NO_AP_FOUND:
                ESP_LOGW(TAG, "AP not found - check SSID");
                break;
            case WIFI_REASON_AUTH_EXPIRE:
                ESP_LOGW(TAG, "Authentication timeout - check password or signal strength");
                break;
            case WIFI_REASON_AUTH_FAIL:
                ESP_LOGW(TAG, "Authentication failed - check password");
                break;
            case WIFI_REASON_ASSOC_FAIL:
                ESP_LOGW(TAG, "Association failed");
                break;
            case WIFI_REASON_HANDSHAKE_TIMEOUT:
                ESP_LOGW(TAG, "4-way handshake timeout - check password");
                break;
            case 205:
                ESP_LOGW(TAG, "Auth timeout (205) - password or encryption mismatch");
                break;
            default:
                ESP_LOGW(TAG, "Other reason: %d", disconnected->reason);
                break;
        }
        
        if (s_retry_num < CONFIG_WIFI_MAXIMUM_RETRY) {
            // Adaptive retry delay: start fast, get slower
            s_retry_delay_ms = (s_retry_delay_ms * 3) / 2;  // 1.5x increase
            if (s_retry_delay_ms > CONFIG_WIFI_MAX_RETRY_MS) {
                s_retry_delay_ms = CONFIG_WIFI_MAX_RETRY_MS;
            }
            
            ESP_LOGI(TAG, "Will retry in %lu ms (%d/%d)", s_retry_delay_ms, s_retry_num + 1, CONFIG_WIFI_MAXIMUM_RETRY);
            s_connection_requested = true;  // Enable reconnection attempts
            // Don't block here, let the task handle the delay
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            s_wifi_info.status = WIFI_STATUS_FAILED;
            ESP_LOGE(TAG, "Failed to connect to AP after %d retries", CONFIG_WIFI_MAXIMUM_RETRY);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        snprintf(s_wifi_info.ip_address, sizeof(s_wifi_info.ip_address), 
                IPSTR, IP2STR(&event->ip_info.ip));
        
        // Get RSSI
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            s_wifi_info.rssi = ap_info.rssi;
        }
        
        s_wifi_info.status = WIFI_STATUS_CONNECTED;
        s_retry_num = 0;
        s_wifi_info.retry_count = 0;
        s_retry_delay_ms = CONFIG_WIFI_INITIAL_RETRY_MS;  // Reset retry delay
        s_connection_requested = false;  // Stop connection attempts
        
        ESP_LOGI(TAG, "Connected to AP, IP: %s, RSSI: %d dBm", 
                s_wifi_info.ip_address, s_wifi_info.rssi);
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// WiFi task function
static void wifi_task(void *pvParameters)
{
    ESP_LOGI(TAG, "WiFi task started");
    uint32_t rssi_update_counter = 0;
    
    while (1) {
        // Only attempt connection if requested and not already connected
        if (s_connection_requested && s_wifi_info.status != WIFI_STATUS_CONNECTED) {
            if (s_retry_num < CONFIG_WIFI_MAXIMUM_RETRY) {
                if (s_retry_num > 0) {
                    ESP_LOGI(TAG, "Retrying connection in %lu ms (%d/%d)", 
                            s_retry_delay_ms, s_retry_num + 1, CONFIG_WIFI_MAXIMUM_RETRY);
                    vTaskDelay(pdMS_TO_TICKS(s_retry_delay_ms));
                    
                    // Check again if still need to connect (might have connected during delay)
                    if (s_wifi_info.status == WIFI_STATUS_CONNECTED) {
                        ESP_LOGI(TAG, "Already connected during retry delay, stopping attempts");
                        s_connection_requested = false;
                        continue;
                    }
                }
                
                ESP_LOGI(TAG, "Attempting WiFi connection (%d/%d)", s_retry_num + 1, CONFIG_WIFI_MAXIMUM_RETRY);
                esp_err_t ret = esp_wifi_connect();
                if (ret == ESP_ERR_WIFI_CONN) {
                    // Already connected, stop attempts
                    ESP_LOGI(TAG, "Already connected, stopping connection attempts");
                    s_connection_requested = false;
                    if (s_wifi_info.status != WIFI_STATUS_CONNECTED) {
                        s_wifi_info.status = WIFI_STATUS_CONNECTED;
                    }
                } else if (ret != ESP_OK) {
                    ESP_LOGW(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(ret));
                    s_retry_num++;
                    s_wifi_info.retry_count = s_retry_num;
                    s_retry_delay_ms = (s_retry_delay_ms * 3) / 2;
                    if (s_retry_delay_ms > CONFIG_WIFI_MAX_RETRY_MS) {
                        s_retry_delay_ms = CONFIG_WIFI_MAX_RETRY_MS;
                    }
                } else {
                    s_retry_num++;
                    s_wifi_info.retry_count = s_retry_num;
                    s_wifi_info.status = WIFI_STATUS_CONNECTING;
                }
            } else {
                ESP_LOGW(TAG, "Maximum retries reached, stopping connection attempts");
                s_connection_requested = false;
                s_wifi_info.status = WIFI_STATUS_FAILED;
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            }
        }
        
        // Update RSSI every 5 seconds when connected
        rssi_update_counter++;
        if (rssi_update_counter >= 5 && s_wifi_info.status == WIFI_STATUS_CONNECTED) {
            rssi_update_counter = 0;
            wifi_ap_record_t ap_info;
            if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
                int8_t old_rssi = s_wifi_info.rssi;
                s_wifi_info.rssi = ap_info.rssi;
                
                // Log significant RSSI changes (>5dBm) or every 60 seconds
                static uint32_t last_rssi_log = 0;
                uint32_t current_time = xTaskGetTickCount() / configTICK_RATE_HZ;
                if (abs(s_wifi_info.rssi - old_rssi) > 5 || (current_time - last_rssi_log) > 60) {
                    ESP_LOGI(TAG, "WiFi signal: %d dBm (%s)", s_wifi_info.rssi,
                             s_wifi_info.rssi > -50 ? "Excellent" :
                             s_wifi_info.rssi > -70 ? "Good" :
                             s_wifi_info.rssi > -80 ? "Fair" : "Weak");
                    last_rssi_log = current_time;
                }
            }
        }
        
        // Check task every 1 second
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

esp_err_t wifi_module_init(void)
{
    if (s_wifi_initialized) {
        ESP_LOGW(TAG, "Wi-Fi module already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing Wi-Fi module...");

    // Initialize NVS only if needed
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs to be erased, reinitializing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS initialized successfully");

    // Initialize event group
    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create Wi-Fi event group");
        return ESP_FAIL;
    }

    // Initialize TCP/IP stack
    ESP_ERROR_CHECK(esp_netif_init());

    // Create default event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Create default Wi-Fi station interface
    esp_netif_create_default_wifi_sta();

    // Initialize Wi-Fi with default configuration
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    // Configure Wi-Fi for WPA2/WPA3 router
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_WIFI_SSID,
            .password = CONFIG_WIFI_PASSWORD,
            .threshold = {
                .authmode = WIFI_AUTH_WPA2_PSK  // WPA2 for the router
            },
            .pmf_cfg = {
                .capable = true,
                .required = false
            }
        }
    };
    
    ESP_LOGI(TAG, "Configured Wi-Fi: SSID='%s', Auth=%d", 
             wifi_config.sta.ssid, wifi_config.sta.threshold.authmode);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    // Initialize status
    s_wifi_info.status = WIFI_STATUS_DISCONNECTED;
    s_wifi_info.rssi = INT8_MIN;
    s_wifi_info.retry_count = 0;
    memset(s_wifi_info.ip_address, 0, sizeof(s_wifi_info.ip_address));

    // Create WiFi task
    BaseType_t task_ret = xTaskCreate(
        wifi_task,
        "wifi_task",
        CONFIG_WIFI_TASK_STACK_SIZE,
        NULL,
        CONFIG_WIFI_TASK_PRIORITY,
        &s_wifi_task_handle
    );
    
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create WiFi task");
        return ESP_FAIL;
    }

    s_wifi_initialized = true;
    ESP_LOGI(TAG, "Wi-Fi module initialized successfully with dedicated task");

    return ESP_OK;
}

esp_err_t wifi_module_connect(void)
{
    if (!s_wifi_initialized) {
        ESP_LOGE(TAG, "Wi-Fi module not initialized");
        return ESP_FAIL;
    }

    if (s_wifi_info.status == WIFI_STATUS_CONNECTED) {
        ESP_LOGW(TAG, "Wi-Fi already connected");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Starting non-blocking Wi-Fi connection...");
    
    // Reset retry parameters
    s_retry_num = 0;
    s_wifi_info.retry_count = 0;
    s_retry_delay_ms = CONFIG_WIFI_INITIAL_RETRY_MS;
    s_wifi_info.status = WIFI_STATUS_CONNECTING;
    s_connection_requested = true;

    // Start Wi-Fi
    esp_err_t ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Wi-Fi: %s", esp_err_to_name(ret));
        s_wifi_info.status = WIFI_STATUS_ERROR;
        s_connection_requested = false;
        return ret;
    }

    ESP_LOGI(TAG, "Wi-Fi connection request sent to background task");
    return ESP_OK;
}

esp_err_t wifi_module_disconnect(void)
{
    if (!s_wifi_initialized) {
        ESP_LOGE(TAG, "Wi-Fi module not initialized");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Disconnecting Wi-Fi...");
    
    // Stop connection attempts
    s_connection_requested = false;
    
    esp_err_t ret = esp_wifi_disconnect();
    if (ret == ESP_OK) {
        s_wifi_info.status = WIFI_STATUS_DISCONNECTED;
        s_wifi_info.rssi = INT8_MIN;
        s_wifi_info.retry_count = 0;
        s_retry_num = 0;
        s_retry_delay_ms = CONFIG_WIFI_INITIAL_RETRY_MS;
        memset(s_wifi_info.ip_address, 0, sizeof(s_wifi_info.ip_address));
    }

    return ret;
}

esp_err_t wifi_get_info(wifi_info_t *info)
{
    if (info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *info = s_wifi_info;
    return ESP_OK;
}

esp_err_t wifi_get_status_string(char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    switch (s_wifi_info.status) {
        case WIFI_STATUS_DISCONNECTED:
            snprintf(buffer, buffer_size, "WiFi: Off");
            break;
        case WIFI_STATUS_CONNECTING:
            snprintf(buffer, buffer_size, "WiFi: Connecting... (%d/%d)", 
                    s_wifi_info.retry_count, CONFIG_WIFI_MAXIMUM_RETRY);
            break;
        case WIFI_STATUS_CONNECTED:
            if (s_wifi_info.rssi > -50) {
                snprintf(buffer, buffer_size, "WiFi: Excellent (%d dBm)", s_wifi_info.rssi);
            } else if (s_wifi_info.rssi > -70) {
                snprintf(buffer, buffer_size, "WiFi: Good (%d dBm)", s_wifi_info.rssi);
            } else if (s_wifi_info.rssi > -80) {
                snprintf(buffer, buffer_size, "WiFi: Fair (%d dBm)", s_wifi_info.rssi);
            } else {
                snprintf(buffer, buffer_size, "WiFi: Weak (%d dBm)", s_wifi_info.rssi);
            }
            break;
        case WIFI_STATUS_FAILED:
            snprintf(buffer, buffer_size, "WiFi: Failed");
            break;
        case WIFI_STATUS_ERROR:
            snprintf(buffer, buffer_size, "WiFi: Error");
            break;
        default:
            snprintf(buffer, buffer_size, "WiFi: Unknown");
            break;
    }

    return ESP_OK;
}

bool wifi_is_connected(void)
{
    return s_wifi_info.status == WIFI_STATUS_CONNECTED;
}

int8_t wifi_get_rssi(void)
{
    return s_wifi_info.rssi;
}

esp_err_t wifi_module_reconnect(void)
{
    if (!s_wifi_initialized) {
        ESP_LOGE(TAG, "Wi-Fi module not initialized");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Forcing Wi-Fi reconnection...");
    
    // Reset retry parameters
    s_retry_num = 0;
    s_wifi_info.retry_count = 0;
    s_retry_delay_ms = CONFIG_WIFI_INITIAL_RETRY_MS;
    s_connection_requested = true;
    
    if (s_wifi_info.status == WIFI_STATUS_CONNECTED) {
        // Disconnect first if connected
        esp_wifi_disconnect();
    }
    
    s_wifi_info.status = WIFI_STATUS_CONNECTING;
    ESP_LOGI(TAG, "Wi-Fi reconnection initiated");
    
    return ESP_OK;
}

esp_err_t wifi_module_deinit(void)
{
    if (!s_wifi_initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Deinitializing Wi-Fi module...");

    // Stop connection attempts
    s_connection_requested = false;

    // Delete WiFi task
    if (s_wifi_task_handle) {
        vTaskDelete(s_wifi_task_handle);
        s_wifi_task_handle = NULL;
        ESP_LOGI(TAG, "WiFi task deleted");
    }

    // Stop Wi-Fi
    esp_wifi_stop();
    
    // Unregister event handlers
    esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler);
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler);

    // Deinitialize Wi-Fi
    esp_wifi_deinit();

    // Delete event group
    if (s_wifi_event_group) {
        vEventGroupDelete(s_wifi_event_group);
        s_wifi_event_group = NULL;
    }

    s_wifi_initialized = false;
    ESP_LOGI(TAG, "Wi-Fi module deinitialized");

    return ESP_OK;
}