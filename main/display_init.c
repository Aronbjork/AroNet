#include "display_init.h"
#include "display_driver.h"
#include "touch_driver.h"
#include "screen_orientation.h"
#include "aronet_config.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include <string.h>

static const char *TAG = "DisplayInit";

bool display_initialize_system(void)
{
    ESP_LOGI(TAG, "Starting display system initialization...");

    ESP_LOGI(TAG, "Free SRAM: %zu, Free PSRAM: %zu",
             heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    lv_init();
    ESP_LOGI(TAG, "LVGL initialized");

    // Touch first (sets up shared I2C bus)
    touch_init();

    // Backlight (uses shared I2C bus from touch)
    display_init_backlight();
    display_set_backlight(100);
    ESP_LOGI(TAG, "Backlight at 100%%");

    // Display panel
    esp_lcd_panel_handle_t panel_handle = display_init_panel();
    if (panel_handle == NULL) {
        ESP_LOGE(TAG, "Failed to initialize display panel!");
        return false;
    }

    // LVGL display driver
    lv_display_t *disp = display_init_lvgl(panel_handle);
    if (disp == NULL) {
        ESP_LOGE(TAG, "Failed to initialize LVGL display driver!");
        return false;
    }
    lv_disp_set_default(disp);

    vTaskDelay(pdMS_TO_TICKS(200));

    // Screen orientation
    screen_orientation_init();

    // Touch input device
    touch_create_indev();

    ESP_LOGI(TAG, "Display system initialization complete");
    return true;
}
