#ifndef WIFI_MODULE_H
#define WIFI_MODULE_H

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Wi-Fi connection status
 */
typedef enum {
    WIFI_STATUS_DISCONNECTED,    // Not connected
    WIFI_STATUS_CONNECTING,      // Connection in progress
    WIFI_STATUS_CONNECTED,       // Connected successfully
    WIFI_STATUS_FAILED,          // Connection failed
    WIFI_STATUS_ERROR            // Wi-Fi error
} wifi_status_t;

/**
 * @brief Wi-Fi information structure
 */
typedef struct {
    wifi_status_t status;
    int8_t rssi;                 // Signal strength (dBm)
    uint8_t retry_count;         // Current retry count
    char ip_address[16];         // IP address string (e.g., "192.168.1.100")
} wifi_info_t;

/**
 * @brief Initialize Wi-Fi module
 * 
 * This function initializes the Wi-Fi stack and prepares for connection
 * 
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t wifi_module_init(void);

/**
 * @brief Start Wi-Fi connection
 * 
 * This function starts the Wi-Fi connection process in non-blocking mode
 * 
 * @return ESP_OK if connection started, ESP_FAIL on error
 */
esp_err_t wifi_module_connect(void);

/**
 * @brief Disconnect from Wi-Fi
 * 
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t wifi_module_disconnect(void);

/**
 * @brief Get current Wi-Fi information
 * 
 * @param info Pointer to store Wi-Fi information
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t wifi_get_info(wifi_info_t *info);

/**
 * @brief Get formatted Wi-Fi status string for display
 * 
 * @param buffer Buffer to store the formatted string (should be at least 32 bytes)
 * @param buffer_size Size of the buffer
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t wifi_get_status_string(char *buffer, size_t buffer_size);

/**
 * @brief Check if Wi-Fi is connected
 * 
 * @return true if connected, false otherwise
 */
bool wifi_is_connected(void);

/**
 * @brief Get signal strength
 * 
 * @return Signal strength in dBm, or INT8_MIN if not connected
 */
int8_t wifi_get_rssi(void);

/**
 * @brief Force reconnect to Wi-Fi (useful after disconnect)
 * 
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t wifi_module_reconnect(void);

/**
 * @brief Deinitialize Wi-Fi module
 * 
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t wifi_module_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // WIFI_MODULE_H