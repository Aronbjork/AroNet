#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// STC8H1K28 I2C protocol variants
#define STC_PROTOCOL_V1_1      1   // brightness 0x05-0x10, buzzer 0x15/0x16
#define STC_PROTOCOL_V1_2_V1_3 2   // brightness 0-245, buzzer 246/247

uint8_t hw_version_get_stc_protocol(void);
bool hw_version_is_stc_protocol_configured(void);
esp_err_t hw_version_set_stc_protocol(uint8_t protocol);
