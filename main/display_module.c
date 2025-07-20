#include "display_module.h"
#include <driver/gpio.h>
#include <driver/ledc.h>
#include <driver/spi_master.h>
#include <esp_err.h>
#include <esp_freertos_hooks.h>
#include <esp_log.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_ops.h>
#include "esp_lcd_ili9488.h"
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lvgl.h>
#include <stdio.h>
#include "sdkconfig.h"
#include "fonts/chinese_font_16.h"


static const char *TAG = "DisplayModule";

// Display configuration constants
static const int DISPLAY_HORIZONTAL_PIXELS = 480;
static const int DISPLAY_VERTICAL_PIXELS = 320;
static const int DISPLAY_COMMAND_BITS = 8;
static const int DISPLAY_PARAMETER_BITS = 8;
static const unsigned int DISPLAY_REFRESH_HZ = 40000000;
static const int DISPLAY_SPI_QUEUE_LEN = 10;
static const int SPI_MAX_TRANSFER_SIZE = 32768;

// GPIO pin definitions
static const gpio_num_t SPI_CLOCK = GPIO_NUM_11;
static const gpio_num_t SPI_MOSI = GPIO_NUM_10;
static const gpio_num_t SPI_MISO = GPIO_NUM_13;
static const gpio_num_t TFT_CS = GPIO_NUM_3;
static const gpio_num_t TFT_RESET = GPIO_NUM_46;
static const gpio_num_t TFT_DC = GPIO_NUM_9;
static const gpio_num_t TFT_BACKLIGHT = GPIO_NUM_12;

// Display configuration
static const lcd_rgb_element_order_t TFT_COLOR_MODE = COLOR_RGB_ELEMENT_ORDER_BGR;
static const size_t LV_BUFFER_SIZE = DISPLAY_HORIZONTAL_PIXELS * 25;
static const int LVGL_UPDATE_PERIOD_MS = 5;

// Backlight configuration
static const ledc_mode_t BACKLIGHT_LEDC_MODE = LEDC_LOW_SPEED_MODE;
static const ledc_channel_t BACKLIGHT_LEDC_CHANNEL = LEDC_CHANNEL_0;
static const ledc_timer_t BACKLIGHT_LEDC_TIMER = LEDC_TIMER_1;
static const ledc_timer_bit_t BACKLIGHT_LEDC_TIMER_RESOLUTION = LEDC_TIMER_10_BIT;
static const uint32_t BACKLIGHT_LEDC_FRQUENCY = 5000;

// Static variables
static esp_lcd_panel_io_handle_t lcd_io_handle = NULL;
static esp_lcd_panel_handle_t lcd_handle = NULL;
static lv_disp_draw_buf_t lv_disp_buf;
static lv_disp_drv_t lv_disp_drv;
static lv_disp_t *lv_display = NULL;
static lv_color_t *lv_buf_1 = NULL;
static lv_color_t *lv_buf_2 = NULL;
static lv_obj_t *boot_label = NULL;
static lv_obj_t *boot_spinner = NULL;
static lv_obj_t *boot_status_label = NULL;
static lv_obj_t *boot_progress_bar = NULL;
static lv_obj_t *boot_screen = NULL;
static lv_obj_t *main_screen = NULL;
static esp_timer_handle_t lvgl_tick_timer = NULL;
static lv_style_t style_chinese_font;

// Time-related UI components for clock module integration
static lv_obj_t *time_label = NULL;
static lv_obj_t *date_label = NULL;
static char current_time_str[16] = "12:34:56";
static char current_date_str[32] = "Jul 20, 2025";

// PIR sensor status UI component
static lv_obj_t *pir_status_label = NULL;
static lv_obj_t *motion_status_label = NULL;

// Forward declarations
static bool notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io,
    esp_lcd_panel_io_event_data_t *edata, void *user_ctx);
static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map);
static void lvgl_tick_cb(void *param);
static esp_err_t display_brightness_init(void);
static esp_err_t initialize_spi(void);
static esp_err_t initialize_display(void);
static esp_err_t initialize_lvgl(void);
static void create_boot_screen(void);
static void create_main_screen(void);
static void update_boot_progress(int progress);

static bool notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io,
    esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    lv_disp_drv_t *disp_driver = (lv_disp_drv_t *)user_ctx;
    lv_disp_flush_ready(disp_driver);
    return false;
}

static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t) drv->user_data;

    int offsetx1 = area->x1;
    int offsetx2 = area->x2;
    int offsety1 = area->y1;
    int offsety2 = area->y2;
    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, color_map);
}

static void IRAM_ATTR lvgl_tick_cb(void *param)
{
    lv_tick_inc(LVGL_UPDATE_PERIOD_MS);
}

static esp_err_t display_brightness_init(void)
{
    const ledc_channel_config_t LCD_backlight_channel =
    {
        .gpio_num = TFT_BACKLIGHT,
        .speed_mode = BACKLIGHT_LEDC_MODE,
        .channel = BACKLIGHT_LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = BACKLIGHT_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
        .flags = 
        {
            .output_invert = 0
        }
    };
    const ledc_timer_config_t LCD_backlight_timer =
    {
        .speed_mode = BACKLIGHT_LEDC_MODE,
        .duty_resolution = BACKLIGHT_LEDC_TIMER_RESOLUTION,
        .timer_num = BACKLIGHT_LEDC_TIMER,
        .freq_hz = BACKLIGHT_LEDC_FRQUENCY,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ESP_LOGI(TAG, "Initializing LEDC for backlight pin: %d", TFT_BACKLIGHT);

    esp_err_t ret = ledc_timer_config(&LCD_backlight_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure LEDC timer: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = ledc_channel_config(&LCD_backlight_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure LEDC channel: %s", esp_err_to_name(ret));
        return ret;
    }
    
    return ESP_OK;
}

void display_set_brightness(int brightness_percentage)
{
    if (brightness_percentage > 100)
    {
        brightness_percentage = 100;
    }    
    else if (brightness_percentage < 0)
    {
        brightness_percentage = 0;
    }
    ESP_LOGI(TAG, "Setting backlight to %d%%", brightness_percentage);

    uint32_t duty_cycle = (1023 * brightness_percentage) / 100;
    ESP_ERROR_CHECK(ledc_set_duty(BACKLIGHT_LEDC_MODE, BACKLIGHT_LEDC_CHANNEL, duty_cycle));
    ESP_ERROR_CHECK(ledc_update_duty(BACKLIGHT_LEDC_MODE, BACKLIGHT_LEDC_CHANNEL));
}

static esp_err_t initialize_spi(void)
{
    ESP_LOGI(TAG, "Initializing SPI bus (MOSI:%d, MISO:%d, CLK:%d)",
             SPI_MOSI, SPI_MISO, SPI_CLOCK);
    spi_bus_config_t bus =
    {
        .mosi_io_num = SPI_MOSI,
        .miso_io_num = SPI_MISO,
        .sclk_io_num = SPI_CLOCK,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .data4_io_num = GPIO_NUM_NC,
        .data5_io_num = GPIO_NUM_NC,
        .data6_io_num = GPIO_NUM_NC,
        .data7_io_num = GPIO_NUM_NC,
        .max_transfer_sz = SPI_MAX_TRANSFER_SIZE,
        .flags = SPICOMMON_BUSFLAG_SCLK | SPICOMMON_BUSFLAG_MISO |
                 SPICOMMON_BUSFLAG_MOSI | SPICOMMON_BUSFLAG_MASTER,
        .intr_flags = ESP_INTR_FLAG_LOWMED | ESP_INTR_FLAG_IRAM
    };

    esp_err_t ret = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        return ret;
    }
    
    return ESP_OK;
}

static esp_err_t initialize_display(void)
{
    const esp_lcd_panel_io_spi_config_t io_config = 
    {
        .cs_gpio_num = TFT_CS,
        .dc_gpio_num = TFT_DC,
        .spi_mode = 0,
        .pclk_hz = DISPLAY_REFRESH_HZ,
        .trans_queue_depth = DISPLAY_SPI_QUEUE_LEN,
        .on_color_trans_done = notify_lvgl_flush_ready,
        .user_ctx = &lv_disp_drv,
        .lcd_cmd_bits = DISPLAY_COMMAND_BITS,
        .lcd_param_bits = DISPLAY_PARAMETER_BITS,
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5,4,0)
        .cs_ena_pretrans = 0,
        .cs_ena_posttrans = 0,
#endif
        .flags =
        {
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
            .dc_as_cmd_phase = 0,
            .dc_low_on_data = 0,
            .octal_mode = 0,
            .lsb_first = 0
#else
            .dc_low_on_data = 0,
            .octal_mode = 0,
            .sio_mode = 0,
            .lsb_first = 0,
            .cs_high_active = 0
#endif
        }
    };

    const esp_lcd_panel_dev_config_t lcd_config = 
    {
        .reset_gpio_num = TFT_RESET,
        .color_space = TFT_COLOR_MODE,
        .bits_per_pixel = 18,
        .flags =
        {
            .reset_active_high = 0
        },
        .vendor_config = NULL
    };

    esp_err_t ret = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_config, &lcd_io_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create LCD panel I/O: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_lcd_new_panel_ili9488(lcd_io_handle, &lcd_config, LV_BUFFER_SIZE, &lcd_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create ILI9488 panel: %s", esp_err_to_name(ret));
        esp_lcd_panel_io_del(lcd_io_handle);
        return ret;
    }

    ret = esp_lcd_panel_reset(lcd_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reset LCD panel: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = esp_lcd_panel_init(lcd_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LCD panel: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = esp_lcd_panel_invert_color(lcd_handle, false);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set color inversion: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = esp_lcd_panel_swap_xy(lcd_handle, true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to swap XY: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = esp_lcd_panel_mirror(lcd_handle, false, true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set mirror: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = esp_lcd_panel_set_gap(lcd_handle, 0, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set gap: %s", esp_err_to_name(ret));
        return ret;
    }
    
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
    ret = esp_lcd_panel_disp_off(lcd_handle, false);
#else
    ret = esp_lcd_panel_disp_on_off(lcd_handle, true);
#endif
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to turn on display: %s", esp_err_to_name(ret));
        return ret;
    }
    
    return ESP_OK;
}

static esp_err_t initialize_lvgl(void)
{
    ESP_LOGI(TAG, "Initializing LVGL");
    lv_init();
    ESP_LOGI(TAG, "Allocating %zu bytes for LVGL buffer", LV_BUFFER_SIZE * sizeof(lv_color_t));
    lv_buf_1 = (lv_color_t *)heap_caps_malloc(LV_BUFFER_SIZE * sizeof(lv_color_t), MALLOC_CAP_DMA);
    
    if (lv_buf_1 == NULL) {
        ESP_LOGE(TAG, "Failed to allocate LVGL buffer memory");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Creating LVGL display buffer");
    lv_disp_draw_buf_init(&lv_disp_buf, lv_buf_1, lv_buf_2, LV_BUFFER_SIZE);

    ESP_LOGI(TAG, "Initializing %dx%d display", DISPLAY_HORIZONTAL_PIXELS, DISPLAY_VERTICAL_PIXELS);
    lv_disp_drv_init(&lv_disp_drv);
    lv_disp_drv.hor_res = DISPLAY_HORIZONTAL_PIXELS;
    lv_disp_drv.ver_res = DISPLAY_VERTICAL_PIXELS;
    lv_disp_drv.flush_cb = lvgl_flush_cb;
    lv_disp_drv.draw_buf = &lv_disp_buf;
    lv_disp_drv.user_data = lcd_handle;
    lv_display = lv_disp_drv_register(&lv_disp_drv);

    ESP_LOGI(TAG, "Initializing Chinese font style");
    lv_style_init(&style_chinese_font);
    lv_style_set_text_font(&style_chinese_font, &lv_font_montserrat_14);

    ESP_LOGI(TAG, "Creating LVGL tick timer");
    const esp_timer_create_args_t lvgl_tick_timer_args =
    {
        .callback = &lvgl_tick_cb,
        .name = "lvgl_tick"
    };
    
    esp_err_t ret = esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create LVGL timer: %s", esp_err_to_name(ret));
        heap_caps_free(lv_buf_1);
        lv_buf_1 = NULL;
        return ret;
    }
    
    ret = esp_timer_start_periodic(lvgl_tick_timer, LVGL_UPDATE_PERIOD_MS * 1000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start LVGL timer: %s", esp_err_to_name(ret));
        esp_timer_delete(lvgl_tick_timer);
        lvgl_tick_timer = NULL;
        heap_caps_free(lv_buf_1);
        lv_buf_1 = NULL;
        return ret;
    }
    
    return ESP_OK;
}


static void update_boot_progress(int progress)
{
    if (boot_progress_bar != NULL) {
        lv_bar_set_value(boot_progress_bar, progress, LV_ANIM_ON);
    }
}


static void create_boot_screen(void)
{
    boot_screen = lv_obj_create(NULL);
    lv_obj_clear_flag(boot_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(boot_screen, lv_color_black(), LV_STATE_DEFAULT);

    lv_obj_t *system_title = lv_label_create(boot_screen);
    lv_label_set_text(system_title, "Smart Assistant");
    lv_obj_set_style_text_color(system_title, lv_color_white(), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(system_title, LV_FONT_DEFAULT, LV_STATE_DEFAULT);
    lv_obj_align(system_title, LV_ALIGN_CENTER, 0, -30);

    boot_progress_bar = lv_bar_create(boot_screen);
    lv_obj_set_size(boot_progress_bar, 200, 8);
    lv_obj_align(boot_progress_bar, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(boot_progress_bar, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_bg_color(boot_progress_bar, lv_color_white(), LV_PART_INDICATOR);
    lv_bar_set_range(boot_progress_bar, 0, 100);
    lv_bar_set_value(boot_progress_bar, 0, LV_ANIM_OFF);

    boot_status_label = lv_label_create(boot_screen);
    lv_label_set_text(boot_status_label, "Starting...");
    lv_obj_set_style_text_color(boot_status_label, lv_color_white(), LV_STATE_DEFAULT);
    lv_obj_add_style(boot_status_label, &style_chinese_font, 0);
    lv_obj_align(boot_status_label, LV_ALIGN_CENTER, 0, 30);

    lv_scr_load(boot_screen);

    ESP_LOGI(TAG, "Simple boot screen created");
}

static void create_main_screen(void)
{
    main_screen = lv_obj_create(NULL);
    lv_obj_clear_flag(main_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(main_screen, lv_color_black(), LV_STATE_DEFAULT);

    // Action recognition status (top-left)
    lv_obj_t *motion_text = lv_label_create(main_screen);
    lv_label_set_text(motion_text, "Action: PC");
    lv_obj_set_style_text_color(motion_text, lv_color_white(), LV_STATE_DEFAULT);
    lv_obj_add_style(motion_text, &style_chinese_font, 0);
    lv_obj_align(motion_text, LV_ALIGN_TOP_LEFT, 10, 10);

    // PIR sensor status (top-left, below action)
    pir_status_label = lv_label_create(main_screen);
    lv_label_set_text(pir_status_label, "PIR: No");
    lv_obj_set_style_text_color(pir_status_label, lv_color_white(), LV_STATE_DEFAULT);
    lv_obj_add_style(pir_status_label, &style_chinese_font, 0);
    lv_obj_align(pir_status_label, LV_ALIGN_TOP_LEFT, 10, 30);

    // MPU6050 motion sensor status (below PIR status)
    motion_status_label = lv_label_create(main_screen);
    lv_label_set_text(motion_status_label, "MPU: None");
    lv_obj_set_style_text_color(motion_status_label, lv_color_white(), LV_STATE_DEFAULT);
    lv_obj_add_style(motion_status_label, &style_chinese_font, 0);
    lv_obj_align(motion_status_label, LV_ALIGN_TOP_LEFT, 10, 50);

    // Weather info (top-right)
    lv_obj_t *weather_text = lv_label_create(main_screen);
    lv_label_set_text(weather_text, "21C Rainy");
    lv_obj_set_style_text_color(weather_text, lv_color_white(), LV_STATE_DEFAULT);
    lv_obj_add_style(weather_text, &style_chinese_font, 0);
    lv_obj_align(weather_text, LV_ALIGN_TOP_RIGHT, -10, 10);

    // Large time display (center) - USE SAME FONT AS OTHER LABELS
    time_label = lv_label_create(main_screen);
    lv_label_set_text(time_label, current_time_str);
    
    // Step 1: Add white color for visibility  
    lv_obj_set_style_text_color(time_label, lv_color_white(), LV_STATE_DEFAULT);
    
    // Step 2: Use same font style as other labels for consistency
    lv_obj_add_style(time_label, &style_chinese_font, 0);
    
    // Step 3: Position
    lv_obj_align(time_label, LV_ALIGN_CENTER, 0, -20);
    
    ESP_LOGI(TAG, "Time label created with WHITE COLOR and DEFAULT FONT (ptr: %p)", time_label);

    // Date display
    date_label = lv_label_create(main_screen);
    lv_label_set_text(date_label, current_date_str);
    lv_obj_set_style_text_color(date_label, lv_color_white(), LV_STATE_DEFAULT);
    lv_obj_add_style(date_label, &style_chinese_font, 0);
    lv_obj_align(date_label, LV_ALIGN_CENTER, 0, 40);

    // Assistant status (bottom-left)
    lv_obj_t *assistant_text = lv_label_create(main_screen);
    lv_label_set_text(assistant_text, "Assistant");
    lv_obj_set_style_text_color(assistant_text, lv_color_white(), LV_STATE_DEFAULT);
    lv_obj_add_style(assistant_text, &style_chinese_font, 0);
    lv_obj_align(assistant_text, LV_ALIGN_BOTTOM_LEFT, 10, -10);

    // WiFi status (bottom-right)
    lv_obj_t *wifi_text = lv_label_create(main_screen);
    lv_label_set_text(wifi_text, "WiFi: OK");
    lv_obj_set_style_text_color(wifi_text, lv_color_white(), LV_STATE_DEFAULT);
    lv_obj_add_style(wifi_text, &style_chinese_font, 0);
    lv_obj_align(wifi_text, LV_ALIGN_BOTTOM_RIGHT, -10, -10);

    ESP_LOGI(TAG, "Simple main screen created");
}

void display_update_boot_status(const char* status_text, int progress)
{
    if (boot_status_label != NULL) {
        lv_label_set_text(boot_status_label, status_text);
    }
    
    update_boot_progress(progress);
    ESP_LOGI(TAG, "Boot status updated: %s (%d%%)", status_text, progress);
}

void display_complete_boot_animation(void)
{
    if (main_screen == NULL) {
        create_main_screen();
    }
    
    ESP_LOGI(TAG, "Transitioning to main screen");
    
    // Use auto-delete feature of lv_scr_load_anim to automatically clean up old screen
    lv_scr_load_anim(main_screen, LV_SCR_LOAD_ANIM_FADE_IN, 500, 0, true);
    
    // Reset pointers after scheduling deletion (they will be freed by LVGL)
    boot_screen = NULL;
    boot_label = NULL;
    boot_spinner = NULL;
    boot_status_label = NULL;
    boot_progress_bar = NULL;
}

esp_err_t display_init_and_show_boot_animation(void)
{
    ESP_LOGI(TAG, "Initializing display system...");
    
    // Initialize backlight (initially off)
    esp_err_t ret = display_brightness_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize backlight: %s", esp_err_to_name(ret));
        return ret;
    }
    display_set_brightness(0);
    
    // Initialize display components
    ret = initialize_spi();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = initialize_display();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize display: %s", esp_err_to_name(ret));
        spi_bus_free(SPI2_HOST);
        return ret;
    }
    
    ret = initialize_lvgl();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LVGL: %s", esp_err_to_name(ret));
        if (lcd_handle) esp_lcd_panel_del(lcd_handle);
        if (lcd_io_handle) esp_lcd_panel_io_del(lcd_io_handle);
        spi_bus_free(SPI2_HOST);
        return ret;
    }
    
    create_boot_screen();
    
    // Give LVGL time to render the boot screen
    vTaskDelay(pdMS_TO_TICKS(100));
    lv_timer_handler();
    
    // Turn on backlight
    display_set_brightness(80);
    ESP_LOGI(TAG, "Display system initialized and boot animation started");
    
    return ESP_OK;
}

void display_task_handler(void)
{
    lv_timer_handler();
}

void display_update_time(int hours, int minutes, int seconds)
{
    if (time_label != NULL) {
        snprintf(current_time_str, sizeof(current_time_str), "%02d:%02d:%02d", hours, minutes, seconds);
        lv_label_set_text(time_label, current_time_str);
        ESP_LOGI(TAG, "Time updated: %s (label ptr: %p)", current_time_str, time_label);
    } else {
        ESP_LOGE(TAG, "Time label is NULL!");
    }
}

void display_update_date(int year, int month, int day)
{
    if (date_label != NULL) {
        const char* month_names[] = {
            "Jan", "Feb", "Mar", "Apr", "May", "Jun",
            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
        };
        
        if (month >= 1 && month <= 12) {
            snprintf(current_date_str, sizeof(current_date_str), "%s %d, %d", 
                     month_names[month-1], day, year);
            lv_label_set_text(date_label, current_date_str);
            ESP_LOGI(TAG, "Date updated: %s", current_date_str);
        }
    }
}

void display_show_time_error(const char* error_message)
{
    if (time_label != NULL) {
        lv_label_set_text(time_label, error_message);
        ESP_LOGI(TAG, "Time error displayed: %s", error_message);
    }
    if (date_label != NULL) {
        lv_label_set_text(date_label, "RTC Error");
        ESP_LOGI(TAG, "Date error displayed");
    }
}

void display_update_pir_status(const char* pir_status_text)
{
    if (pir_status_label != NULL && pir_status_text != NULL) {
        lv_label_set_text(pir_status_label, pir_status_text);
        ESP_LOGD(TAG, "PIR status updated: %s", pir_status_text);
    } else {
        ESP_LOGW(TAG, "PIR status label is NULL or invalid text provided");
    }
}

void display_update_motion_status(const char* motion_status_text)
{
    if (motion_status_label != NULL && motion_status_text != NULL) {
        lv_label_set_text(motion_status_label, motion_status_text);
        ESP_LOGD(TAG, "Motion status updated: %s", motion_status_text);
    } else {
        ESP_LOGW(TAG, "Motion status label is NULL or invalid text provided");
    }
}

esp_err_t display_deinit(void)
{
    ESP_LOGI(TAG, "Deinitializing display system...");
    
    // Stop and delete LVGL timer
    if (lvgl_tick_timer != NULL) {
        esp_timer_stop(lvgl_tick_timer);
        esp_timer_delete(lvgl_tick_timer);
        lvgl_tick_timer = NULL;
        ESP_LOGI(TAG, "LVGL timer deleted");
    }
    
    // Free LVGL buffer memory
    if (lv_buf_1 != NULL) {
        heap_caps_free(lv_buf_1);
        lv_buf_1 = NULL;
        ESP_LOGI(TAG, "LVGL buffer freed");
    }
    
    // Delete LCD panel
    if (lcd_handle != NULL) {
        esp_lcd_panel_del(lcd_handle);
        lcd_handle = NULL;
        ESP_LOGI(TAG, "LCD panel deleted");
    }
    
    // Delete LCD panel I/O
    if (lcd_io_handle != NULL) {
        esp_lcd_panel_io_del(lcd_io_handle);
        lcd_io_handle = NULL;
        ESP_LOGI(TAG, "LCD I/O handle deleted");
    }
    
    // Free SPI bus
    esp_err_t ret = spi_bus_free(SPI2_HOST);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to free SPI bus: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "SPI bus freed");
    }
    
    // Reset other pointers
    lv_display = NULL;
    boot_label = NULL;
    boot_spinner = NULL;
    boot_status_label = NULL;
    boot_progress_bar = NULL;
    boot_screen = NULL;
    main_screen = NULL;
    time_label = NULL;
    date_label = NULL;
    
    ESP_LOGI(TAG, "Display system deinitialized successfully");
    return ESP_OK;
}