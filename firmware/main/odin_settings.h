#pragma once
#include <stdbool.h>
/* Call under the LVGL lock after the Wi-Fi and chat UI have been created. */
void odin_settings_create(void);

/* Navigation callbacks explicitly limit the cog to the home page. */
void odin_settings_set_home(bool home);
