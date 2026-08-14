#include "ui_nav.h"
#include "ui_theme.h"
#include "ui_wallpaper.h"
#include "lv_port_indev.h"
#include "lvgl.h"

/*
 * 锁屏页面。
 *
 * 页面只负责显示壁纸，确认键解锁的实际处理仍由 on_key() 完成。
 * 不再显示“PRESS ENTER”等按键提示。
 */
void ui_page_boot_create(lv_obj_t *parent)
{
    lv_group_t *group; // 保存当前输入设备焦点组。
    lv_obj_t *wall; // 保存锁屏壁纸对象。

    group = lv_port_indev_get_group(); // 读取当前按键输入组。
    if (group != NULL) // 判断输入组是否有效。
    {
        lv_group_remove_all_objs(group); // 清除上一页面残留的焦点对象。
    }

    wall = lv_image_create(parent); // 创建壁纸图片对象。
    lv_image_set_src(wall, &ui_wallpaper); // 设置壁纸图片资源。
    lv_obj_set_size(wall, 320, 240); // 让壁纸覆盖横屏 LCD。
    lv_obj_set_pos(wall, 0, 0); // 将壁纸放到屏幕左上角。
}

/* 处理锁屏页面按键。 */
void ui_page_boot_on_key(BSP_Key_t key)
{
    if (key == BSP_KEY_ENTER) // 判断是否按下确认键。
    {
        ui_nav_push(UI_PAGE_DESKTOP); // 解锁并进入桌面。
    }
}
