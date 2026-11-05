#pragma once
#include <stdint.h>
#include "esp_err.h"

/* Owns V1.0 LCD power, backlight, DSI, touch I2C, and the LVGL task. */
esp_err_t odin_board_init(void);
esp_err_t odin_board_set_backlight(uint32_t brightness);
