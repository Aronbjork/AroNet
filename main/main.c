#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "lvgl.h"
#include "display_init.h"
#include "aronet_config.h"

static const char *TAG = "AroNet";

static void lvgl_tick_cb(void *arg)
{
    lv_tick_inc(UI_LVGL_TICK_PERIOD_MS);
}

static void create_demo_ui(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1a1a2e), LV_PART_MAIN);

    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, "AroNet");
    lv_obj_set_style_text_color(label, lv_color_hex(0x2D9488), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_40, LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, -30);

    lv_obj_t *sub = lv_label_create(scr);
    lv_label_set_text(sub, "Touch display ready");
    lv_obj_set_style_text_color(sub, lv_color_hex(0x888888), LV_PART_MAIN);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 30);
}

void app_main(void)
{
    ESP_LOGI(TAG, "AroNet v%s starting...", FIRMWARE_VERSION_STRING);

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize display system (panel + LVGL + touch)
    if (!display_initialize_system()) {
        ESP_LOGE(TAG, "Display init failed!");
        return;
    }

    // Create LVGL tick timer
    const esp_timer_create_args_t tick_timer_args = {
        .callback = lvgl_tick_cb,
        .name = "lvgl_tick"
    };
    esp_timer_handle_t tick_timer;
    ESP_ERROR_CHECK(esp_timer_create(&tick_timer_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, UI_LVGL_TICK_PERIOD_MS * 1000));

    // Create demo UI
    lv_lock();
    create_demo_ui();
    lv_unlock();

    ESP_LOGI(TAG, "System ready");

    // Main LVGL loop
    while (1) {
        lv_lock();
        uint32_t time_till_next = lv_timer_handler();
        lv_unlock();

        if (time_till_next < UI_TASK_DELAY_MS) {
            time_till_next = UI_TASK_DELAY_MS;
        }
        vTaskDelay(pdMS_TO_TICKS(time_till_next));
    }
}
