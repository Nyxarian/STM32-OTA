/*
 * 系统升级页面的对外接口。
 *
 * 当前接口只服务于前端演示页面；真实 OTA 接入后，
 * 页面仍然可以由这些接口创建、接收按键并在离开时释放资源。
 */
#ifndef __SYSTEM_UPGRADE_PAGE_H__
#define __SYSTEM_UPGRADE_PAGE_H__

#include "lvgl.h"
#include "BSP_Key.h"

/* 创建系统升级页面。 */
void ui_page_system_upgrade_create(lv_obj_t *parent);

/* 处理系统升级页面收到的按键。 */
void ui_page_system_upgrade_on_key(BSP_Key_t key);

/* 离开页面时释放升级演示定时器。 */
void ui_page_system_upgrade_leave(void);

#endif /* __SYSTEM_UPGRADE_PAGE_H__ */
