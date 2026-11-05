#pragma once
#include "lvgl.h"
#ifdef __cplusplus
extern "C" {
#endif
#define ODIN_BG 0x101817
#define ODIN_SURFACE 0x182320
#define ODIN_RAISED 0x22332D
#define ODIN_LINE 0x34483F
#define ODIN_TEXT 0xF2F3E9
#define ODIN_MUTED 0xA5B8AB
#define ODIN_ACCENT 0xC8E6AE
/* User-defined power-source rule; applied to valid STC8 readings. */
#define ODIN_USB_POWER_THRESHOLD_MV 4300
/* UI calls require the LVGL lock or an LVGL callback. */
void odin_ui_create(void);
void odin_ui_set_status(const char *status);
void odin_ui_set_transcript(const char *text);
void odin_ui_set_mode(const char *mode);
void odin_ui_set_battery(bool available, uint32_t millivolts, unsigned percent, bool charging);
/* Shared appearance: 0 secondary, 1 primary, 2 destructive. */
void odin_ui_button_style(lv_obj_t *obj, int variant);
void odin_ui_panel_style(lv_obj_t *obj);
void odin_ui_input_style(lv_obj_t *obj);
void odin_ui_keyboard_style(lv_obj_t *obj);
#ifdef __cplusplus
}
#endif
