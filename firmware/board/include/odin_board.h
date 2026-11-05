#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/* Owns V1.0 LCD power, backlight, DSI, touch I2C, and the LVGL task. */
esp_err_t odin_board_init(void);
esp_err_t odin_board_set_backlight(uint32_t brightness);
uint32_t odin_board_get_backlight(void);
/* Set under the LVGL lock. Return true to consume a touch sample. */
void odin_board_set_touch_filter(bool (*filter)(bool pressed));

/* STC8 battery registers. Call from a worker task; uses the existing touch bus. */
typedef struct {
    uint32_t adc_mv;
    uint32_t battery_mv;
    uint8_t percent;
    uint8_t charge_state;
    uint8_t led_state;
} odin_battery_info_t;
esp_err_t odin_board_read_battery(odin_battery_info_t *info);
