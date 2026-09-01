#ifndef ARONET_CONFIG_H
#define ARONET_CONFIG_H

#include "hw_version.h"

// =============================================================================
// FIRMWARE VERSION
// =============================================================================
#define FIRMWARE_VERSION_MAJOR  0
#define FIRMWARE_VERSION_MINOR  1
#define FIRMWARE_VERSION_PATCH  0
#define FIRMWARE_VERSION_STRING "0.1.0"

// =============================================================================
// DISPLAY HARDWARE VERSION
// =============================================================================
//   0 = Version 1.0 (TCA9534/PCA9557 I/O expander for backlight)
//   1 = STC8H1K28 present, factory default protocol = v1.1
//   2 = STC8H1K28 present, factory default protocol = v1.2/1.3
#define DISPLAY_HW_VERSION      1

#if DISPLAY_HW_VERSION == 2
#define STC_PROTOCOL_FACTORY_DEFAULT   STC_PROTOCOL_V1_2_V1_3
#else
#define STC_PROTOCOL_FACTORY_DEFAULT   STC_PROTOCOL_V1_1
#endif

// =============================================================================
// UI CONFIGURATION
// =============================================================================
#define UI_LVGL_TICK_PERIOD_MS      15
#define UI_TASK_DELAY_MS            10

#endif // ARONET_CONFIG_H
