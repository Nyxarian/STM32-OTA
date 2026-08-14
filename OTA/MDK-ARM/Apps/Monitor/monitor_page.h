/*
 * 系统监控页面对外接口。
 *
 * 页面只通过导航器调用以下三个函数：
 * - 创建监控页面；
 * - 接收监控页面按键；
 * - 离开页面时释放周期刷新定时器。
 */
#ifndef __MONITOR_PAGE_H__
#define __MONITOR_PAGE_H__

#include "lvgl.h"
#include "BSP_Key.h"

/*
 * 创建系统监控页面。
 *
 * parent：导航器传入的页面容器。
 * 所有监控控件都创建在该容器中。
 */
void ui_page_monitor_create(lv_obj_t *parent);

/*
 * 处理系统监控页面按键。
 *
 * ESC：返回桌面。
 * 其他按键：监控页没有编辑动作，保持页面不变。
 */
void ui_page_monitor_on_key(BSP_Key_t key);

/*
 * 离开系统监控页面前释放页面私有定时器。
 *
 * 导航器在清空页面控件之前调用此函数，
 * 防止定时器继续访问已经删除的标签对象。
 */
void ui_page_monitor_leave(void);

#endif /* __MONITOR_PAGE_H__ */
