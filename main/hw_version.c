#include "hw_version.h"
#include "aronet_config.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"

static const char *TAG = "hw_version";

#define NVS_NAMESPACE     "hwver"
#define NVS_KEY_STC_PROTO "stc_proto"
#define NVS_COMMISSIONED_NAMESPACE "params"

static uint8_t s_stc_protocol = 0;
static bool s_stc_protocol_configured = false;

static void hw_version_ensure_nvs_mounted(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS requires erase and reinit");
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_flash_init();
    }
}

static bool nvs_is_device_already_commissioned(void)
{
    nvs_iterator_t it = NULL;
    esp_err_t ret = nvs_entry_find(NVS_DEFAULT_PART_NAME, NVS_COMMISSIONED_NAMESPACE, NVS_TYPE_ANY, &it);
    if (ret == ESP_OK && it != NULL) {
        nvs_release_iterator(it);
        return true;
    }
    return false;
}

static void hw_version_load(void)
{
    if (s_stc_protocol != 0) {
        return;
    }

    hw_version_ensure_nvs_mounted();

    uint8_t value = 0;
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        nvs_get_u8(handle, NVS_KEY_STC_PROTO, &value);
        nvs_close(handle);
    }

    if (value == STC_PROTOCOL_V1_1 || value == STC_PROTOCOL_V1_2_V1_3) {
        s_stc_protocol = value;
        s_stc_protocol_configured = true;
        return;
    }

    if (nvs_is_device_already_commissioned()) {
        value = STC_PROTOCOL_V1_1;
        ESP_LOGW(TAG, "Existing unit, no saved STC protocol - defaulting to v1.1");
    } else {
        value = STC_PROTOCOL_FACTORY_DEFAULT;
        ESP_LOGI(TAG, "New unit - using factory-default STC protocol v1.%s",
                 value == STC_PROTOCOL_V1_1 ? "1" : "2/1.3");
    }

    esp_err_t save_ret = hw_version_set_stc_protocol(value);
    if (save_ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to persist STC protocol: %s", esp_err_to_name(save_ret));
        s_stc_protocol = value;
    }
}

uint8_t hw_version_get_stc_protocol(void)
{
    hw_version_load();
    return s_stc_protocol;
}

bool hw_version_is_stc_protocol_configured(void)
{
    hw_version_load();
    return s_stc_protocol_configured;
}

esp_err_t hw_version_set_stc_protocol(uint8_t protocol)
{
    if (protocol != STC_PROTOCOL_V1_1 && protocol != STC_PROTOCOL_V1_2_V1_3) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = nvs_set_u8(handle, NVS_KEY_STC_PROTO, protocol);
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);

    if (ret == ESP_OK) {
        s_stc_protocol = protocol;
        s_stc_protocol_configured = true;
        ESP_LOGI(TAG, "STC8H1K28 protocol set to v1.%s", protocol == STC_PROTOCOL_V1_1 ? "1" : "2/1.3");
    }

    return ret;
}
