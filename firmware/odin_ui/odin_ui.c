#include "odin_ui.h"
#include <string.h>

static lv_obj_t *status_label, *transcript_label, *mode_label;

void odin_ui_button_style(lv_obj_t *obj, int variant)
{
    uint32_t bg = variant == 1 ? ODIN_ACCENT : variant == 2 ? 0x352825 : ODIN_RAISED;
    uint32_t fg = variant == 1 ? ODIN_BG : variant == 2 ? 0xF1B5A5 : ODIN_TEXT;
    lv_obj_set_style_bg_color(obj, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(fg), 0);
    lv_obj_set_style_text_font(obj, &lv_font_montserrat_16, 0);
    lv_obj_set_style_radius(obj, 14, 0);
    lv_obj_set_style_border_width(obj, variant == 1 ? 0 : 1, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(variant == 2 ? 0x69453C : ODIN_LINE), 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(variant == 1 ? 0xABC994 : 0x3B5144), LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x28322D), LV_STATE_DISABLED);
    lv_obj_set_style_text_color(obj, lv_color_hex(0x7F9185), LV_STATE_DISABLED);
    lv_obj_set_style_outline_width(obj, 0, LV_STATE_FOCUSED);
}
void odin_ui_panel_style(lv_obj_t *obj)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(ODIN_BG), 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(ODIN_TEXT), 0);
    lv_obj_set_style_text_font(obj, &lv_font_montserrat_16, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 24, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}
void odin_ui_input_style(lv_obj_t *obj)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(ODIN_SURFACE), 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(ODIN_TEXT), 0);
    lv_obj_set_style_text_font(obj, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(ODIN_MUTED), LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_border_color(obj, lv_color_hex(ODIN_LINE), 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(ODIN_ACCENT), LV_STATE_FOCUSED);
    lv_obj_set_style_radius(obj, 14, 0);
    lv_obj_set_style_pad_all(obj, 14, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(ODIN_ACCENT), LV_PART_CURSOR);
    lv_obj_set_style_text_line_space(obj, 7, 0);
}
void odin_ui_keyboard_style(lv_obj_t *obj)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(ODIN_SURFACE), 0);
    lv_obj_set_style_radius(obj, 16, 0);
    lv_obj_set_style_pad_all(obj, 8, 0);
    lv_obj_set_style_pad_row(obj, 6, 0);
    lv_obj_set_style_pad_column(obj, 6, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(ODIN_RAISED), LV_PART_ITEMS);
    lv_obj_set_style_text_color(obj, lv_color_hex(ODIN_TEXT), LV_PART_ITEMS);
    lv_obj_set_style_text_font(obj, &lv_font_montserrat_18, LV_PART_ITEMS);
    lv_obj_set_style_radius(obj, 9, LV_PART_ITEMS);
    lv_obj_set_style_border_width(obj, 0, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(obj, 0, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x455E4B), LV_PART_ITEMS | LV_STATE_PRESSED);
}
static lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *obj = lv_label_create(parent); lv_label_set_text(obj, text);
    lv_obj_set_pos(obj, x, y); lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0); return obj;
}
void odin_ui_create(void)
{
    lv_theme_t *theme = lv_theme_default_init(lv_disp_get_default(), lv_color_hex(ODIN_ACCENT),
                                             lv_color_hex(ODIN_RAISED), true, &lv_font_montserrat_16);
    lv_disp_set_theme(lv_disp_get_default(), theme);
    lv_obj_t *scr = lv_scr_act(); odin_ui_panel_style(scr); lv_obj_set_style_pad_all(scr, 0, 0);
    label(scr, "O D I N", 48, 32, &lv_font_montserrat_24, ODIN_TEXT);
    label(scr, "YOUR PERSONAL COMPANION", 48, 70, &lv_font_montserrat_14, ODIN_MUTED);
    mode_label = label(scr, "Connecting", 744, 36, &lv_font_montserrat_14, ODIN_ACCENT);
    lv_obj_set_width(mode_label, 230); lv_obj_set_style_text_align(mode_label, LV_TEXT_ALIGN_RIGHT, 0);
    label(scr, "A little clarity.\nA conversation away.", 56, 164, &lv_font_montserrat_40, ODIN_TEXT);
    status_label = label(scr, "Ready when you are", 58, 290, &lv_font_montserrat_18, ODIN_ACCENT);
    transcript_label = label(scr, "Ask a question, share a thought,\nor simply start talking.", 58, 326, &lv_font_montserrat_16, ODIN_MUTED);
    lv_obj_set_width(transcript_label, 490); lv_label_set_long_mode(transcript_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_line_space(transcript_label, 7, 0);
    /* Static concentric rings avoid continuous framebuffer redraws. */
    for (int i = 0; i < 3; ++i) {
        lv_obj_t *ring = lv_obj_create(scr); int size = 286 - i * 54;
        lv_obj_set_size(ring, size, size); lv_obj_set_pos(ring, 745 - size / 2, 286 - size / 2);
        lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(ring, lv_color_hex(i == 2 ? 0x233D2D : ODIN_BG), 0);
        lv_obj_set_style_border_color(ring, lv_color_hex(i == 0 ? 0x24392D : 0x496745), 0);
        lv_obj_set_style_border_width(ring, 1, 0);
        lv_obj_set_style_pad_all(ring, 0, 0); lv_obj_clear_flag(ring, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }
    const int heights[] = {16, 32, 50, 66, 40, 24, 48, 30, 14};
    for (int i = 0; i < 9; ++i) {
        lv_obj_t *bar = lv_obj_create(scr); lv_obj_set_size(bar, 5, heights[i]);
        lv_obj_set_pos(bar, 695 + i * 12, 286 - heights[i] / 2);
        lv_obj_set_style_bg_color(bar, lv_color_hex(ODIN_ACCENT), 0);
        lv_obj_set_style_border_width(bar, 0, 0); lv_obj_set_style_radius(bar, 4, 0);
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }
    label(scr, "A SPACE TO THINK OUT LOUD", 611, 464, &lv_font_montserrat_14, ODIN_MUTED);
    lv_obj_t *line = lv_obj_create(scr); lv_obj_set_pos(line, 48, 532); lv_obj_set_size(line, 928, 1);
    lv_obj_set_style_bg_color(line, lv_color_hex(ODIN_LINE), 0); lv_obj_set_style_border_width(line, 0, 0);
    label(scr, "Made for your everyday questions", 48, 554, &lv_font_montserrat_14, ODIN_MUTED);
}
void odin_ui_set_status(const char *status)
{
    if (status_label && status) lv_label_set_text(status_label, status);
}
void odin_ui_set_transcript(const char *text)
{
    if (transcript_label && text) lv_label_set_text(transcript_label, text);
}
void odin_ui_set_mode(const char *mode)
{
    if (!mode_label || !mode) return;
    lv_label_set_text(mode_label, !strcmp(mode, "LM STUDIO") ? LV_SYMBOL_OK "  AI connected" : "AI offline");
}
