#include "aronet_device_client.h"
#include "aronet_device_gui.h"
#include "lvgl.h"
#include "display_init.h"
#include "esp_log.h"
#include "esp_random.h"

#define POLL_INTERVAL_MS 3000
#define MAX_QUEUE_JOBS 50
#define MAX_INVENTORY_PARTS 50

static const char *TAG = "aronet_gui";

extern const lv_font_t font_multilang_small;

typedef enum {
    UI_CONNECTING,
    UI_IDLE,
    UI_QUEUE,
    UI_JOB_DETAILS,
    UI_INVENTORY_DETAILS,
    UI_JOB_RUNNING,
    UI_ERROR,
} ui_state_t;

static ui_state_t ui_state;
static aronet_job_t current_job;
static aronet_job_t queue_jobs[MAX_QUEUE_JOBS];
static uint32_t queue_job_count;
static aronet_part_t inventory_parts[MAX_INVENTORY_PARTS];
static uint32_t inventory_part_count;
static aronet_part_t current_part;
static uint32_t last_poll_ms;
static uint32_t active_tab_index;
static lv_obj_t *adjust_spinbox;
static lv_obj_t *number_keyboard;
static lv_font_t swedish_font_18;
static lv_font_t swedish_font_20;
static lv_font_t swedish_font_24;
static lv_font_t swedish_font_26;
static lv_font_t swedish_font_32;

static const char *const idle_messages[] = {
    "Good and happy chocolate biscuit",
    "It is never too late to give up",
    "One thing at a time, it will get done",
    "Good work starts with a good break",
    "Calm and methodical wins the day",
    "The next job waits when you are ready",
};

static void show_queue(uint32_t tab_index);
static void start_job_event(lv_event_t *event);

static void add_button_label(lv_obj_t *button, const char *text)
{
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_center(label);
}

static void style_list(lv_obj_t *list)
{
    lv_obj_set_style_bg_color(list, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(list, lv_color_hex(0x303030), LV_PART_MAIN);
    lv_obj_set_style_text_color(list, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
}

static void style_list_row(lv_obj_t *row)
{
    lv_obj_set_style_bg_color(row, lv_color_hex(0x111111), LV_PART_MAIN);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x242424), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(row, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_border_color(row, lv_color_hex(0x303030), LV_PART_MAIN);
}

static void init_swedish_fonts(void)
{
    swedish_font_18 = lv_font_montserrat_18;
    swedish_font_20 = lv_font_montserrat_20;
    swedish_font_24 = lv_font_montserrat_24;
    swedish_font_26 = lv_font_montserrat_26;
    swedish_font_32 = lv_font_montserrat_32;
    swedish_font_18.fallback = &font_multilang_small;
    swedish_font_20.fallback = &font_multilang_small;
    swedish_font_24.fallback = &font_multilang_small;
    swedish_font_26.fallback = &font_multilang_small;
    swedish_font_32.fallback = &font_multilang_small;
}

static void show_screen(const char *headline, const char *detail, uint32_t color)
{
    lv_obj_t *screen = lv_screen_active();
    lv_obj_clean(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x14213D), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "AroNet");
    lv_obj_set_style_text_font(title, &swedish_font_32, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_pos(title, 36, 26);

    lv_obj_t *status = lv_label_create(screen);
    lv_label_set_text(status, headline);
    lv_obj_set_style_text_font(status, &swedish_font_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(status, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_pos(status, 36, 100);

    lv_obj_t *description = lv_label_create(screen);
    lv_label_set_text(description, detail);
    lv_obj_set_style_text_font(description, &swedish_font_18, LV_PART_MAIN);
    lv_obj_set_style_text_color(description, lv_color_hex(0xD9E2F3), LV_PART_MAIN);
    lv_obj_set_width(description, 720);
    lv_obj_set_pos(description, 36, 155);
    lv_obj_invalidate(screen);
}

static void show_idle_content(lv_obj_t *parent)
{
    size_t message_count = sizeof(idle_messages) / sizeof(idle_messages[0]);
    const char *message = idle_messages[esp_random() % message_count];

    lv_obj_t *headline = lv_label_create(parent);
    lv_label_set_text(headline, "Ready for work");
    lv_obj_set_style_text_font(headline, &swedish_font_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(headline, lv_color_hex(0x65C466), LV_PART_MAIN);
    lv_obj_align(headline, LV_ALIGN_CENTER, 0, -30);
    lv_obj_t *detail = lv_label_create(parent);
    lv_label_set_text(detail, message);
    lv_obj_set_style_text_font(detail, &swedish_font_18, LV_PART_MAIN);
    lv_obj_set_style_text_color(detail, lv_color_hex(0xD9E2F3), LV_PART_MAIN);
    lv_obj_align(detail, LV_ALIGN_CENTER, 0, 24);
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
        last_poll_ms = 0;
        show_queue(0);
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
        show_queue(0);
    } else {
        ui_state = UI_ERROR;
        show_error(result);
    }
}

static void back_to_queue_event(lv_event_t *event)
{
    show_queue((uint32_t)(intptr_t)lv_event_get_user_data(event));
}

static void adjust_part_event(lv_event_t *event)
{
    int32_t amount = lv_spinbox_get_value(adjust_spinbox);
    int32_t change = (int32_t)(intptr_t)lv_event_get_user_data(event) * amount;
    aronet_error_t result = aronet_adjust_part(current_part.id, change, "Display stock adjustment");
    if (result == ARONET_OK) {
        current_part.quantity += change;
        show_queue(1);
    } else {
        ui_state = UI_ERROR;
        show_error(result);
    }
}

static void spinbox_increment_event(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_SHORT_CLICKED) {
        lv_spinbox_increment(adjust_spinbox);
    } else if (lv_event_get_code(event) == LV_EVENT_LONG_PRESSED_REPEAT) {
        lv_spinbox_set_value(adjust_spinbox, lv_spinbox_get_value(adjust_spinbox) + 10);
    }
}

static void spinbox_decrement_event(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_SHORT_CLICKED) {
        lv_spinbox_decrement(adjust_spinbox);
    } else if (lv_event_get_code(event) == LV_EVENT_LONG_PRESSED_REPEAT) {
        lv_spinbox_set_value(adjust_spinbox, lv_spinbox_get_value(adjust_spinbox) - 10);
    }
}

static void keyboard_event(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_READY || lv_event_get_code(event) == LV_EVENT_CANCEL) {
        lv_obj_del(number_keyboard);
        number_keyboard = NULL;
    }
}

static void open_number_keyboard_event(lv_event_t *event)
{
    (void)event;
    if (number_keyboard) {
        return;
    }
    number_keyboard = lv_keyboard_create(lv_screen_active());
    lv_keyboard_set_mode(number_keyboard, LV_KEYBOARD_MODE_NUMBER);
    lv_keyboard_set_textarea(number_keyboard, adjust_spinbox);
    lv_obj_set_size(number_keyboard, 800, 230);
    lv_obj_set_pos(number_keyboard, 0, 250);
    lv_obj_set_style_bg_color(number_keyboard, lv_color_hex(0x050505), LV_PART_MAIN);
    lv_obj_set_style_text_color(number_keyboard, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_bg_color(number_keyboard, lv_color_hex(0x202020), LV_PART_ITEMS);
    lv_obj_set_style_text_color(number_keyboard, lv_color_hex(0xFFFFFF), LV_PART_ITEMS);
    lv_obj_add_event_cb(number_keyboard, keyboard_event, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(number_keyboard, keyboard_event, LV_EVENT_CANCEL, NULL);
}

static void inventory_part_event(lv_event_t *event)
{
    current_part = *(aronet_part_t *)lv_event_get_user_data(event);
    ui_state = UI_INVENTORY_DETAILS;
    char detail[512];
    snprintf(detail, sizeof(detail), "Part number: %.31s\nName: %.63s\nDescription: %.255s\nIn stock: %lu %.15s",
             current_part.part_number, current_part.name, current_part.description,
             (unsigned long)current_part.quantity, current_part.unit);
    show_screen("Adjust inventory", detail, 0x5AA9E6);

    adjust_spinbox = lv_spinbox_create(lv_screen_active());
    lv_spinbox_set_range(adjust_spinbox, 1, 99999);
    lv_spinbox_set_digit_format(adjust_spinbox, 5, 0);
    lv_spinbox_set_value(adjust_spinbox, 1);
    lv_obj_set_size(adjust_spinbox, 180, 56);
    lv_obj_set_pos(adjust_spinbox, 36, 330);
    lv_obj_set_style_text_font(adjust_spinbox, &swedish_font_20, LV_PART_MAIN);
    lv_obj_add_event_cb(adjust_spinbox, open_number_keyboard_event, LV_EVENT_CLICKED, NULL);

    lv_obj_t *increase_button = lv_btn_create(lv_screen_active());
    lv_obj_set_size(increase_button, 68, 56);
    lv_obj_set_pos(increase_button, 228, 330);
    lv_obj_add_event_cb(increase_button, spinbox_increment_event, LV_EVENT_ALL, NULL);
    add_button_label(increase_button, "+");

    lv_obj_t *decrease_button = lv_btn_create(lv_screen_active());
    lv_obj_set_size(decrease_button, 68, 56);
    lv_obj_set_pos(decrease_button, 308, 330);
    lv_obj_add_event_cb(decrease_button, spinbox_decrement_event, LV_EVENT_ALL, NULL);
    add_button_label(decrease_button, "-");

    lv_obj_t *add_button = lv_btn_create(lv_screen_active());
    lv_obj_set_size(add_button, 180, 62);
    lv_obj_set_pos(add_button, 36, 398);
    lv_obj_set_style_bg_color(add_button, lv_color_hex(0x2E9E62), LV_PART_MAIN);
    lv_obj_add_event_cb(add_button, adjust_part_event, LV_EVENT_CLICKED, (void *)(intptr_t)1);
    add_button_label(add_button, "ADD STOCK");

    lv_obj_t *trim_button = lv_btn_create(lv_screen_active());
    lv_obj_set_size(trim_button, 180, 62);
    lv_obj_set_pos(trim_button, 238, 398);
    lv_obj_set_style_bg_color(trim_button, lv_color_hex(0xC94C4C), LV_PART_MAIN);
    lv_obj_add_event_cb(trim_button, adjust_part_event, LV_EVENT_CLICKED, (void *)(intptr_t)-1);
    add_button_label(trim_button, "DEDUCT STOCK");

    lv_obj_t *back_button = lv_btn_create(lv_screen_active());
    lv_obj_set_size(back_button, 220, 68);
    lv_obj_set_pos(back_button, 440, 398);
    lv_obj_set_style_bg_color(back_button, lv_color_hex(0x4D648D), LV_PART_MAIN);
    lv_obj_add_event_cb(back_button, back_to_queue_event, LV_EVENT_CLICKED, (void *)(intptr_t)1);
    add_button_label(back_button, "BACK");
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
    lv_obj_add_event_cb(back_button, back_to_queue_event, LV_EVENT_CLICKED, (void *)(intptr_t)0);
    label = lv_label_create(back_button);
    lv_label_set_text(label, "BACK TO QUEUE");
    lv_obj_center(label);
}

static void tab_changed_event(lv_event_t *event)
{
    active_tab_index = lv_tabview_get_tab_active(lv_event_get_target(event));
}

static void show_queue(uint32_t tab_index)
{
    aronet_error_t jobs_result = aronet_get_jobs("available", queue_jobs, MAX_QUEUE_JOBS, &queue_job_count);
    if (jobs_result == ARONET_ERR_NOT_FOUND) {
        queue_job_count = 0;
    } else if (jobs_result != ARONET_OK) {
        ui_state = UI_ERROR;
        show_error(jobs_result);
        return;
    }
    aronet_error_t parts_result = aronet_get_parts(inventory_parts, MAX_INVENTORY_PARTS,
                                                   &inventory_part_count);
    if (parts_result == ARONET_ERR_NOT_FOUND) {
        inventory_part_count = 0;
    } else if (parts_result != ARONET_OK) {
        ui_state = UI_ERROR;
        show_error(parts_result);
        return;
    }

    ui_state = UI_QUEUE;
    lv_obj_t *screen = lv_screen_active();
    lv_obj_clean(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x14213D), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *tabview = lv_tabview_create(screen);
    lv_obj_set_size(tabview, 800, 480);
    lv_obj_set_style_bg_color(tabview, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tabview, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_event_cb(tabview, tab_changed_event, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_t *jobs_tab = lv_tabview_add_tab(tabview, "Jobs");
    lv_obj_t *inventory_tab = lv_tabview_add_tab(tabview, "Inventory");
    lv_obj_set_style_bg_color(jobs_tab, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(jobs_tab, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(inventory_tab, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(inventory_tab, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_t *tab_bar = lv_tabview_get_tab_bar(tabview);
    lv_obj_set_style_bg_color(tab_bar, lv_color_hex(0x080808), LV_PART_MAIN);
    lv_obj_set_style_text_color(tab_bar, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    for (uint32_t index = 0; index < lv_obj_get_child_count(tab_bar); index++) {
        lv_obj_t *tab_button = lv_obj_get_child(tab_bar, index);
        lv_obj_set_style_bg_color(tab_button, lv_color_hex(0x080808), LV_PART_MAIN);
        lv_obj_set_style_bg_color(tab_button, lv_color_hex(0x242424), LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_set_style_text_color(tab_button, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    }

    lv_obj_t *list = lv_list_create(jobs_tab);
    lv_obj_set_size(list, 744, 350);
    lv_obj_center(list);
    style_list(list);
    for (uint32_t index = 0; index < queue_job_count; index++) {
        char text[256];
        snprintf(text, sizeof(text), "%.31s | %.63s | %lu pcs\n%.63s | %s | %lum elapsed",
                 queue_jobs[index].product_code, queue_jobs[index].operation_name,
             (unsigned long)queue_jobs[index].quantity, queue_jobs[index].batch_number,
                 queue_jobs[index].status == JOB_STATUS_PAUSED ? "Paused" : "Pending",
             (unsigned long)(queue_jobs[index].elapsed_seconds / 60));
        lv_obj_t *button = lv_list_add_button(list, NULL, text);
        style_list_row(button);
        lv_obj_add_event_cb(button, queue_event, LV_EVENT_CLICKED, &queue_jobs[index]);
    }

    if (!queue_job_count) {
        show_idle_content(jobs_tab);
    }

    lv_obj_t *inventory_list = lv_list_create(inventory_tab);
    lv_obj_set_size(inventory_list, 744, 350);
    lv_obj_center(inventory_list);
    style_list(inventory_list);
    for (uint32_t index = 0; index < inventory_part_count; index++) {
        char text[128];
        snprintf(text, sizeof(text), "%.31s | %.63s\nIn stock: %lu %s",
                 inventory_parts[index].part_number, inventory_parts[index].name,
                 (unsigned long)inventory_parts[index].quantity, inventory_parts[index].unit);
        lv_obj_t *button = lv_list_add_button(inventory_list, NULL, text);
        style_list_row(button);
        lv_obj_add_event_cb(button, inventory_part_event, LV_EVENT_CLICKED, &inventory_parts[index]);
    }
    if (!inventory_part_count) {
        lv_obj_t *empty_label = lv_label_create(inventory_tab);
        lv_label_set_text(empty_label, "No inventory parts found");
        lv_obj_center(empty_label);
    }

    active_tab_index = tab_index;
    lv_tabview_set_active(tabview, active_tab_index, LV_ANIM_OFF);

    lv_obj_invalidate(screen);
    ESP_LOGI(TAG, "Displaying %lu jobs and %lu inventory parts", (unsigned long)queue_job_count,
             (unsigned long)inventory_part_count);
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
    lv_obj_set_style_text_font(label, &swedish_font_20, LV_PART_MAIN);
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
    init_swedish_fonts();
    ui_state = UI_CONNECTING;
    show_screen("Connecting", "Joining Wi-Fi and contacting the AroNet server.", 0xF4B942);
    ESP_LOGI(TAG, "Connection screen rendered");
}

void aronet_gui_tick(void)
{
    uint32_t now = lv_tick_get();
    if (ui_state == UI_JOB_DETAILS || ui_state == UI_INVENTORY_DETAILS ||
        ui_state == UI_JOB_RUNNING || now - last_poll_ms < POLL_INTERVAL_MS) {
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
        show_queue(active_tab_index);
    } else if (result == ARONET_ERR_NOT_FOUND) {
        show_queue(active_tab_index);
    } else {
        ui_state = UI_ERROR;
        show_error(result);
        ESP_LOGW(TAG, "Job request failed: %s", aronet_error_string(result));
    }
}
