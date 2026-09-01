#pragma once
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "lvgl.h"
#include "aronet_config.h"

#if DISPLAY_HW_VERSION >= 1
#include "driver/i2c_master.h"
extern i2c_master_dev_handle_t s_backlight_dev_handle;
#endif

#define EXAMPLE_LCD_BK_LIGHT_ON_LEVEL  1
#define EXAMPLE_LCD_BK_LIGHT_OFF_LEVEL 0

const char *display_get_name(void);
void display_init_backlight(void);
void display_set_backlight(uint32_t level);
uint32_t display_get_backlight(void);
esp_lcd_panel_handle_t display_init_panel(void);
lv_display_t *display_init_lvgl(esp_lcd_panel_handle_t panel_handle);
bool display_set_orientation_hardware(bool swap_xy, bool mirror_x, bool mirror_y);
esp_lcd_panel_handle_t display_get_panel_handle(void);
