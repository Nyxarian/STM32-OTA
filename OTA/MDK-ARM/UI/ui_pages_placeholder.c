#include "ui_nav.h"
#include "ui_theme.h"
#include "lvgl.h"

/*
 * 通用占位页面。
 *
 * 用于尚未实现的应用入口。页面保留模块名称和入口状态，
 * 不再显示“ESC BACK”等按键说明。
 */
static const char *placeholder_title = ""; // 保存桌面传入的应用名称。

/* 设置占位页面需要显示的应用名称。 */
void ui_page_placeholder_set_title(const char *title)
{
    placeholder_title = title; // 保存当前应用标题指针。
}

/* 创建通用占位页面。 */
void ui_page_placeholder_create(lv_obj_t *parent)
{
    lv_obj_t *title; // 保存应用名称标签。
    lv_obj_t *sub; // 保存英文副标题标签。
    lv_obj_t *hint; // 保存模块状态标签。

    title = lv_label_create(parent); // 创建应用名称标签。
    lv_label_set_text(title, placeholder_title); // 设置应用名称。
    lv_obj_set_style_text_color(title, UI_COLOR_TEXT, LV_PART_MAIN); // 设置应用名称颜色。
    lv_obj_set_style_text_font(title, &ui_font_12, LV_PART_MAIN); // 设置应用名称字体。
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -10); // 将应用名称放在页面中部偏上。

    sub = lv_label_create(parent); // 创建英文副标题。
    lv_label_set_text(sub, "MODULE PLACEHOLDER"); // 设置占位状态文字。
    lv_obj_set_style_text_color(sub, UI_COLOR_LIGHT_BLUE, LV_PART_MAIN); // 设置副标题颜色。
    lv_obj_set_style_text_font(sub, &ui_font_10, LV_PART_MAIN); // 设置副标题字体。
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 10); // 将副标题放在应用名称下方。

    hint = lv_label_create(parent); // 创建模块状态标签。
    lv_label_set_text(hint, "\xe6\xa8\xa1\xe5\x9d\x97\xe5\x85\xa5\xe5\x8f\xa3\xe5\xb7\xb2\xe4\xbf\x9d\xe7\x95\x99"); // 设置模块入口状态。
    lv_obj_set_style_text_color(hint, UI_COLOR_TEXT_DIM, LV_PART_MAIN); // 设置状态文字颜色。
    lv_obj_set_style_text_font(hint, &ui_font_12, LV_PART_MAIN); // 设置状态文字字体。
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 30); // 将状态文字放在副标题下方。
}

/* 处理占位页面按键。 */
void ui_page_placeholder_on_key(BSP_Key_t key)
{
    if (key == BSP_KEY_ESC) // 判断是否按下返回键。
    {
        ui_nav_pop(); // 返回桌面。
    }
}
