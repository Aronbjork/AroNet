#include "screen_orientation.h"
#include "display_driver.h"
#include "esp_log.h"
#include "lvgl.h"

static const char *TAG = "ScreenOrientation";

static screen_orientation_t s_current_orientation = SCREEN_ORIENTATION_STANDARD;
static bool s_initialized = false;

bool screen_orientation_init(void)
{
    s_current_orientation = SCREEN_ORIENTATION_STANDARD;
    s_initialized = true;
    ESP_LOGI(TAG, "Screen orientation system initialized");
    return true;
}

bool screen_orientation_set(screen_orientation_t orientation)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Screen orientation system not initialized");
        return false;
    }

    lv_display_t *disp = lv_display_get_default();
    if (!disp) {
        ESP_LOGE(TAG, "No default display found");
        return false;
    }

    bool swap_xy = false, mirror_x = false, mirror_y = false;

    if (orientation == SCREEN_ORIENTATION_FLIPPED) {
        mirror_x = true;
        mirror_y = true;
    }

    if (!display_set_orientation_hardware(swap_xy, mirror_x, mirror_y)) {
        ESP_LOGE(TAG, "Failed to set hardware orientation");
        return false;
    }

    s_current_orientation = orientation;
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(disp);

    ESP_LOGI(TAG, "Screen orientation set to: %s",
             orientation == SCREEN_ORIENTATION_STANDARD ? "Standard (0°)" : "Flipped (180°)");
    return true;
}

screen_orientation_t screen_orientation_get_current(void)
{
    return s_current_orientation;
}
