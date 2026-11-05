#pragma once
/* Create under the LVGL lock. The worker owns all blocking Wi-Fi operations. */
void odin_wifi_ui_create(void);

void odin_wifi_ui_open(void);
