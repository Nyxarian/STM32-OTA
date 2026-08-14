#include "ui_theme.h"


/* 对整屏应用基础样式(背景色/文字色/字体) */
void ui_theme_apply_screen(lv_obj_t *screen)
{
    lv_obj_set_style_bg_color(screen, UI_COLOR_BG, LV_PART_MAIN);
    lv_obj_set_style_text_color(screen, UI_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(screen, &ui_font_14, LV_PART_MAIN);
    lv_obj_set_style_pad_all(screen, 0, LV_PART_MAIN);
}






