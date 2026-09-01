#include "lvgl.h"
#include "touch_driver.h"
#include "screen_orientation.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <inttypes.h>

static const char *TAG = "touch";

static i2c_master_bus_handle_t i2c_bus_handle = NULL;
static i2c_master_dev_handle_t gt911_dev_handle = NULL;

typedef struct {
    uint8_t touch_points;
    struct {
        uint16_t x;
        uint16_t y;
        uint16_t size;
        uint8_t track_id;
    } point[GT911_MAX_CONTACTS];
} touch_data_t;

static touch_data_t g_touch_data;
static bool g_touch_initialized = false;
static uint8_t GT911_I2C_ADDR = 0x5D;

static esp_err_t gt911_reset_sequence(void);
static esp_err_t touch_i2c_init(void);
static esp_err_t gt911_i2c_read(uint16_t reg_addr, uint8_t *data, uint8_t len);
static esp_err_t gt911_i2c_write(uint16_t reg_addr, uint8_t *data, uint8_t len);
static esp_err_t gt911_detect(void);
static void gt911_read_touch_data(void);
static void gt911_read_cb(lv_indev_t *indev, lv_indev_data_t *data);
static void touch_monitor_task_fn(void *arg);

esp_err_t touch_init(void)
{
    ESP_LOGI(TAG, "Initializing GT911 touch controller");

    esp_err_t ret = gt911_reset_sequence();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reset GT911");
        return ret;
    }

    ret = touch_i2c_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2C");
        return ret;
    }

    ret = gt911_detect();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to detect GT911 touch controller");
        return ret;
    }

    g_touch_initialized = true;
    return ESP_OK;
}

lv_indev_t *touch_create_indev(void)
{
    if (!g_touch_initialized) {
        ESP_LOGE(TAG, "Touch controller not initialized");
        return NULL;
    }

    lv_indev_t *touch_indev = lv_indev_create();
    lv_indev_set_type(touch_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch_indev, gt911_read_cb);

    return touch_indev;
}

void touch_start_monitor_task(uint32_t stack_size, int priority)
{
    xTaskCreate(touch_monitor_task_fn, "touch_monitor", stack_size, NULL, priority, NULL);
}

/************************** Internal functions **************************/

static esp_err_t gt911_reset_sequence(void)
{
    ESP_LOGI(TAG, "Performing GT911 reset sequence");

    gpio_config_t io_config = {
        .pin_bit_mask = (1ULL << TOUCH_GT911_RST) | (1ULL << TOUCH_GT911_INT),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t ret = gpio_config(&io_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GT911 reset/interrupt pins: %s", esp_err_to_name(ret));
        return ret;
    }

    // Reset sequence: INT low during reset → address 0x14
    gpio_set_level(TOUCH_GT911_RST, 0);
    gpio_set_level(TOUCH_GT911_INT, 0);
    vTaskDelay(pdMS_TO_TICKS(10));

    gpio_set_level(TOUCH_GT911_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));

    // Set INT pin as input after reset
    io_config.pin_bit_mask = (1ULL << TOUCH_GT911_INT);
    io_config.mode = GPIO_MODE_INPUT;
    io_config.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_config);

    ESP_LOGI(TAG, "GT911 reset complete - should respond at address 0x14");
    return ESP_OK;
}

static esp_err_t touch_i2c_init(void)
{
    ESP_LOGI(TAG, "Initializing I2C for GT911");

    i2c_master_bus_config_t bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = TOUCH_I2C_NUM,
        .scl_io_num = TOUCH_GT911_SCL,
        .sda_io_num = TOUCH_GT911_SDA,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t ret = i2c_new_master_bus(&bus_config, &i2c_bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure I2C bus: %s", esp_err_to_name(ret));
        return ret;
    }

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = GT911_I2C_ADDR,
        .scl_speed_hz = TOUCH_I2C_SPEED,
    };

    ret = i2c_master_bus_add_device(i2c_bus_handle, &dev_config, &gt911_dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add GT911 device: %s", esp_err_to_name(ret));
        i2c_del_master_bus(i2c_bus_handle);
        i2c_bus_handle = NULL;
    }

    return ret;
}

static esp_err_t gt911_i2c_read(uint16_t reg_addr, uint8_t *data, uint8_t len)
{
    uint8_t reg_addr_buf[2] = {(reg_addr >> 8) & 0xFF, reg_addr & 0xFF};
    esp_err_t ret = i2c_master_transmit_receive(gt911_dev_handle,
                                                reg_addr_buf, sizeof(reg_addr_buf),
                                                data, len, 10);
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "I2C read failed at 0x%02X: %s", GT911_I2C_ADDR, esp_err_to_name(ret));
    }
    return ret;
}

static esp_err_t gt911_i2c_write(uint16_t reg_addr, uint8_t *data, uint8_t len)
{
    uint8_t write_buf[len + 2];
    write_buf[0] = (reg_addr >> 8) & 0xFF;
    write_buf[1] = reg_addr & 0xFF;

    if (data && len > 0) {
        memcpy(&write_buf[2], data, len);
    }

    esp_err_t ret = i2c_master_transmit(gt911_dev_handle, write_buf, len + 2, 10);
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "I2C write failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

static esp_err_t gt911_detect(void)
{
    uint8_t id_buf[GT911_PRODUCT_ID_LEN] = {0};
    esp_err_t ret = gt911_i2c_read(GT911_PRODUCT_ID_REG, id_buf, GT911_PRODUCT_ID_LEN);

    if (ret != ESP_OK) {
        ESP_LOGI(TAG, "GT911 not at 0x%02X, trying 0x14", GT911_I2C_ADDR);

        i2c_master_bus_rm_device(gt911_dev_handle);
        gt911_dev_handle = NULL;

        i2c_device_config_t dev_config = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = 0x14,
            .scl_speed_hz = TOUCH_I2C_SPEED,
        };

        ret = i2c_master_bus_add_device(i2c_bus_handle, &dev_config, &gt911_dev_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to add GT911 at 0x14: %s", esp_err_to_name(ret));
            return ret;
        }

        GT911_I2C_ADDR = 0x14;
        ret = gt911_i2c_read(GT911_PRODUCT_ID_REG, id_buf, GT911_PRODUCT_ID_LEN);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "GT911 not detected at 0x14 either");
            return ret;
        }
    }

    ESP_LOGI(TAG, "GT911 Product ID: %c%c%c%c",
             id_buf[0], id_buf[1], id_buf[2], id_buf[3]);

    memset(&g_touch_data, 0, sizeof(touch_data_t));

    uint8_t clear_state = 0;
    gt911_i2c_write(GT911_COORD_REG, &clear_state, 1);

    return ESP_OK;
}

static void gt911_read_touch_data(void)
{
    if (!g_touch_initialized) {
        return;
    }

    uint8_t touch_status;
    if (gt911_i2c_read(GT911_COORD_REG, &touch_status, 1) != ESP_OK) {
        return;
    }

    g_touch_data.touch_points = touch_status & 0x0F;
    if (g_touch_data.touch_points > GT911_MAX_CONTACTS) {
        g_touch_data.touch_points = GT911_MAX_CONTACTS;
    }

    if (g_touch_data.touch_points > 0) {
        uint8_t point_data[GT911_MAX_CONTACTS * GT911_POINT_SIZE];
        if (gt911_i2c_read(GT911_POINT1_REG, point_data, g_touch_data.touch_points * GT911_POINT_SIZE) != ESP_OK) {
            return;
        }

        for (int i = 0; i < g_touch_data.touch_points; i++) {
            uint8_t *buf = &point_data[i * GT911_POINT_SIZE];
            g_touch_data.point[i].x = ((uint16_t)buf[1] << 8) | buf[0];
            g_touch_data.point[i].y = ((uint16_t)buf[3] << 8) | buf[2];
            g_touch_data.point[i].size = ((uint16_t)buf[5] << 8) | buf[4];
            g_touch_data.point[i].track_id = buf[7];
        }
    }

    uint8_t clear_status = 0;
    gt911_i2c_write(GT911_COORD_REG, &clear_status, 1);
}

static void transform_touch_coordinates(int *x, int *y)
{
    screen_orientation_t orientation = screen_orientation_get_current();

    if (orientation == SCREEN_ORIENTATION_FLIPPED) {
        *x = 799 - *x;
        *y = 479 - *y;
    }
}

static void gt911_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    gt911_read_touch_data();

    if (g_touch_data.touch_points > 0) {
        int x = g_touch_data.point[0].x;
        int y = g_touch_data.point[0].y;

        transform_touch_coordinates(&x, &y);

        if (x < 0) x = 0;
        if (x > 799) x = 799;
        if (y < 0) y = 0;
        if (y > 479) y = 479;
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static void touch_monitor_task_fn(void *arg)
{
    ESP_LOGI(TAG, "Touch monitoring task started");

    while (1) {
        gt911_read_touch_data();

        if (g_touch_data.touch_points > 0) {
            ESP_LOGI(TAG, "Touch: %d points, x=%d y=%d",
                     (int)g_touch_data.touch_points,
                     (int)g_touch_data.point[0].x,
                     (int)g_touch_data.point[0].y);
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

i2c_master_bus_handle_t touch_get_i2c_bus_handle(void)
{
    return i2c_bus_handle;
}
