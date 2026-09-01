#pragma once

#include "driver/i2c_master.h"
#include "lvgl.h"

// Touch controller pins (CrowPanel 5 Advance V1.0)
#define TOUCH_GT911_SCL           16
#define TOUCH_GT911_SDA           15
#define TOUCH_GT911_RST           4
#define TOUCH_GT911_INT           21
#define TOUCH_I2C_NUM             0
#define TOUCH_I2C_SPEED           400000

// GT911 register definitions
#define GT911_PRODUCT_ID_REG      0x8140
#define GT911_COMMAND_REG         0x8040
#define GT911_CONFIG_REG          0x8047
#define GT911_COORD_REG           0x814E
#define GT911_POINT1_REG          0x8150
#define GT911_PRODUCT_ID_LEN      4
#define GT911_MAX_CONTACTS        5
#define GT911_POINT_SIZE          8

esp_err_t touch_init(void);
lv_indev_t *touch_create_indev(void);
void touch_start_monitor_task(uint32_t stack_size, int priority);
i2c_master_bus_handle_t touch_get_i2c_bus_handle(void);
