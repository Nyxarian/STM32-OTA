#ifndef __AT_TEST_PAGE_H__
#define __AT_TEST_PAGE_H__

#include "lvgl.h"
#include "BSP_Key.h"

/* 创建 ESP8266 原始 AT 与 HTTP/SSL 测试页面。 */
void ui_page_at_test_create(lv_obj_t *parent);

/* 处理测试页的六按键操作。 */
void ui_page_at_test_on_key(BSP_Key_t key);

/* 离开页面时释放页面资源并恢复普通 WiFi 状态机。 */
void ui_page_at_test_leave(void);

#endif /* __AT_TEST_PAGE_H__ */
