#pragma once
#include <stdbool.h>
#include "esp_err.h"
#define ODIN_POWER_IDLE_MS (10U * 60U * 1000U)
/* Call initialization and setters under the LVGL lock / from UI callbacks. */
esp_err_t odin_power_init(void);
bool odin_power_is_enabled(void);
void odin_power_set_enabled(bool enabled);
