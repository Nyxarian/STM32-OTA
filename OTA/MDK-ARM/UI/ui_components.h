#ifndef __UI_COMPONENTS_H__
#define __UI_COMPONENTS_H__

/*
 * 通用 UI 组件接口。
 *
 * 本模块位于“主题层”和“业务页面层”之间：
 *
 * ui_theme.c/.h：
 *   定义颜色、字体等视觉规范，不负责创建具体控件。
 *
 * ui_components.c/.h：
 *   依据主题规范创建多个页面都能复用的基础控件。
 *
 * Apps/Tetris、Apps/Settings 等业务页面：
 *   只描述自己的页面布局和业务交互，不重复设置基础样式。
 *
 * 本文件只声明通用创建函数，例如：
 * - 纯色矩形；
 * - 描边矩形；
 * - 文字标签；
 * - 统一页面标题栏。
 *
 * 不应放入本模块的内容：
 * - 俄罗斯方块的棋盘绘制、方块颜色和游戏定时器；
 * - 设置项的选中状态、数值调整和 Flash 保存；
 * - WiFi、天气、OTA 等应用专属业务逻辑。
 */

#include "lvgl.h"

/*
 * 创建一个不响应点击和滚动的纯色矩形。
 *
 * 适用场景：
 * - 页面分割线；
 * - 应用背景块；
 * - 信息面板底色；
 * - 棋盘或预览区域背景。
 *
 * parent：创建对象的父容器。
 * x / y：矩形左上角相对父容器的坐标。
 * width / height：矩形尺寸，单位为像素。
 * color：矩形背景颜色。
 *
 * 返回值：创建成功后的 LVGL 对象指针。
 * 调用者可以继续通过返回指针设置边框、事件或动态刷新样式。
 */
lv_obj_t *ui_create_shape(lv_obj_t *parent,
                          int16_t x,
                          int16_t y,
                          int16_t width,
                          int16_t height,
                          lv_color_t color);

/*
 * 创建一个透明背景、单像素边框的矩形。
 *
 * 适用场景：
 * - 信息框轮廓；
 * - NEXT 方块预览框；
 * - 设置项选中框；
 * - 状态区域分组框。
 *
 * 参数含义与 ui_create_shape() 相同。
 * border_color 用于指定一像素边框颜色。
 *
 * 返回值：创建成功后的 LVGL 对象指针。
 */
lv_obj_t *ui_create_outline(lv_obj_t *parent,
                            int16_t x,
                            int16_t y,
                            int16_t width,
                            int16_t height,
                            lv_color_t border_color);

/*
 * 创建文字标签。
 *
 * parent：创建标签的父容器。
 * text：初始显示内容。字符串常量可直接传入。
 * x / y：文字左上角相对父容器的坐标。
 * font：要使用的 LVGL 字体，例如 ui_font_10、ui_font_12。
 * color：文字颜色。
 *
 * 返回值：标签对象指针。
 * 必须保存该指针的场景：分数、状态、网络状态等需要动态改变文字的控件。
 */
lv_obj_t *ui_create_text(lv_obj_t *parent,
                         const char *text,
                         int16_t x,
                         int16_t y,
                         const lv_font_t *font,
                         lv_color_t color);

/*
 * 创建统一的应用页头。
 *
 * 此函数固定创建四个视觉元素：
 * - 左上角主标题；
 * - 主标题下方的模块说明；
 * - 右上角 ESC BACK 返回提示；
 * - 页头下方的克莱因蓝分割线。
 *
 * parent：页面根容器。
 * title：应用名称，例如 "TETRIS"、"SETTINGS"。
 * subtitle：应用分类或编号，例如 "GAME / 01"。
 *
 * 页头不包含实际的 ESC 按键处理。
 * 每个业务页面仍需要在自己的 on_key() 中调用 ui_nav_pop()。
 */
void ui_create_page_header(lv_obj_t *parent,
                           const char *title,
                           const char *subtitle);

#endif /* __UI_COMPONENTS_H__ */
