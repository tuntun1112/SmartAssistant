// wifi_connect.c
#include <string.h>
#include "wifi_connect.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/timers.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"

#define WIFI_CONNECTED_BIT BIT0

static EventGroupHandle_t wifi_event_group;
static TimerHandle_t reconnect_timer = NULL;
static const char *TAG = "wifi_connect";

// 重連計時器回呼函式：計時器到期後嘗試重新連線
static void reconnect_timer_callback(TimerHandle_t xTimer) {
    ESP_LOGI(TAG, "Attempting to reconnect Wi-Fi");
    esp_wifi_connect();
}

// 事件處理函式
static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                esp_wifi_connect();
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGW(TAG, "Wi-Fi disconnected, will retry in 5000 ms");
                // 若計時器尚未啟動，則啟動之
                if (xTimerIsTimerActive(reconnect_timer) == pdFALSE) {
                    if (xTimerStart(reconnect_timer, 0) != pdPASS) {
                        ESP_LOGE(TAG, "Failed to start reconnect timer");
                    }
                }
                break;
            default:
                break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_connect(const char *ssid, const char *password) {
    // NVS 初始化，處理可能的錯誤情況
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    // 基本網路與事件系統初始化
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // 創建 Event Group
    wifi_event_group = xEventGroupCreate();

    // 創建一個 5000 毫秒後到期的一次性重連計時器
    reconnect_timer = xTimerCreate("reconnect_timer", pdMS_TO_TICKS(5000), pdFALSE, NULL, reconnect_timer_callback);
    if (reconnect_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create reconnect timer");
    }

    // 註冊事件處理器
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    // 設定 Wi-Fi 配置
    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    wifi_config.sta.ssid[sizeof(wifi_config.sta.ssid) - 1] = '\0';

    strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.password[sizeof(wifi_config.sta.password) - 1] = '\0';

    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;  // 強制 WPA2
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    // 啟動 Wi-Fi STA 模式
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting to Wi-Fi SSID: %s", ssid);

    // 等待連線成功
    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, false, true, portMAX_DELAY);
}
