#include "aronet_device_client.h"
#include "aronet_device_gui.h"
#include "lvgl.h"
#include "display_init.h"

#define POLL_INTERVAL_MS 3000

typedef enum {
    UI_CONNECTING,
    UI_IDLE,
    UI_JOB_READY,
    UI_JOB_RUNNING,
    UI_ERROR,
} ui_state_t;

static ui_state_t ui_state;
static aronet_job_t current_job;
static uint32_t last_poll_ms;

static void show_screen(const char *headline, const char *detail, uint32_t color)
{
    lv_obj_t *screen = lv_screen_active();
    lv_obj_clean(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x14213D), LV_PART_MAIN);

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
}

static void show_job(void)
{
    char detail[256];
    snprintf(detail, sizeof(detail), "Product: %s\nOperation: %s\nBatch: %s\nQuantity: %lu",
             current_job.product_code, current_job.operation_name, current_job.batch_number,
             (unsigned long)current_job.quantity);
    show_screen("New job available", detail, 0xF4B942);

    lv_obj_t *button = lv_btn_create(lv_screen_active());
    lv_obj_set_size(button, 320, 76);
    lv_obj_align(button, LV_ALIGN_BOTTOM_MID, 0, -42);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x2E9E62), LV_PART_MAIN);
    lv_obj_add_event_cb(button, start_job_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, "START JOB");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_center(label);
}

void aronet_gui_init(void)
{
    ui_state = UI_CONNECTING;
    show_screen("Connecting", "Joining Wi-Fi and contacting the AroNet server.", 0xF4B942);
}

void aronet_gui_tick(void)
{
    uint32_t now = lv_tick_get();
    if (ui_state == UI_JOB_READY || ui_state == UI_JOB_RUNNING || now - last_poll_ms < POLL_INTERVAL_MS) {
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

    aronet_job_t next_job;
    aronet_error_t result = aronet_get_next_job(&next_job);
    if (result == ARONET_OK) {
        current_job = next_job;
        ui_state = UI_JOB_READY;
        show_job();
    } else if (result == ARONET_ERR_NOT_FOUND) {
        if (ui_state != UI_IDLE) {
            ui_state = UI_IDLE;
            show_idle();
        }
    } else {
        ui_state = UI_ERROR;
        show_error(result);
    }
}
