#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "lvgl.h"
#include "display_init.h"
#include "aronet_config.h"
#include "aronet_device_client.h"
#include "aronet_device_gui.h"

static const char *TAG = "AroNet";

static void lvgl_tick_cb(void *arg)
{
    lv_tick_inc(UI_LVGL_TICK_PERIOD_MS);
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

    ret = aronet_wifi_connect(CONFIG_ARONET_WIFI_SSID, CONFIG_ARONET_WIFI_PASSWORD);
    if (ret != ARONET_OK) {
        ESP_LOGW(TAG, "Wi-Fi is not configured; open menuconfig before deploying");
    }
    ESP_ERROR_CHECK(aronet_device_init(CONFIG_ARONET_SERVER_IP, 5000, CONFIG_ARONET_DEVICE_ID) == ARONET_OK
                    ? ESP_OK : ESP_FAIL);

    lv_lock();
    aronet_gui_init();
    lv_unlock();

    ESP_LOGI(TAG, "System ready");

    // Main LVGL loop
    while (1) {
        lv_lock();
        aronet_gui_tick();
        lv_unlock();

        lv_lock();
        uint32_t time_till_next = lv_timer_handler();
        lv_unlock();

        if (time_till_next < UI_TASK_DELAY_MS) {
            time_till_next = UI_TASK_DELAY_MS;
        }
        vTaskDelay(pdMS_TO_TICKS(time_till_next));
    }
}
