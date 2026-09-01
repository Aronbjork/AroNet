/**
 * AroNet ESP32 Device Integration Example
 * 
 * This file shows how to integrate the device client into your main.c
 * Add these functions to your existing main.c in the AroNet project
 */

#include "aronet_device_client.h"
#include "lvgl.h"
#include "display_init.h"
#include "touch_driver.h"

// Forward declarations
static void aronet_gui_update_status(void);
static void aronet_gui_show_job(const aronet_job_t *job);
static void aronet_gui_show_error(const char *error);

// ============ STATE ============

typedef enum {
    APP_STATE_CONNECT,
    APP_STATE_IDLE,
    APP_STATE_SHOWING_JOB,
    APP_STATE_JOB_STARTED,
    APP_STATE_JOB_COMPLETE,
    APP_STATE_ERROR
} app_state_t;

static app_state_t app_state = APP_STATE_CONNECT;
static aronet_device_status_t device_status;
static aronet_job_t current_job;
static char error_message[128];

// ============ LVGL WIDGETS ============

static lv_obj_t *label_status;
static lv_obj_t *label_job_product;
static lv_obj_t *label_job_operation;
static lv_obj_t *label_job_time;
static lv_obj_t *btn_start_job;
static lv_obj_t *btn_complete_job;
static lv_obj_t *btn_skip_job;
static lv_obj_t *label_error;

// ============ GUI SCREEN BUILDERS ============

/**
 * Screen 1: Connection Status
 */
static void screen_connection(void) {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_clean(scr);
    
    // Center label
    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, "Connecting to\nAroNet Server...");
    lv_obj_center(label);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_28, 0);
}

/**
 * Screen 2: Idle - Waiting for jobs
 */
static void screen_idle(void) {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_clean(scr);
    
    // Title
    lv_obj_t *label_title = lv_label_create(scr);
    lv_label_set_text(label_title, "AroNet Manufacturing");
    lv_obj_set_pos(label_title, 10, 10);
    lv_obj_set_style_text_font(label_title, &lv_font_montserrat_20, 0);
    
    // Status
    label_status = lv_label_create(scr);
    lv_label_set_text(label_status, "Status: Idle");
    lv_obj_set_pos(label_status, 10, 50);
    
    // Instructions
    lv_obj_t *label_info = lv_label_create(scr);
    lv_label_set_text(label_info, "Waiting for jobs...");
    lv_obj_set_pos(label_info, 10, 100);
    lv_obj_set_style_text_font(label_info, &lv_font_montserrat_16, 0);
}

/**
 * Screen 3: Job Available - Ask user to start
 */
static void screen_job_available(void) {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_clean(scr);
    
    // Title
    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, "Job Available!");
    lv_obj_set_pos(label, 10, 20);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
    
    // Product info
    label_job_product = lv_label_create(scr);
    lv_label_set_text_fmt(label_job_product, "Product: %s", current_job.product_code);
    lv_obj_set_pos(label_job_product, 10, 70);
    lv_obj_set_width(label_job_product, 780);
    
    // Operation info
    label_job_operation = lv_label_create(scr);
    lv_label_set_text_fmt(label_job_operation, "Operation: %s", current_job.operation_name);
    lv_obj_set_pos(label_job_operation, 10, 120);
    lv_obj_set_width(label_job_operation, 780);
    
    // Estimated time
    label_job_time = lv_label_create(scr);
    lv_label_set_text_fmt(label_job_time, "Batch: %s", current_job.batch_number);
    lv_obj_set_pos(label_job_time, 10, 170);
    lv_obj_set_width(label_job_time, 780);
    
    // Buttons
    btn_start_job = lv_btn_create(scr);
    lv_obj_set_size(btn_start_job, 200, 60);
    lv_obj_set_pos(btn_start_job, 50, 350);
    lv_obj_set_style_bg_color(btn_start_job, lv_color_hex(0x28a745), 0);
    lv_obj_add_event_cb(btn_start_job, event_start_job, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *label_btn = lv_label_create(btn_start_job);
    lv_label_set_text(label_btn, "START JOB");
    lv_obj_center(label_btn);
    
    btn_skip_job = lv_btn_create(scr);
    lv_obj_set_size(btn_skip_job, 200, 60);
    lv_obj_set_pos(btn_skip_job, 550, 350);
    lv_obj_set_style_bg_color(btn_skip_job, lv_color_hex(0x6c757d), 0);
    lv_obj_add_event_cb(btn_skip_job, event_skip_job, LV_EVENT_CLICKED, NULL);
    
    label_btn = lv_label_create(btn_skip_job);
    lv_label_set_text(label_btn, "SKIP");
    lv_obj_center(label_btn);
}

/**
 * Screen 4: Job In Progress
 */
static void screen_job_in_progress(void) {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_clean(scr);
    
    // Product
    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text_fmt(label, "▶ %s", current_job.product_code);
    lv_obj_set_pos(label, 10, 50);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_32, 0);
    
    // Operation
    label = lv_label_create(scr);
    lv_label_set_text_fmt(label, "Operation: %s", current_job.operation_name);
    lv_obj_set_pos(label, 10, 120);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    
    // Timer (you'd update this periodically)
    label = lv_label_create(scr);
    lv_label_set_text(label, "Time: 00:00");
    lv_obj_set_pos(label, 10, 170);
    
    // Complete button
    btn_complete_job = lv_btn_create(scr);
    lv_obj_set_size(btn_complete_job, 300, 80);
    lv_obj_center_x(btn_complete_job);
    lv_obj_set_y(btn_complete_job, 350);
    lv_obj_set_style_bg_color(btn_complete_job, lv_color_hex(0x0066cc), 0);
    lv_obj_add_event_cb(btn_complete_job, event_complete_job, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *label_btn = lv_label_create(btn_complete_job);
    lv_label_set_text(label_btn, "MARK COMPLETE");
    lv_obj_set_style_text_font(label_btn, &lv_font_montserrat_20, 0);
    lv_obj_center(label_btn);
}

// ============ BUTTON EVENT HANDLERS ============

static void event_start_job(lv_event_t *e) {
    aronet_error_t err = aronet_start_job(current_job.id);
    if (err == ARONET_OK) {
        app_state = APP_STATE_JOB_STARTED;
        screen_job_in_progress();
    } else {
        snprintf(error_message, sizeof(error_message), "Error: %s", aronet_error_string(err));
        app_state = APP_STATE_ERROR;
        aronet_gui_show_error(error_message);
    }
}

static void event_skip_job(lv_event_t *e) {
    app_state = APP_STATE_IDLE;
    screen_idle();
}

static void event_complete_job(lv_event_t *e) {
    aronet_error_t err = aronet_complete_job(current_job.id);
    if (err == ARONET_OK) {
        app_state = APP_STATE_IDLE;
        screen_idle();
        // Refresh job queue
        aronet_get_next_job(&current_job);
        if (current_job.id > 0) {
            app_state = APP_STATE_SHOWING_JOB;
            screen_job_available();
        }
    } else {
        snprintf(error_message, sizeof(error_message), "Error: %s", aronet_error_string(err));
        app_state = APP_STATE_ERROR;
        aronet_gui_show_error(error_message);
    }
}

// ============ MAIN LOOP INTEGRATION ============

/**
 * Initialize AroNet device system
 * Call this from your main() or app_init()
 * 
 * @param device_id Unique ID for this display (e.g., "DISPLAY-01")
 * @param server_ip IP of AroNet backend server
 */
void aronet_device_setup(const char *device_id, const char *server_ip) {
    aronet_error_t err = aronet_device_init(server_ip, 5000, device_id);
    if (err != ARONET_OK) {
        snprintf(error_message, sizeof(error_message), "Init error: %s", aronet_error_string(err));
        app_state = APP_STATE_ERROR;
    } else {
        app_state = APP_STATE_CONNECT;
    }
    
    // Show connection screen
    screen_connection();
}

/**
 * Main update loop
 * Call this regularly (10-20 times per second from your LVGL tick)
 * Or from a separate FreeRTOS task
 */
void aronet_device_update(void) {
    // Sync with server every few seconds
    aronet_sync_tick();
    
    // Update GUI based on state
    switch (app_state) {
        case APP_STATE_CONNECT:
            if (aronet_is_connected()) {
                app_state = APP_STATE_IDLE;
                screen_idle();
            }
            break;
            
        case APP_STATE_IDLE:
            // Check for new jobs (can optimize with callbacks)
            if (aronet_get_next_job(&current_job) == ARONET_OK && current_job.id > 0) {
                app_state = APP_STATE_SHOWING_JOB;
                screen_job_available();
            }
            break;
            
        case APP_STATE_SHOWING_JOB:
            // User will click a button - handled by event_start_job
            break;
            
        case APP_STATE_JOB_STARTED:
            // Job running - show timer, wait for completion click
            break;
            
        case APP_STATE_ERROR:
            // Error shown on screen, user can retry
            break;
            
        default:
            break;
    }
}

// ============ EXAMPLE INTEGRATION IN main.c ============

/*

In your main.c, replace or add to the existing code:

#include "aronet_device_client.h"

// ...existing includes...

void app_main(void) {
    // ...existing init code (display, touch, etc)...
    
    // Initialize AroNet device
    aronet_device_setup("DISPLAY-01", "192.168.1.100");  // Change IP to your backend server
    
    // Main loop
    while (1) {
        // ...existing display/touch handling...
        
        // Update AroNet state
        aronet_device_update();
        
        // Render LVGL
        lv_timer_handler();
        
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

*/
