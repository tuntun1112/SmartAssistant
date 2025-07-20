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
static lv_obj_t *boot_logo_container = NULL;
static lv_obj_t *boot_screen = NULL;
static lv_obj_t *main_screen = NULL;
static lv_style_t style_screen;
static lv_style_t style_card;
static lv_style_t style_title;
static lv_style_t style_status_panel;
static lv_style_t style_progress_bar;
static lv_style_t style_logo;
static esp_timer_handle_t lvgl_tick_timer = NULL;

// Forward declarations
static bool notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io,
    esp_lcd_panel_io_event_data_t *edata, void *user_ctx);
static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map);
static void lvgl_tick_cb(void *param);
static esp_err_t display_brightness_init(void);
static esp_err_t initialize_spi(void);
static esp_err_t initialize_display(void);
static esp_err_t initialize_lvgl(void);
static void create_boot_animation(void);
static void create_main_screen(void);
static void init_styles(void);
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

static void init_styles(void)
{
    // Screen background style
    lv_style_init(&style_screen);
    lv_style_set_bg_color(&style_screen, lv_color_hex(0x0f0f23));
    lv_style_set_bg_grad_color(&style_screen, lv_color_hex(0x1a1a3a));
    lv_style_set_bg_grad_dir(&style_screen, LV_GRAD_DIR_VER);
    
    // Card style for containers
    lv_style_init(&style_card);
    lv_style_set_bg_color(&style_card, lv_color_hex(0x2d2d44));
    lv_style_set_bg_opa(&style_card, LV_OPA_90);
    lv_style_set_border_color(&style_card, lv_color_hex(0x4a4a6a));
    lv_style_set_border_width(&style_card, 1);
    lv_style_set_border_opa(&style_card, LV_OPA_50);
    lv_style_set_radius(&style_card, 12);
    lv_style_set_shadow_width(&style_card, 10);
    lv_style_set_shadow_color(&style_card, lv_color_black());
    lv_style_set_shadow_opa(&style_card, LV_OPA_30);
    lv_style_set_pad_all(&style_card, 8);
    
    // Title style
    lv_style_init(&style_title);
    lv_style_set_text_color(&style_title, lv_color_white());
    lv_style_set_text_font(&style_title, &lv_font_montserrat_14);
    
    // Status panel style
    lv_style_init(&style_status_panel);
    lv_style_set_bg_color(&style_status_panel, lv_color_hex(0x1a1a3a));
    lv_style_set_bg_opa(&style_status_panel, LV_OPA_80);
    lv_style_set_radius(&style_status_panel, 8);
    lv_style_set_pad_all(&style_status_panel, 8);
    lv_style_set_border_color(&style_status_panel, lv_color_hex(0x4CAF50));
    lv_style_set_border_width(&style_status_panel, 1);
    lv_style_set_border_opa(&style_status_panel, LV_OPA_50);
    
    // Progress bar style
    lv_style_init(&style_progress_bar);
    lv_style_set_bg_color(&style_progress_bar, lv_color_hex(0x4CAF50));
    lv_style_set_bg_grad_color(&style_progress_bar, lv_color_hex(0x81C784));
    lv_style_set_bg_grad_dir(&style_progress_bar, LV_GRAD_DIR_HOR);
    lv_style_set_radius(&style_progress_bar, 10);
    
    // Logo container style
    lv_style_init(&style_logo);
    lv_style_set_bg_color(&style_logo, lv_color_hex(0x4CAF50));
    lv_style_set_bg_opa(&style_logo, LV_OPA_20);
    lv_style_set_radius(&style_logo, 50);
    lv_style_set_border_color(&style_logo, lv_color_hex(0x4CAF50));
    lv_style_set_border_width(&style_logo, 2);
    lv_style_set_border_opa(&style_logo, LV_OPA_60);
}

static void update_boot_progress(int progress)
{
    if (boot_progress_bar != NULL) {
        lv_bar_set_value(boot_progress_bar, progress, LV_ANIM_ON);
    }
}

static void opacity_anim_cb(void * obj, int32_t value)
{
    lv_obj_set_style_opa((lv_obj_t*)obj, value, LV_STATE_DEFAULT);
}

static void create_boot_animation(void)
{
    // Initialize styles first
    init_styles();
    
    boot_screen = lv_obj_create(NULL);
    lv_obj_clear_flag(boot_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(boot_screen, &style_screen, LV_STATE_DEFAULT);

    // Create logo container with pulsing effect
    boot_logo_container = lv_obj_create(boot_screen);
    lv_obj_set_size(boot_logo_container, 100, 100);
    lv_obj_align(boot_logo_container, LV_ALIGN_CENTER, 0, -80);
    lv_obj_add_style(boot_logo_container, &style_logo, LV_STATE_DEFAULT);
    lv_obj_clear_flag(boot_logo_container, LV_OBJ_FLAG_SCROLLABLE);

    // Create title label inside logo container
    boot_label = lv_label_create(boot_logo_container);
    lv_label_set_text(boot_label, "SA");
    lv_obj_add_style(boot_label, &style_title, LV_STATE_DEFAULT);
    lv_obj_center(boot_label);

    // Create system title
    lv_obj_t *system_title = lv_label_create(boot_screen);
    lv_label_set_text(system_title, "SmartAssistant");
    lv_obj_add_style(system_title, &style_title, LV_STATE_DEFAULT);
    lv_obj_align(system_title, LV_ALIGN_CENTER, 0, -20);

    // Create progress bar
    boot_progress_bar = lv_bar_create(boot_screen);
    lv_obj_set_size(boot_progress_bar, 280, 8);
    lv_obj_align(boot_progress_bar, LV_ALIGN_CENTER, 0, 20);
    lv_obj_add_style(boot_progress_bar, &style_progress_bar, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(boot_progress_bar, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_radius(boot_progress_bar, 4, LV_PART_MAIN);
    lv_bar_set_range(boot_progress_bar, 0, 100);
    lv_bar_set_value(boot_progress_bar, 0, LV_ANIM_OFF);

    // Create status panel
    lv_obj_t *status_panel = lv_obj_create(boot_screen);
    lv_obj_set_size(status_panel, 300, 50);
    lv_obj_align(status_panel, LV_ALIGN_CENTER, 0, 65);
    lv_obj_add_style(status_panel, &style_status_panel, LV_STATE_DEFAULT);
    lv_obj_clear_flag(status_panel, LV_OBJ_FLAG_SCROLLABLE);

    // Create status label inside panel
    boot_status_label = lv_label_create(status_panel);
    lv_label_set_text(boot_status_label, "Initializing System...");
    lv_obj_add_style(boot_status_label, &style_title, LV_STATE_DEFAULT);
    lv_obj_center(boot_status_label);

    // Create pulsing animation for logo using opacity
    lv_anim_t pulse_anim;
    lv_anim_init(&pulse_anim);
    lv_anim_set_var(&pulse_anim, boot_logo_container);
    lv_anim_set_exec_cb(&pulse_anim, opacity_anim_cb);
    lv_anim_set_values(&pulse_anim, LV_OPA_50, LV_OPA_100);
    lv_anim_set_time(&pulse_anim, 1000);
    lv_anim_set_repeat_count(&pulse_anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_playback_time(&pulse_anim, 1000);
    lv_anim_start(&pulse_anim);

    // Load boot screen
    lv_scr_load(boot_screen);

    ESP_LOGI(TAG, "Modern boot animation created");
}

static void create_main_screen(void)
{
    main_screen = lv_obj_create(NULL);
    lv_obj_clear_flag(main_screen, LV_OBJ_FLAG_SCROLLABLE);
    // lv_obj_set_style_bg_color(main_screen, lv_color_hex(0xf0f0f0), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(main_screen, lv_color_hex(0xF8E8D0), LV_STATE_DEFAULT);

    // Motion recognition status card (top-left)
    lv_obj_t *motion_card = lv_obj_create(main_screen);
    lv_obj_set_size(motion_card, 200, 70);
    lv_obj_align(motion_card, LV_ALIGN_TOP_LEFT, 10, 10);
    lv_obj_set_style_bg_color(motion_card, lv_color_white(), LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(motion_card, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(motion_card, 1, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(motion_card, 10, LV_STATE_DEFAULT);
    lv_obj_clear_flag(motion_card, LV_OBJ_FLAG_SCROLLABLE);

    // Motion recognition header
    lv_obj_t *motion_header = lv_label_create(motion_card);
    lv_label_set_text(motion_header, "motion recognition model working!");
    lv_obj_set_style_text_color(motion_header, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(motion_header, &lv_font_montserrat_14, LV_STATE_DEFAULT);
    lv_obj_align(motion_header, LV_ALIGN_TOP_LEFT, 8, 3);

    // Computer icon and status
    lv_obj_t *computer_status = lv_label_create(motion_card);
    lv_label_set_text(computer_status, "Using computer 🖥️");
    lv_obj_set_style_text_color(computer_status, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(computer_status, &lv_font_montserrat_14, LV_STATE_DEFAULT);
    lv_obj_align(computer_status, LV_ALIGN_TOP_LEFT, 8, 20);

    // Accuracy and time
    lv_obj_t *accuracy_label = lv_label_create(motion_card);
    lv_label_set_text(accuracy_label, "94%accuracy");
    lv_obj_set_style_text_color(accuracy_label, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(accuracy_label, &lv_font_montserrat_14, LV_STATE_DEFAULT);
    lv_obj_align(accuracy_label, LV_ALIGN_BOTTOM_LEFT, 8, -3);

    lv_obj_t *time_ago = lv_label_create(motion_card);
    lv_label_set_text(time_ago, "125 minutes ago");
    lv_obj_set_style_text_color(time_ago, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(time_ago, &lv_font_montserrat_14, LV_STATE_DEFAULT);
    lv_obj_align(time_ago, LV_ALIGN_BOTTOM_RIGHT, -8, -3);

    // Weather card (top-right)
    lv_obj_t *weather_card = lv_obj_create(main_screen);
    lv_obj_set_size(weather_card, 200, 70);
    lv_obj_align(weather_card, LV_ALIGN_TOP_RIGHT, -10, 10);
    lv_obj_set_style_bg_color(weather_card, lv_color_white(), LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(weather_card, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(weather_card, 1, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(weather_card, 10, LV_STATE_DEFAULT);
    lv_obj_clear_flag(weather_card, LV_OBJ_FLAG_SCROLLABLE);

    // Weather temperature
    lv_obj_t *weather_temp = lv_label_create(weather_card);
    lv_label_set_text(weather_temp, "21°C / RAIN");
    lv_obj_set_style_text_color(weather_temp, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(weather_temp, &lv_font_montserrat_14, LV_STATE_DEFAULT);
    lv_obj_align(weather_temp, LV_ALIGN_TOP_RIGHT, -8, 8);

    // Cloud icon
    lv_obj_t *cloud_icon = lv_label_create(weather_card);
    lv_label_set_text(cloud_icon, "🌧️");
    lv_obj_set_style_text_color(cloud_icon, lv_color_hex(0x666666), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(cloud_icon, &lv_font_montserrat_14, LV_STATE_DEFAULT);
    lv_obj_align(cloud_icon, LV_ALIGN_TOP_RIGHT, -8, 25);

    // Location
    lv_obj_t *location_label = lv_label_create(weather_card);
    lv_label_set_text(location_label, "Taiwan Taoyuan Luzhu");
    lv_obj_set_style_text_color(location_label, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(location_label, &lv_font_montserrat_14, LV_STATE_DEFAULT);
    lv_obj_align(location_label, LV_ALIGN_BOTTOM_RIGHT, -8, -3);

    // VERY LARGE time display with seconds (center) - 使用較大字體
    lv_obj_t *time_label = lv_label_create(main_screen);
    lv_label_set_text(time_label, "12:34:56");
    lv_obj_set_style_text_color(time_label, lv_color_hex(0x333333), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(time_label, &lv_font_montserrat_14, LV_STATE_DEFAULT);
    // 使用 transform zoom 放大顯示
    lv_obj_set_style_transform_zoom(time_label, 300, LV_STATE_DEFAULT);
    lv_obj_align(time_label, LV_ALIGN_CENTER, -20, -40);

    // 新增分隔線於時間下方
    static lv_point_t time_line_points[] = {{0,0}, {300,0}};
    lv_obj_t *time_line = lv_line_create(main_screen);
    lv_line_set_points(time_line, time_line_points, 2);
    lv_obj_set_style_line_width(time_line, 2, LV_PART_MAIN);
    lv_obj_set_style_line_color(time_line, lv_color_hex(0x666666), LV_STATE_DEFAULT);
    lv_obj_align(time_line, LV_ALIGN_CENTER, 0, -10);

    // AM/PM indicator
    lv_obj_t *ampm_label = lv_label_create(main_screen);
    lv_label_set_text(ampm_label, "AM");
    lv_obj_set_style_text_color(ampm_label, lv_color_hex(0x333333), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ampm_label, &lv_font_montserrat_14, LV_STATE_DEFAULT);
    lv_obj_align(ampm_label, LV_ALIGN_CENTER, 100, -40);

    // Date display
    lv_obj_t *date_label = lv_label_create(main_screen);
    lv_label_set_text(date_label, "2025/07/17");
    lv_obj_set_style_text_color(date_label, lv_color_hex(0x666666), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(date_label, &lv_font_montserrat_14, LV_STATE_DEFAULT);
    lv_obj_align(date_label, LV_ALIGN_CENTER, 0, -10);

    // Notification status (center)
    lv_obj_t *notification_label = lv_label_create(main_screen);
    lv_label_set_text(notification_label, "YOU DON'T HAVE ANY NOTIFICATIONS");
    lv_obj_set_style_text_color(notification_label, lv_color_hex(0x666666), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(notification_label, &lv_font_montserrat_14, LV_STATE_DEFAULT);
    lv_obj_align(notification_label, LV_ALIGN_CENTER, 0, 30);

    // Speech bubble (bottom-left)
    lv_obj_t *speech_bubble = lv_obj_create(main_screen);
    lv_obj_set_size(speech_bubble, 200, 50);
    lv_obj_align(speech_bubble, LV_ALIGN_BOTTOM_LEFT, 10, -60);
    lv_obj_set_style_bg_color(speech_bubble, lv_color_white(), LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(speech_bubble, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(speech_bubble, 1, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(speech_bubble, 15, LV_STATE_DEFAULT);
    lv_obj_clear_flag(speech_bubble, LV_OBJ_FLAG_SCROLLABLE);

    // Speech text
    lv_obj_t *speech_text = lv_label_create(speech_bubble);
    lv_label_set_text(speech_text, "HOW CAN I\nHELP YOU TODAY?");
    lv_obj_set_style_text_color(speech_text, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(speech_text, &lv_font_montserrat_14, LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(speech_text, LV_TEXT_ALIGN_CENTER, LV_STATE_DEFAULT);
    lv_obj_center(speech_text);

    // Cat emoji (below speech bubble)
    lv_obj_t *cat_emoji = lv_label_create(main_screen);
    lv_label_set_text(cat_emoji, "😺");
    lv_obj_set_style_text_color(cat_emoji, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(cat_emoji, &lv_font_montserrat_14, LV_STATE_DEFAULT);
    lv_obj_align(cat_emoji, LV_ALIGN_BOTTOM_LEFT, 30, -30);

    // API connection status (bottom-right)
    lv_obj_t *api_status = lv_label_create(main_screen);
    lv_label_set_text(api_status, "● ALL API is Connected");
    lv_obj_set_style_text_color(api_status, lv_color_hex(0x4CAF50), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(api_status, &lv_font_montserrat_14, LV_STATE_DEFAULT);
    lv_obj_align(api_status, LV_ALIGN_BOTTOM_RIGHT, -10, -10);

    ESP_LOGI(TAG, "Fixed main screen layout to match design better");
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
    boot_logo_container = NULL;
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
    
    create_boot_animation();
    
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
    boot_logo_container = NULL;
    boot_screen = NULL;
    main_screen = NULL;
    
    ESP_LOGI(TAG, "Display system deinitialized successfully");
    return ESP_OK;
}