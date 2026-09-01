#include "aronet_device_client.h"
#include "aronet_device_gui.h"
#include "lvgl.h"
#include "display_init.h"
#include "esp_log.h"

#define POLL_INTERVAL_MS 3000
#define MAX_QUEUE_JOBS 12

static const char *TAG = "aronet_gui";

typedef enum {
    UI_CONNECTING,
    UI_IDLE,
    UI_QUEUE,
    UI_JOB_DETAILS,
    UI_JOB_RUNNING,
    UI_ERROR,
} ui_state_t;

static ui_state_t ui_state;
static aronet_job_t current_job;
static aronet_job_t queue_jobs[MAX_QUEUE_JOBS];
static uint32_t queue_job_count;
static uint32_t last_poll_ms;

static void show_queue(void);
static void start_job_event(lv_event_t *event);

static void show_screen(const char *headline, const char *detail, uint32_t color)
{
    lv_obj_t *screen = lv_screen_active();
    lv_obj_clean(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x14213D), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "AroNet");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_32, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_pos(title, 36, 26);

    lv_obj_t *status = lv_label_create(screen);
    lv_label_set_text(status, headline);
    lv_obj_set_style_text_font(status, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(status, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_pos(status, 36, 100);

    lv_obj_t *description = lv_label_create(screen);
    lv_label_set_text(description, detail);
    lv_obj_set_style_text_font(description, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_set_style_text_color(description, lv_color_hex(0xD9E2F3), LV_PART_MAIN);
    lv_obj_set_width(description, 720);
    lv_obj_set_pos(description, 36, 155);
    lv_obj_invalidate(screen);
}

static void show_idle(void)
{
    show_screen("Ready for work", "Waiting for the next job from AroNet.", 0x65C466);
}

static void show_error(aronet_error_t error)
{
    char detail[128];
    snprintf(detail, sizeof(detail), "Server connection: %s\nThe display will retry automatically.",
             aronet_error_string(error));
    show_screen("Connection problem", detail, 0xE76F51);
}

static void complete_job_event(lv_event_t *event)
{
    (void)event;
    aronet_error_t result = aronet_complete_job(current_job.id);
    if (result == ARONET_OK) {
        aronet_update_device_status("idle");
        ui_state = UI_IDLE;
        show_idle();
        last_poll_ms = 0;
    } else {
        ui_state = UI_ERROR;
        show_error(result);
    }
}

static void pause_job_event(lv_event_t *event)
{
    (void)event;
    aronet_error_t result = aronet_pause_job(current_job.id);
    if (result == ARONET_OK) {
        aronet_update_device_status("idle");
        last_poll_ms = 0;
        show_queue();
    } else {
        ui_state = UI_ERROR;
        show_error(result);
    }
}

static void back_to_queue_event(lv_event_t *event)
{
    (void)event;
    show_queue();
}

static void queue_event(lv_event_t *event)
{
    current_job = *(aronet_job_t *)lv_event_get_user_data(event);
    ui_state = UI_JOB_DETAILS;

    char detail[400];
    snprintf(detail, sizeof(detail), "Product: %.31s\n%.127s\nOperation: %.63s\nBatch: %.63s\nQuantity: %lu\nPrevious time: %lum %lus",
             current_job.product_code, current_job.product_name, current_job.operation_name,
             current_job.batch_number, (unsigned long)current_job.quantity,
             (unsigned long)(current_job.elapsed_seconds / 60),
             (unsigned long)(current_job.elapsed_seconds % 60));
    show_screen("Job details", detail, 0xF4B942);

    lv_obj_t *start_button = lv_btn_create(lv_screen_active());
    lv_obj_set_size(start_button, 220, 68);
    lv_obj_set_pos(start_button, 36, 370);
    lv_obj_set_style_bg_color(start_button, lv_color_hex(0x2E9E62), LV_PART_MAIN);
    lv_obj_add_event_cb(start_button, start_job_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label = lv_label_create(start_button);
    lv_label_set_text(label, "START");
    lv_obj_center(label);

    lv_obj_t *back_button = lv_btn_create(lv_screen_active());
    lv_obj_set_size(back_button, 220, 68);
    lv_obj_set_pos(back_button, 290, 370);
    lv_obj_set_style_bg_color(back_button, lv_color_hex(0x4D648D), LV_PART_MAIN);
    lv_obj_add_event_cb(back_button, back_to_queue_event, LV_EVENT_CLICKED, NULL);
    label = lv_label_create(back_button);
    lv_label_set_text(label, "BACK TO QUEUE");
    lv_obj_center(label);
}

static void show_queue(void)
{
    aronet_error_t result = aronet_get_jobs("available", queue_jobs, MAX_QUEUE_JOBS, &queue_job_count);
    if (result == ARONET_ERR_NOT_FOUND) {
        ui_state = UI_IDLE;
        show_idle();
        return;
    }
    if (result != ARONET_OK) {
        ui_state = UI_ERROR;
        show_error(result);
        return;
    }

    ui_state = UI_QUEUE;
    lv_obj_t *screen = lv_screen_active();
    lv_obj_clean(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x14213D), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text_fmt(title, "AroNet Queue (%lu)", (unsigned long)queue_job_count);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_26, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_pos(title, 28, 20);

    lv_obj_t *list = lv_list_create(screen);
    lv_obj_set_size(list, 744, 388);
    lv_obj_set_pos(list, 28, 76);
    for (uint32_t index = 0; index < queue_job_count; index++) {
        char text[256];
        snprintf(text, sizeof(text), "%.31s | %.63s | %lu pcs\n%.63s | %s | %lum elapsed",
                 queue_jobs[index].product_code, queue_jobs[index].operation_name,
             (unsigned long)queue_jobs[index].quantity, queue_jobs[index].batch_number,
             queue_jobs[index].status == JOB_STATUS_PAUSED ? "Paused" : "Pending",
             (unsigned long)(queue_jobs[index].elapsed_seconds / 60));
        lv_obj_t *button = lv_list_add_button(list, NULL, text);
        lv_obj_add_event_cb(button, queue_event, LV_EVENT_CLICKED, &queue_jobs[index]);
    }
    lv_obj_invalidate(screen);
    ESP_LOGI(TAG, "Displaying %lu queued jobs", (unsigned long)queue_job_count);
}

static void start_job_event(lv_event_t *event)
{
    (void)event;
    aronet_error_t result = aronet_assign_job(current_job.id);
    if (result == ARONET_OK) {
        result = aronet_start_job(current_job.id);
    }
    if (result != ARONET_OK) {
        ui_state = UI_ERROR;
        show_error(result);
        return;
    }

    aronet_update_device_status("working");
    ui_state = UI_JOB_RUNNING;
    show_screen("Job in progress", current_job.operation_name, 0x5AA9E6);
    lv_obj_t *button = lv_btn_create(lv_screen_active());
    lv_obj_set_size(button, 320, 76);
    lv_obj_align(button, LV_ALIGN_BOTTOM_MID, 0, -42);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x2E9E62), LV_PART_MAIN);
    lv_obj_add_event_cb(button, complete_job_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, "COMPLETE JOB");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_center(label);

    lv_obj_t *cancel_button = lv_btn_create(lv_screen_active());
    lv_obj_set_size(cancel_button, 190, 60);
    lv_obj_set_pos(cancel_button, 570, 30);
    lv_obj_set_style_bg_color(cancel_button, lv_color_hex(0xC94C4C), LV_PART_MAIN);
    lv_obj_add_event_cb(cancel_button, pause_job_event, LV_EVENT_CLICKED, NULL);
    label = lv_label_create(cancel_button);
    lv_label_set_text(label, "PAUSE JOB");
    lv_obj_center(label);
}

void aronet_gui_init(void)
{
    ui_state = UI_CONNECTING;
    show_screen("Connecting", "Joining Wi-Fi and contacting the AroNet server.", 0xF4B942);
    ESP_LOGI(TAG, "Connection screen rendered");
}

void aronet_gui_tick(void)
{
    uint32_t now = lv_tick_get();
    if (ui_state == UI_JOB_DETAILS || ui_state == UI_JOB_RUNNING || now - last_poll_ms < POLL_INTERVAL_MS) {
        return;
    }
    last_poll_ms = now;
    if (!aronet_is_connected()) {
        if (ui_state != UI_CONNECTING) {
            ui_state = UI_CONNECTING;
            show_screen("Connecting", "Waiting for the Wi-Fi connection.", 0xF4B942);
        }
        return;
    }

    aronet_error_t result = aronet_get_jobs("available", queue_jobs, MAX_QUEUE_JOBS, &queue_job_count);
    if (result == ARONET_OK) {
        show_queue();
    } else if (result == ARONET_ERR_NOT_FOUND) {
        if (ui_state != UI_IDLE) {
            ui_state = UI_IDLE;
            show_idle();
            ESP_LOGI(TAG, "No pending job; displaying idle screen");
        }
    } else {
        ui_state = UI_ERROR;
        show_error(result);
        ESP_LOGW(TAG, "Job request failed: %s", aronet_error_string(result));
    }
}
