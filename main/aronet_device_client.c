#include "aronet_device_client.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_event.h"
#include "esp_attr.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

static const char *TAG = "aronet_client";
static char server_url[64];
static char configured_device_id[32];
static int32_t wifi_rssi;
static bool wifi_connected;
static bool network_initialized;

typedef struct {
    char data[32768];
    size_t length;
} http_response_t;

static EXT_RAM_BSS_ATTR http_response_t s_response;

static esp_err_t http_event_handler(esp_http_client_event_t *event)
{
    http_response_t *response = event->user_data;
    if (event->event_id == HTTP_EVENT_ON_DATA && response && event->data_len > 0) {
        size_t available = sizeof(response->data) - response->length - 1;
        size_t copy_length = event->data_len < available ? event->data_len : available;
        memcpy(response->data + response->length, event->data, copy_length);
        response->length += copy_length;
        response->data[response->length] = '\0';
    }
    return ESP_OK;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_connected = false;
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        wifi_connected = true;
        ESP_LOGI(TAG, "Wi-Fi connected");
    }
}

static aronet_error_t request(const char *path, esp_http_client_method_t method,
                              const char *body, http_response_t *response)
{
    if (!wifi_connected) {
        return ARONET_ERR_NETWORK;
    }

    char url[160];
    snprintf(url, sizeof(url), "%s%s", server_url, path);
    memset(response, 0, sizeof(*response));

    esp_http_client_config_t config = {
        .url = url,
        .method = method,
        .timeout_ms = 5000,
        .event_handler = http_event_handler,
        .user_data = response,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        return ARONET_ERR_MEMORY;
    }
    if (body) {
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, body, strlen(body));
    }

    esp_err_t err = esp_http_client_perform(client);
    int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "HTTP request failed: %s", esp_err_to_name(err));
        return ARONET_ERR_NETWORK;
    }
    return status_code >= 200 && status_code < 300 ? ARONET_OK : ARONET_ERR_HTTP;
}

static void copy_json_string(cJSON *object, const char *name, char *destination, size_t length)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (cJSON_IsString(item) && item->valuestring) {
        snprintf(destination, length, "%s", item->valuestring);
    }
}

static aronet_error_t parse_job(cJSON *json, aronet_job_t *job)
{
    if (!cJSON_IsObject(json) || !cJSON_GetObjectItemCaseSensitive(json, "id")) {
        return ARONET_ERR_NOT_FOUND;
    }
    memset(job, 0, sizeof(*job));
    job->id = cJSON_GetObjectItemCaseSensitive(json, "id")->valueint;
    cJSON *product_id = cJSON_GetObjectItemCaseSensitive(json, "product_id");
    cJSON *operation_id = cJSON_GetObjectItemCaseSensitive(json, "operation_id");
    cJSON *quantity = cJSON_GetObjectItemCaseSensitive(json, "quantity");
    cJSON *elapsed_seconds = cJSON_GetObjectItemCaseSensitive(json, "elapsed_seconds");
    job->product_id = cJSON_IsNumber(product_id) ? product_id->valueint : 0;
    job->operation_id = cJSON_IsNumber(operation_id) ? operation_id->valueint : 0;
    job->quantity = cJSON_IsNumber(quantity) ? quantity->valueint : 1;
    job->elapsed_seconds = cJSON_IsNumber(elapsed_seconds) ? elapsed_seconds->valueint : 0;
    cJSON *status = cJSON_GetObjectItemCaseSensitive(json, "status");
    if (cJSON_IsString(status) && strcmp(status->valuestring, "paused") == 0) {
        job->status = JOB_STATUS_PAUSED;
    } else if (cJSON_IsString(status) && strcmp(status->valuestring, "in_progress") == 0) {
        job->status = JOB_STATUS_IN_PROGRESS;
    } else {
        job->status = JOB_STATUS_PENDING;
    }
    copy_json_string(json, "batch_number", job->batch_number, sizeof(job->batch_number));
    copy_json_string(json, "product_code", job->product_code, sizeof(job->product_code));
    copy_json_string(json, "product_name", job->product_name, sizeof(job->product_name));
    copy_json_string(json, "operation_name", job->operation_name, sizeof(job->operation_name));
    return ARONET_OK;
}

aronet_error_t aronet_wifi_connect(const char *ssid, const char *password)
{
    if (network_initialized) {
        return ARONET_OK;
    }
    if (!ssid || !ssid[0]) {
        return ARONET_ERR_NETWORK;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_config));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                         &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                         &wifi_event_handler, NULL, NULL));
    wifi_config_t config = {0};
    snprintf((char *)config.sta.ssid, sizeof(config.sta.ssid), "%s", ssid);
    snprintf((char *)config.sta.password, sizeof(config.sta.password), "%s", password ? password : "");
    config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &config));
    ESP_ERROR_CHECK(esp_wifi_start());
    network_initialized = true;
    return ARONET_OK;
}

aronet_error_t aronet_device_init(const char *server_ip, uint16_t port, const char *device_id)
{
    if (!server_ip || !device_id) {
        return ARONET_ERR_NETWORK;
    }
    snprintf(server_url, sizeof(server_url), "http://%s:%u/api", server_ip, port);
    snprintf(configured_device_id, sizeof(configured_device_id), "%s", device_id);
    return ARONET_OK;
}

void aronet_device_deinit(void) {}

bool aronet_is_connected(void) { return wifi_connected; }
void aronet_set_wifi_signal(int32_t rssi) { wifi_rssi = rssi; }

aronet_error_t aronet_update_device_status(const char *status)
{
    char path[96];
    char body[96];
    snprintf(path, sizeof(path), "/devices/%s/status", configured_device_id);
    snprintf(body, sizeof(body), "{\"status\":\"%s\",\"wifi_signal\":%ld}",
             status, (long)wifi_rssi);
    return request(path, HTTP_METHOD_POST, body, &s_response);
}

aronet_error_t aronet_get_next_job(aronet_job_t *job)
{
    char path[96];
    snprintf(path, sizeof(path), "/devices/%s/status", configured_device_id);
    aronet_error_t result = request(path, HTTP_METHOD_GET, NULL, &s_response);
    if (result != ARONET_OK) {
        return result;
    }
    cJSON *root = cJSON_Parse(s_response.data);
    cJSON *next_job = root ? cJSON_GetObjectItemCaseSensitive(root, "next_job") : NULL;
    result = parse_job(next_job, job);
    cJSON_Delete(root);
    return result;
}

aronet_error_t aronet_get_device_status(aronet_device_status_t *status, aronet_job_t *job)
{
    if (status) {
        memset(status, 0, sizeof(*status));
        snprintf(status->device_id, sizeof(status->device_id), "%s", configured_device_id);
    }
    return job ? aronet_get_next_job(job) : ARONET_OK;
}

aronet_error_t aronet_assign_job(uint32_t job_id)
{
    char path[64];
    char body[64];
    snprintf(path, sizeof(path), "/jobs/%lu/assign", (unsigned long)job_id);
    snprintf(body, sizeof(body), "{\"device_id\":\"%s\"}", configured_device_id);
    return request(path, HTTP_METHOD_PUT, body, &s_response);
}

aronet_error_t aronet_start_job(uint32_t job_id)
{
    char path[64];
    snprintf(path, sizeof(path), "/jobs/%lu/start", (unsigned long)job_id);
    return request(path, HTTP_METHOD_PUT, "{}", &s_response);
}

aronet_error_t aronet_complete_job(uint32_t job_id)
{
    char path[64];
    char body[64];
    snprintf(path, sizeof(path), "/jobs/%lu/complete", (unsigned long)job_id);
    snprintf(body, sizeof(body), "{\"device_id\":\"%s\"}", configured_device_id);
    return request(path, HTTP_METHOD_PUT, body, &s_response);
}

aronet_error_t aronet_pause_job(uint32_t job_id)
{
    char path[64];
    snprintf(path, sizeof(path), "/jobs/%lu/pause", (unsigned long)job_id);
    return request(path, HTTP_METHOD_PUT, "{}", &s_response);
}

aronet_error_t aronet_get_jobs(const char *status, aronet_job_t *jobs, uint32_t max_jobs, uint32_t *count)
{
    if (!jobs || !count || max_jobs == 0) {
        return ARONET_ERR_MEMORY;
    }

    char path[96];
    snprintf(path, sizeof(path), "/jobs%s%s%s", status ? "?status=" : "", status ? status : "",
             status ? "&limit=50" : "?limit=50");
    aronet_error_t result = request(path, HTTP_METHOD_GET, NULL, &s_response);
    if (result != ARONET_OK) {
        return result;
    }

    cJSON *root = cJSON_Parse(s_response.data);
    if (!cJSON_IsArray(root)) {
        cJSON_Delete(root);
        return ARONET_ERR_JSON;
    }
    uint32_t job_count = 0;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, root) {
        if (job_count >= max_jobs) {
            break;
        }
        if (parse_job(item, &jobs[job_count]) == ARONET_OK) {
            job_count++;
        }
    }
    cJSON_Delete(root);
    *count = job_count;
    return job_count ? ARONET_OK : ARONET_ERR_NOT_FOUND;
}

aronet_error_t aronet_get_parts(aronet_part_t *parts, uint32_t max_parts, uint32_t *count)
{
    (void)parts;
    (void)max_parts;
    if (count) *count = 0;
    return ARONET_ERR_NOT_FOUND;
}

aronet_error_t aronet_get_part(uint32_t part_id, aronet_part_t *part)
{
    (void)part_id;
    (void)part;
    return ARONET_ERR_NOT_FOUND;
}

void aronet_sync_tick(void) { aronet_update_device_status("idle"); }

const char *aronet_error_string(aronet_error_t error)
{
    static const char *messages[] = {"OK", "Network unavailable", "Server request failed",
                                     "Invalid server response", "Not found", "Timed out", "Out of memory"};
    return error <= ARONET_ERR_MEMORY ? messages[error] : "Unknown error";
}