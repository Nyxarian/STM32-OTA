#ifndef __WIFI_PAGE_H__
#define __WIFI_PAGE_H__

#include "lvgl.h"
#include "BSP_Key.h"

/* 创建无线网络状态、扫描列表和密码输入页面。 */
void ui_page_wifi_create(lv_obj_t *parent);

/* 处理无线网络页面的六个实体按键。 */
void ui_page_wifi_on_key(BSP_Key_t key);

/* 离开页面时释放定时器并清空页面对象指针。 */
void ui_page_wifi_leave(void);

#endif /* __WIFI_PAGE_H__ */
