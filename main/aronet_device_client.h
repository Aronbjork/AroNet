#pragma once

/**
 * AroNet ESP32 Device Client
 * 
 * HTTP REST client for communicating with the AroNet backend server.
 * Handles:
 * - Device status reporting
 * - Job queue fetching
 * - Job lifecycle (start, complete)
 * - Inventory updates
 * 
 * Usage:
 *   aronet_device_init("192.168.1.100", 5000, "DISPLAY-01");
 *   aronet_get_next_job(&job);
 *   aronet_start_job(job.id);
 *   aronet_complete_job(job.id);
 */

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============ DATA STRUCTURES ============

typedef enum {
    JOB_STATUS_PENDING,
    JOB_STATUS_ASSIGNED,
    JOB_STATUS_IN_PROGRESS,
    JOB_STATUS_COMPLETED
} job_status_t;

typedef struct {
    uint32_t id;
    uint32_t product_id;
    uint32_t operation_id;
    uint32_t quantity;
    char batch_number[64];
    char product_code[32];
    char product_name[128];
    char operation_name[64];
    job_status_t status;
    char assigned_device_id[32];
    time_t started_at;
    time_t completed_at;
} aronet_job_t;

typedef struct {
    char device_id[32];
    time_t last_seen;
    int32_t wifi_signal;  // RSSI in dBm (e.g., -50 to -100)
    uint32_t current_job_id;
    char status[32];  // "idle", "working", "error"
} aronet_device_status_t;

typedef struct {
    uint32_t id;
    char part_number[32];
    char name[64];
    char description[256];
    uint32_t quantity;
    uint32_t reorder_level;
    char unit[16];
} aronet_part_t;

typedef enum {
    ARONET_OK = 0,
    ARONET_ERR_NETWORK = 1,
    ARONET_ERR_HTTP = 2,
    ARONET_ERR_JSON = 3,
    ARONET_ERR_NOT_FOUND = 4,
    ARONET_ERR_TIMEOUT = 5,
    ARONET_ERR_MEMORY = 6
} aronet_error_t;

// ============ INITIALIZATION ============

/**
 * Initialize device connection to AroNet server
 * 
 * @param server_ip    IP address of backend server (e.g., "192.168.1.100")
 * @param port         Port number (default 5000)
 * @param device_id    Unique device identifier (e.g., "DISPLAY-01")
 * @return ARONET_OK on success
 */
aronet_error_t aronet_device_init(const char *server_ip, uint16_t port, const char *device_id);

/** Connect the display to its configured Wi-Fi network. */
aronet_error_t aronet_wifi_connect(const char *ssid, const char *password);

/**
 * Deinitialize and cleanup
 */
void aronet_device_deinit(void);

/**
 * Check if device is connected to server
 */
bool aronet_is_connected(void);

/**
 * Set WiFi signal strength (for status reporting)
 */
void aronet_set_wifi_signal(int32_t rssi);

// ============ DEVICE STATUS ============

/**
 * Get current device status and next job in queue
 * 
 * @param status    Pointer to status struct (will be filled)
 * @param job       Pointer to next job struct (will be filled, NULL if no job)
 * @return ARONET_OK on success
 */
aronet_error_t aronet_get_device_status(aronet_device_status_t *status, aronet_job_t *job);

/**
 * Update device status (idle, working, error)
 * 
 * @param status_str "idle", "working", or "error"
 * @return ARONET_OK on success
 */
aronet_error_t aronet_update_device_status(const char *status_str);

// ============ JOB MANAGEMENT ============

/**
 * Get next job from queue for this device
 * 
 * @param job   Pointer to job struct (will be filled)
 * @return ARONET_OK if job found, ARONET_ERR_NOT_FOUND if no pending jobs
 */
aronet_error_t aronet_get_next_job(aronet_job_t *job);

/**
 * Assign job to this device
 * 
 * @param job_id    Job ID to assign
 * @return ARONET_OK on success
 */
aronet_error_t aronet_assign_job(uint32_t job_id);

/**
 * Mark job as started
 * 
 * @param job_id    Job ID
 * @return ARONET_OK on success
 */
aronet_error_t aronet_start_job(uint32_t job_id);

/**
 * Mark job as completed and decrement inventory
 * 
 * @param job_id    Job ID
 * @return ARONET_OK on success
 */
aronet_error_t aronet_complete_job(uint32_t job_id);

/**
 * Get jobs for this device (filtered by status)
 * 
 * @param status    "pending", "assigned", "in_progress", "completed", or NULL for all
 * @param jobs      Array to store results
 * @param max_jobs  Maximum number of jobs to fetch
 * @param count     Pointer to store actual count returned
 * @return ARONET_OK on success
 */
aronet_error_t aronet_get_jobs(const char *status, aronet_job_t *jobs, uint32_t max_jobs, uint32_t *count);

// ============ INVENTORY CHECK ============

/**
 * Get list of parts with current inventory
 * 
 * @param parts     Array to store results
 * @param max_parts Maximum number of parts to fetch
 * @param count     Pointer to store actual count returned
 * @return ARONET_OK on success
 */
aronet_error_t aronet_get_parts(aronet_part_t *parts, uint32_t max_parts, uint32_t *count);

/**
 * Get single part by ID
 * 
 * @param part_id   Part ID
 * @param part      Pointer to part struct (will be filled)
 * @return ARONET_OK on success
 */
aronet_error_t aronet_get_part(uint32_t part_id, aronet_part_t *part);

// ============ POLLING HELPERS ============

/**
 * Periodic sync task (call from main RTOS task, ~1Hz)
 * Handles:
 * - Reporting device status
 * - Fetching job queue
 * - Handling lost connection
 */
void aronet_sync_tick(void);

/**
 * Error code to string
 */
const char* aronet_error_string(aronet_error_t error);

#ifdef __cplusplus
}
#endif
