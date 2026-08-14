/*
 * 通用 UI 组件实现。
 *
 * 本文件只负责创建多个页面都能复用的基础控件，
 * 不保存俄罗斯方块、设置、网络、天气或升级页面的业务状态。
 */
#include "ui_components.h"
#include "ui_theme.h"

/*
 * 创建一个不参与点击和滚动的纯色矩形。
 */
lv_obj_t *ui_create_shape(lv_obj_t *parent,
                          int16_t x,
                          int16_t y,
                          int16_t width,
                          int16_t height,
                          lv_color_t color)
{
    lv_obj_t *shape; // 保存新创建的 LVGL 基础对象。

    shape = lv_obj_create(parent); // 创建基础对象。
    lv_obj_set_size(shape, width, height); // 设置矩形宽度和高度。
    lv_obj_set_pos(shape, x, y); // 设置矩形左上角位置。
    lv_obj_set_style_bg_color(shape, color, LV_PART_MAIN); // 设置背景颜色。
    lv_obj_set_style_bg_opa(shape, LV_OPA_COVER, LV_PART_MAIN); // 设置背景完全不透明。
    lv_obj_set_style_border_width(shape, 0, LV_PART_MAIN); // 默认不绘制边框。
    lv_obj_set_style_radius(shape, 0, LV_PART_MAIN); // 默认使用直角。
    lv_obj_set_style_pad_all(shape, 0, LV_PART_MAIN); // 清除内部留白。
    lv_obj_remove_flag(shape, LV_OBJ_FLAG_SCROLLABLE); // 禁止矩形滚动。
    lv_obj_remove_flag(shape, LV_OBJ_FLAG_CLICKABLE); // 禁止矩形抢占点击事件。

    return shape; // 返回矩形对象供页面继续设置。
}

/*
 * 创建一个透明背景、单像素边框的矩形。
 */
lv_obj_t *ui_create_outline(lv_obj_t *parent,
                            int16_t x,
                            int16_t y,
                            int16_t width,
                            int16_t height,
                            lv_color_t border_color)
{
    lv_obj_t *outline; // 保存新创建的描边对象。

    outline = ui_create_shape(parent, x, y, width, height, UI_COLOR_APP_BG); // 复用纯色矩形完成基础初始化。
    lv_obj_set_style_bg_opa(outline, LV_OPA_TRANSP, LV_PART_MAIN); // 将矩形内部改为透明。
    lv_obj_set_style_border_width(outline, 1, LV_PART_MAIN); // 设置单像素边框。
    lv_obj_set_style_border_color(outline, border_color, LV_PART_MAIN); // 设置边框颜色。

    return outline; // 返回描边对象供页面继续设置。
}

/*
 * 创建一个指定字体、颜色和位置的文字标签。
 */
lv_obj_t *ui_create_text(lv_obj_t *parent,
                         const char *text,
                         int16_t x,
                         int16_t y,
                         const lv_font_t *font,
                         lv_color_t color)
{
    lv_obj_t *label; // 保存新创建的文字标签。

    label = lv_label_create(parent); // 创建 LVGL 标准标签。
    lv_label_set_text(label, text); // 设置标签显示内容。
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN); // 设置标签字体。
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN); // 设置标签颜色。
    lv_obj_set_pos(label, x, y); // 设置标签左上角位置。

    return label; // 返回标签对象供页面动态刷新。
}

/*
 * 创建所有应用页面共用的页头。
 *
 * 页头只显示左上角主标题和副标题。
 * 用户已经熟悉六个实体按键，因此不再显示“返回 / ESC”等按键说明。
 */
void ui_create_page_header(lv_obj_t *parent,
                           const char *title,
                           const char *subtitle)
{
    ui_create_text(parent, title, 20, 6, &ui_font_14, UI_COLOR_TEXT); // 创建页面主标题。
    ui_create_text(parent, subtitle, 20, 23, &ui_font_12, UI_COLOR_TEXT_DIM); // 创建页面副标题。
}
