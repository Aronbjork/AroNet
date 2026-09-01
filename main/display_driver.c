#include "display_driver.h"
#include "touch_driver.h"
#include "esp_lcd_panel_ops.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hw_version.h"

static const char *TAG = "display";

static esp_lcd_panel_handle_t g_panel_handle = NULL;
static uint32_t s_vsync_count = 0;

// Display buffer configuration
#define EXAMPLE_LCD_NUM_FB           1
#define EXAMPLE_DATA_BUS_WIDTH       16
#define EXAMPLE_LV_COLOR_FORMAT      LV_COLOR_FORMAT_RGB565
#define EXAMPLE_PIXEL_SIZE           2
#define EXAMPLE_LVGL_DRAW_BUF_LINES 120

// CrowPanel 5 Advance V1.0 configuration
#define DISPLAY_NAME                "CrowPanel 5 Advance V1.0"
#define DISPLAY_PIXEL_CLOCK_HZ      18000000
#define DISPLAY_H_RES               800
#define DISPLAY_V_RES               480
#define DISPLAY_HSYNC_PULSE_WIDTH   4
#define DISPLAY_HSYNC_BACK_PORCH    43
#define DISPLAY_HSYNC_FRONT_PORCH   8
#define DISPLAY_VSYNC_PULSE_WIDTH   4
#define DISPLAY_VSYNC_BACK_PORCH    12
#define DISPLAY_VSYNC_FRONT_PORCH   8

// Pin definitions - CrowPanel 5 Advance V1.0
#define PIN_PCLK    39
#define PIN_HSYNC   40
#define PIN_VSYNC   41
#define PIN_DE      42
#define PIN_D0      21  // B0
#define PIN_D1      47  // B1
#define PIN_D2      48  // B2
#define PIN_D3      45  // B3
#define PIN_D4      38  // B4
#define PIN_D5      9   // G0
#define PIN_D6      10  // G1
#define PIN_D7      11  // G2
#define PIN_D8      12  // G3
#define PIN_D9      13  // G4
#define PIN_D10     14  // G5
#define PIN_D11     7   // R0
#define PIN_D12     17  // R1
#define PIN_D13     18  // R2
#define PIN_D14     3   // R3
#define PIN_D15     46  // R4

#include "aronet_config.h"

// V1.0: TCA9534 I/O Expander
#define TCA9534_I2C_ADDR    0x18
#define TCA9534_REG_INPUT   0x00
#define TCA9534_REG_OUTPUT  0x01
#define TCA9534_REG_POLARITY 0x02
#define TCA9534_REG_CONFIG  0x03
#define TCA9534_BL_PIN      1

// V1.1: STC8H1K28 microcontroller
#define STC8H1K28_I2C_ADDR  0x30
#define STC_BL_OFF          0x05
#define STC_BL_20           0x06
#define STC_BL_40           0x07
#define STC_BL_60           0x08
#define STC_BL_80           0x09
#define STC_BL_100          0x10

// V1.2/V1.3: STC8H1K28 updated protocol
#define STC_BL_V2_MAX       0
#define STC_BL_V2_MIN       244
#define STC_BL_V2_OFF       245

static bool s_backlight_initialized = false;
i2c_master_dev_handle_t s_backlight_dev_handle = NULL;
static uint32_t s_current_backlight_level = 100;

const char *display_get_name(void)
{
    return DISPLAY_NAME;
}

#if DISPLAY_HW_VERSION == 0
static esp_err_t tca9534_write_reg(uint8_t reg, uint8_t value)
{
    if (s_backlight_dev_handle == NULL) {
        ESP_LOGE(TAG, "TCA9534 device handle not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t write_buf[2] = {reg, value};
    return i2c_master_transmit(s_backlight_dev_handle, write_buf, sizeof(write_buf), pdMS_TO_TICKS(100));
}

static esp_err_t tca9534_read_reg(uint8_t reg, uint8_t *value)
{
    if (s_backlight_dev_handle == NULL) {
        ESP_LOGE(TAG, "TCA9534 device handle not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_master_transmit_receive(s_backlight_dev_handle, &reg, 1, value, 1, pdMS_TO_TICKS(100));
}
#endif

#if DISPLAY_HW_VERSION >= 1
static esp_err_t stc8h1k28_set_brightness(uint8_t brightness_cmd)
{
    if (s_backlight_dev_handle == NULL) {
        ESP_LOGE(TAG, "STC8H1K28 device handle not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_master_transmit(s_backlight_dev_handle, &brightness_cmd, 1, pdMS_TO_TICKS(100));
}
#endif

#if DISPLAY_HW_VERSION >= 1
static uint8_t percentage_to_stc_brightness(uint32_t level)
{
    if (hw_version_get_stc_protocol() == STC_PROTOCOL_V1_1) {
        if (level == 0) return STC_BL_OFF;
        if (level <= 20) return STC_BL_20;
        if (level <= 40) return STC_BL_40;
        if (level <= 60) return STC_BL_60;
        if (level <= 80) return STC_BL_80;
        return STC_BL_100;
    }

    // v1.2/v1.3: 0=max, 244=min, 245=off
    if (level == 0) return STC_BL_V2_OFF;
    if (level > 100) level = 100;
    uint32_t inverted = 100 - level;
    return (uint8_t)((inverted * STC_BL_V2_MIN) / 100);
}
#endif

void display_init_backlight(void)
{
    if (s_backlight_initialized) {
        ESP_LOGW(TAG, "Backlight already initialized");
        return;
    }

#if DISPLAY_HW_VERSION == 0
    ESP_LOGI(TAG, "Initializing TCA9534 I/O expander for backlight control (v1.0)");

    i2c_master_bus_handle_t i2c_bus_handle = touch_get_i2c_bus_handle();
    if (i2c_bus_handle == NULL) {
        ESP_LOGE(TAG, "I2C bus not initialized by touch driver");
        return;
    }

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = TCA9534_I2C_ADDR,
        .scl_speed_hz = 400000,
    };

    esp_err_t ret = i2c_master_bus_add_device(i2c_bus_handle, &dev_config, &s_backlight_dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add TCA9534 device: %s", esp_err_to_name(ret));
        return;
    }

    uint8_t config_reg = 0xFF;
    ret = tca9534_read_reg(TCA9534_REG_CONFIG, &config_reg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read TCA9534 config: %s", esp_err_to_name(ret));
        return;
    }

    config_reg &= ~(1 << TCA9534_BL_PIN);
    ret = tca9534_write_reg(TCA9534_REG_CONFIG, config_reg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure TCA9534: %s", esp_err_to_name(ret));
        return;
    }

    uint8_t output_reg = 0x00;
    ret = tca9534_read_reg(TCA9534_REG_OUTPUT, &output_reg);
    if (ret == ESP_OK) {
        output_reg |= (1 << TCA9534_BL_PIN);
        ret = tca9534_write_reg(TCA9534_REG_OUTPUT, output_reg);
    }

    if (ret == ESP_OK) {
        s_backlight_initialized = true;
        ESP_LOGI(TAG, "TCA9534 backlight initialized (v1.0)");
    } else {
        ESP_LOGE(TAG, "Failed to init TCA9534 backlight: %s", esp_err_to_name(ret));
    }
#elif DISPLAY_HW_VERSION >= 1
    uint8_t stc_protocol = hw_version_get_stc_protocol();
    ESP_LOGI(TAG, "Initializing STC8H1K28 backlight (v1.%s)",
             stc_protocol == STC_PROTOCOL_V1_1 ? "1" : "2/1.3");

    i2c_master_bus_handle_t i2c_bus_handle = touch_get_i2c_bus_handle();
    if (i2c_bus_handle == NULL) {
        ESP_LOGE(TAG, "I2C bus not initialized by touch driver");
        return;
    }

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = STC8H1K28_I2C_ADDR,
        .scl_speed_hz = 400000,
    };

    esp_err_t ret = i2c_master_bus_add_device(i2c_bus_handle, &dev_config, &s_backlight_dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add STC8H1K28 device: %s", esp_err_to_name(ret));
        return;
    }

    // Send both protocols' max brightness for boot safety
    stc8h1k28_set_brightness(STC_BL_100);
    vTaskDelay(pdMS_TO_TICKS(20));
    ret = stc8h1k28_set_brightness(STC_BL_V2_MAX);

    if (ret == ESP_OK) {
        s_backlight_initialized = true;
        ESP_LOGI(TAG, "STC8H1K28 backlight initialized at 100%%");
    } else {
        ESP_LOGE(TAG, "Failed to init STC8H1K28 backlight: %s", esp_err_to_name(ret));
    }
#endif
}

void display_set_backlight(uint32_t level)
{
    if (!s_backlight_initialized) {
        ESP_LOGW(TAG, "Backlight not initialized, initializing now...");
        display_init_backlight();
        if (!s_backlight_initialized) {
            ESP_LOGE(TAG, "Failed to initialize backlight");
            return;
        }
    }

    s_current_backlight_level = level;

#if DISPLAY_HW_VERSION == 0
    ESP_LOGI(TAG, "Setting TCA9534 backlight to %lu%%", level);
    uint8_t output_reg = 0x00;
    esp_err_t ret = tca9534_read_reg(TCA9534_REG_OUTPUT, &output_reg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read TCA9534 output: %s", esp_err_to_name(ret));
        return;
    }
    if (level > 0) {
        output_reg |= (1 << TCA9534_BL_PIN);
    } else {
        output_reg &= ~(1 << TCA9534_BL_PIN);
    }
    tca9534_write_reg(TCA9534_REG_OUTPUT, output_reg);
#elif DISPLAY_HW_VERSION >= 1
    uint8_t brightness_cmd = percentage_to_stc_brightness(level);
    ESP_LOGI(TAG, "Setting STC8H1K28 backlight to %lu%% (cmd: 0x%02X)", level, brightness_cmd);
    stc8h1k28_set_brightness(brightness_cmd);
#endif
}

uint32_t display_get_backlight(void)
{
    return s_current_backlight_level;
}

esp_lcd_panel_handle_t display_init_panel(void)
{
    ESP_LOGI(TAG, "Initializing %s", DISPLAY_NAME);
    vTaskDelay(pdMS_TO_TICKS(250));

    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_rgb_panel_config_t panel_config = {
        .data_width = EXAMPLE_DATA_BUS_WIDTH,
        .dma_burst_size = 32,
        .num_fbs = EXAMPLE_LCD_NUM_FB,
        .bounce_buffer_size_px = 20 * DISPLAY_H_RES,
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .disp_gpio_num = -1,
        .pclk_gpio_num = PIN_PCLK,
        .vsync_gpio_num = PIN_VSYNC,
        .hsync_gpio_num = PIN_HSYNC,
        .de_gpio_num = PIN_DE,
        .data_gpio_nums = {
            PIN_D0,  PIN_D1,  PIN_D2,  PIN_D3,
            PIN_D4,  PIN_D5,  PIN_D6,  PIN_D7,
            PIN_D8,  PIN_D9,  PIN_D10, PIN_D11,
            PIN_D12, PIN_D13, PIN_D14, PIN_D15,
        },
        .timings = {
            .pclk_hz = DISPLAY_PIXEL_CLOCK_HZ,
            .h_res = DISPLAY_H_RES,
            .v_res = DISPLAY_V_RES,
            .hsync_pulse_width = DISPLAY_HSYNC_PULSE_WIDTH,
            .hsync_back_porch = DISPLAY_HSYNC_BACK_PORCH,
            .hsync_front_porch = DISPLAY_HSYNC_FRONT_PORCH,
            .vsync_pulse_width = DISPLAY_VSYNC_PULSE_WIDTH,
            .vsync_back_porch = DISPLAY_VSYNC_BACK_PORCH,
            .vsync_front_porch = DISPLAY_VSYNC_FRONT_PORCH,
            .flags.hsync_idle_low = 1,
            .flags.vsync_idle_low = 1,
            .flags.de_idle_high = 0,
            .flags.pclk_active_neg = 1,
            .flags.pclk_idle_high = 0,
        },
        .flags.fb_in_psram = true,
    };

    esp_err_t ret = esp_lcd_new_rgb_panel(&panel_config, &panel_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install RGB LCD panel driver: %s", esp_err_to_name(ret));
        return NULL;
    }

    g_panel_handle = panel_handle;

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    vTaskDelay(pdMS_TO_TICKS(50));

    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    vTaskDelay(pdMS_TO_TICKS(100));

    display_set_backlight(100);
    ESP_LOGI(TAG, "ST7262 display controller initialized");

    return panel_handle;
}

static void display_lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    esp_lcd_panel_handle_t panel_handle = lv_display_get_user_data(disp);
    esp_lcd_panel_draw_bitmap(panel_handle, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
    lv_display_flush_ready(disp);
}

static bool display_notify_lvgl_flush_ready(esp_lcd_panel_handle_t panel,
                                            const esp_lcd_rgb_panel_event_data_t *event_data,
                                            void *user_ctx)
{
    s_vsync_count++;
    return false;
}

lv_display_t *display_init_lvgl(esp_lcd_panel_handle_t panel_handle)
{
    if (!panel_handle) {
        ESP_LOGE(TAG, "Invalid panel handle");
        return NULL;
    }

    lv_display_t *display = lv_display_create(DISPLAY_H_RES, DISPLAY_V_RES);
    if (!display) {
        ESP_LOGE(TAG, "Failed to create LVGL display");
        return NULL;
    }

    lv_display_set_user_data(display, panel_handle);
    lv_display_set_color_format(display, EXAMPLE_LV_COLOR_FORMAT);

    size_t draw_buffer_size = DISPLAY_H_RES * EXAMPLE_LVGL_DRAW_BUF_LINES * EXAMPLE_PIXEL_SIZE;
    void *buffer_one = heap_caps_aligned_alloc(32, draw_buffer_size,
                                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    void *buffer_two = heap_caps_aligned_alloc(32, draw_buffer_size,
                                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buffer_one || !buffer_two) {
        ESP_LOGE(TAG, "Failed to allocate LVGL draw buffers");
        free(buffer_one);
        free(buffer_two);
        return NULL;
    }

    memset(buffer_one, 0, draw_buffer_size);
    memset(buffer_two, 0, draw_buffer_size);
    lv_display_set_buffers(display, buffer_one, buffer_two, draw_buffer_size,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, display_lvgl_flush_cb);

    esp_lcd_rgb_panel_event_callbacks_t callbacks = {
        .on_vsync = display_notify_lvgl_flush_ready,
    };
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_register_event_callbacks(panel_handle, &callbacks, NULL));

    ESP_LOGI(TAG, "Display initialized: partial RGB draw-buffer mode");
    return display;
}

bool display_set_orientation_hardware(bool swap_xy, bool mirror_x, bool mirror_y)
{
    if (g_panel_handle == NULL) {
        ESP_LOGE(TAG, "Panel handle not available");
        return false;
    }

    esp_err_t ret = esp_lcd_panel_swap_xy(g_panel_handle, swap_xy);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set XY swap: %s", esp_err_to_name(ret));
        return false;
    }

    ret = esp_lcd_panel_mirror(g_panel_handle, mirror_x, mirror_y);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set mirroring: %s", esp_err_to_name(ret));
        return false;
    }

    ESP_LOGI(TAG, "Hardware orientation set: swap_xy=%s, mirror_x=%s, mirror_y=%s",
             swap_xy ? "true" : "false",
             mirror_x ? "true" : "false",
             mirror_y ? "true" : "false");
    return true;
}

esp_lcd_panel_handle_t display_get_panel_handle(void)
{
    return g_panel_handle;
}
