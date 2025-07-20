#include "time_module.h"
#include "display_module.h"
#include <esp_log.h>
#include <esp_timer.h>
#include <sys/time.h>
#include <driver/i2c.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char *TAG = "TimeModule";

// DS3231 RTC I2C configuration
#define DS3231_I2C_ADDR         0x68
#define DS3231_I2C_PORT         I2C_NUM_0
#define DS3231_SDA_GPIO         GPIO_NUM_8
#define DS3231_SCL_GPIO         GPIO_NUM_18
#define DS3231_I2C_FREQ_HZ      100000

// DS3231 register addresses
#define DS3231_REG_SECONDS      0x00
#define DS3231_REG_MINUTES      0x01
#define DS3231_REG_HOURS        0x02
#define DS3231_REG_DAY          0x03
#define DS3231_REG_DATE         0x04
#define DS3231_REG_MONTH        0x05
#define DS3231_REG_YEAR         0x06

// Module state
static bool module_initialized = false;
static bool rtc_available = false;
static time_status_t current_status = TIME_STATUS_NOT_SET;
static esp_timer_handle_t update_timer = NULL;
static time_info_t last_known_time = {0};

// Forward declarations
static esp_err_t ds3231_init(void);
static esp_err_t ds3231_read_time(time_info_t *time_info);
static esp_err_t ds3231_write_time(const time_info_t *time_info);
static uint8_t bcd_to_dec(uint8_t val);
static uint8_t dec_to_bcd(uint8_t val);
static void time_update_timer_callback(void *arg);
static void update_display_with_current_time(void);

static esp_err_t ds3231_init(void)
{
    ESP_LOGI(TAG, "Initializing DS3231 RTC on I2C pins SDA:%d, SCL:%d", DS3231_SDA_GPIO, DS3231_SCL_GPIO);
    
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = DS3231_SDA_GPIO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = DS3231_SCL_GPIO,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = DS3231_I2C_FREQ_HZ,
    };
    
    esp_err_t ret = i2c_param_config(DS3231_I2C_PORT, &conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure I2C: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = i2c_driver_install(DS3231_I2C_PORT, conf.mode, 0, 0, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install I2C driver: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Test communication with DS3231
    uint8_t test_data;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (DS3231_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, DS3231_REG_SECONDS, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (DS3231_I2C_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read_byte(cmd, &test_data, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    ret = i2c_master_cmd_begin(DS3231_I2C_PORT, cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "DS3231 RTC communication test successful");
        rtc_available = true;
        current_status = TIME_STATUS_OK;
        
        // Test read current time and show what we get
        time_info_t test_time;
        esp_err_t read_ret = ds3231_read_time(&test_time);
        if (read_ret == ESP_OK) {
            ESP_LOGI(TAG, "Current RTC time: %04d-%02d-%02d %02d:%02d:%02d", 
                     test_time.year, test_time.month, test_time.day,
                     test_time.hour, test_time.minute, test_time.second);
            
            // If year is 2000, RTC needs to be set with current time
            if (test_time.year == 2000) {
                ESP_LOGW(TAG, "RTC shows default time, setting to 2025-07-20 15:35:00");
                time_info_t new_time = {
                    .year = 2025,
                    .month = 7,
                    .day = 20,
                    .hour = 15,
                    .minute = 35,
                    .second = 0,
                    .weekday = 0,
                    .status = TIME_STATUS_OK
                };
                ds3231_write_time(&new_time);
            }
        }
    } else {
        ESP_LOGW(TAG, "DS3231 RTC not found or communication failed: %s", esp_err_to_name(ret));
        rtc_available = false;
        current_status = TIME_STATUS_RTC_ERROR;
    }
    
    return ESP_OK; // Don't fail initialization if RTC is not available
}

static uint8_t bcd_to_dec(uint8_t val)
{
    return (val / 16 * 10) + (val % 16);
}

static uint8_t dec_to_bcd(uint8_t val)
{
    return (val / 10 * 16) + (val % 10);
}

static esp_err_t ds3231_read_time(time_info_t *time_info)
{
    if (!rtc_available) {
        return ESP_FAIL;
    }
    
    uint8_t data[7];
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (DS3231_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, DS3231_REG_SECONDS, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (DS3231_I2C_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd, data, 7, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(DS3231_I2C_PORT, cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read time from DS3231: %s", esp_err_to_name(ret));
        current_status = TIME_STATUS_RTC_ERROR;
        return ret;
    }
    
    // Convert BCD to decimal
    time_info->second = bcd_to_dec(data[0] & 0x7F);
    time_info->minute = bcd_to_dec(data[1] & 0x7F);
    time_info->hour = bcd_to_dec(data[2] & 0x3F);  // 24-hour format
    time_info->weekday = bcd_to_dec(data[3] & 0x07);
    time_info->day = bcd_to_dec(data[4] & 0x3F);
    time_info->month = bcd_to_dec(data[5] & 0x1F);
    time_info->year = 2000 + bcd_to_dec(data[6]);
    time_info->status = TIME_STATUS_OK;
    
    ESP_LOGI(TAG, "Raw data: %02x %02x %02x %02x %02x %02x %02x", 
             data[0], data[1], data[2], data[3], data[4], data[5], data[6]);
    
    current_status = TIME_STATUS_OK;
    last_known_time = *time_info;
    
    ESP_LOGI(TAG, "Time read: %04d-%02d-%02d %02d:%02d:%02d", 
             time_info->year, time_info->month, time_info->day,
             time_info->hour, time_info->minute, time_info->second);
    
    return ESP_OK;
}

static esp_err_t ds3231_write_time(const time_info_t *time_info)
{
    if (!rtc_available) {
        return ESP_FAIL;
    }
    
    uint8_t data[7];
    data[0] = dec_to_bcd(time_info->second);
    data[1] = dec_to_bcd(time_info->minute);
    data[2] = dec_to_bcd(time_info->hour);
    data[3] = dec_to_bcd(time_info->weekday);
    data[4] = dec_to_bcd(time_info->day);
    data[5] = dec_to_bcd(time_info->month);
    data[6] = dec_to_bcd(time_info->year - 2000);
    
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (DS3231_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, DS3231_REG_SECONDS, true);
    i2c_master_write(cmd, data, 7, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(DS3231_I2C_PORT, cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write time to DS3231: %s", esp_err_to_name(ret));
        current_status = TIME_STATUS_RTC_ERROR;
        return ret;
    }
    
    ESP_LOGI(TAG, "Time set: %04d-%02d-%02d %02d:%02d:%02d", 
             time_info->year, time_info->month, time_info->day,
             time_info->hour, time_info->minute, time_info->second);
    
    return ESP_OK;
}

static void time_update_timer_callback(void *arg)
{
    update_display_with_current_time();
}

static void update_display_with_current_time(void)
{
    time_info_t current_time;
    esp_err_t ret = time_module_get_time(&current_time);
    
    if (ret == ESP_OK && current_time.status == TIME_STATUS_OK) {
        // Update display with current time
        display_update_time(current_time.hour, current_time.minute, current_time.second);
        display_update_date(current_time.year, current_time.month, current_time.day);
    } else {
        // Show error message in time area
        const char* status_msg = time_module_get_status_string();
        display_show_time_error(status_msg);
    }
}

esp_err_t time_module_init(void)
{
    if (module_initialized) {
        ESP_LOGW(TAG, "Time module already initialized");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing time module...");
    
    // Initialize DS3231 RTC
    esp_err_t ret = ds3231_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize DS3231");
        // Continue initialization even if RTC fails
    }
    
    module_initialized = true;
    ESP_LOGI(TAG, "Time module initialized successfully");
    
    return ESP_OK;
}

esp_err_t time_module_get_time(time_info_t *time_info)
{
    if (!module_initialized) {
        return ESP_FAIL;
    }
    
    if (rtc_available) {
        return ds3231_read_time(time_info);
    } else {
        // Fallback to system time or default
        time_info->status = current_status;
        *time_info = last_known_time;
        return ESP_FAIL;
    }
}

esp_err_t time_module_set_time(int year, int month, int day, int hour, int minute, int second)
{
    if (!module_initialized) {
        return ESP_FAIL;
    }
    
    time_info_t time_info = {
        .year = year,
        .month = month,
        .day = day,
        .hour = hour,
        .minute = minute,
        .second = second,
        .weekday = 0,  // Will be calculated by RTC
        .status = TIME_STATUS_OK
    };
    
    if (rtc_available) {
        return ds3231_write_time(&time_info);
    } else {
        ESP_LOGW(TAG, "RTC not available, cannot set time");
        return ESP_FAIL;
    }
}

const char* time_module_get_status_string(void)
{
    switch (current_status) {
        case TIME_STATUS_OK:
            return "Time OK";
        case TIME_STATUS_RTC_ERROR:
            return "RTC Error";
        case TIME_STATUS_NOT_SET:
            return "Time Not Set";
        case TIME_STATUS_SYNC_FAILED:
            return "Sync Failed";
        default:
            return "Unknown";
    }
}

esp_err_t time_module_start_display_updates(void)
{
    if (!module_initialized) {
        return ESP_FAIL;
    }
    
    if (update_timer != NULL) {
        ESP_LOGW(TAG, "Display updates already running");
        return ESP_OK;
    }
    
    const esp_timer_create_args_t timer_args = {
        .callback = &time_update_timer_callback,
        .name = "time_update"
    };
    
    esp_err_t ret = esp_timer_create(&timer_args, &update_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create update timer: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = esp_timer_start_periodic(update_timer, 1000000); // 1 second
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start update timer: %s", esp_err_to_name(ret));
        esp_timer_delete(update_timer);
        update_timer = NULL;
        return ret;
    }
    
    ESP_LOGI(TAG, "Started periodic display updates every 1 second");
    
    // Update display immediately
    update_display_with_current_time();
    
    return ESP_OK;
}

esp_err_t time_module_stop_display_updates(void)
{
    if (update_timer != NULL) {
        esp_timer_stop(update_timer);
        esp_timer_delete(update_timer);
        update_timer = NULL;
        ESP_LOGI(TAG, "Stopped display updates");
    }
    
    return ESP_OK;
}

esp_err_t time_module_deinit(void)
{
    if (!module_initialized) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Deinitializing time module...");
    
    // Stop display updates
    time_module_stop_display_updates();
    
    // Deinitialize I2C driver
    if (rtc_available) {
        i2c_driver_delete(DS3231_I2C_PORT);
        rtc_available = false;
    }
    
    module_initialized = false;
    current_status = TIME_STATUS_NOT_SET;
    
    ESP_LOGI(TAG, "Time module deinitialized successfully");
    return ESP_OK;
}