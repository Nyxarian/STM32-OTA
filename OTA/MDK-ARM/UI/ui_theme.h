#ifndef __UI_THEME_H__
#define __UI_THEME_H__

#include "lvgl.h"

/* 字体(由 tools/generate_ota_ui_font.py 生成多字号) */
extern const lv_font_t ui_font_10;   /* 副标题/底部信息 */
extern const lv_font_t ui_font_12;   /* 应用标题/桌面标题 */
extern const lv_font_t ui_font_14;   /* 通用/锁屏提示 */

/* 主题色板:550W 终端风格(黑 + 克莱因蓝 + 白 + 红) */
#define UI_COLOR_BG         lv_color_hex(0x000000)  /* LCD 背景:纯黑 */
#define UI_COLOR_APP_BG     lv_color_hex(0x030712)  /* 应用块背景 */
#define UI_COLOR_DEEP_BLUE  lv_color_hex(0x06112E)  /* 深蓝交互背景 */
#define UI_COLOR_KL         lv_color_hex(0x002FA7)  /* 克莱因蓝主色 */
#define UI_COLOR_AUX_BLUE   lv_color_hex(0x163D9B)  /* 辅助蓝(边框/分割线) */
#define UI_COLOR_LIGHT_BLUE lv_color_hex(0x9FB7FF)  /* 浅蓝文字 */
#define UI_COLOR_TEXT       lv_color_hex(0xF7F9FF)  /* 主要白色 */
#define UI_COLOR_TEXT_DIM   lv_color_hex(0x7E8AA8)  /* 次要文字 */
#define UI_COLOR_RED        lv_color_hex(0xE10600)  /* 红色强调 */
#define UI_COLOR_SELECT     lv_color_hex(0x081B52)  /* 选中/按下背景 */
#define UI_COLOR_BORDER     lv_color_hex(0x0F1B3A)  /* 屏幕边框辅助色 */

/* 兼容旧引用(旧页面已迁移,保留别名避免遗漏) */
#define UI_COLOR_ACCENT     UI_COLOR_LIGHT_BLUE
#define UI_COLOR_OK         lv_color_hex(0x22C55E)

/* 对整屏应用基础样式(背景色/文字色) */
void ui_theme_apply_screen(lv_obj_t *screen);

#endif /* __UI_THEME_H__ */
