#ifndef __UI_NAV_H__
#define __UI_NAV_H__

#include <stdint.h>

#include "lvgl.h"
#include "BSP_Key.h"

/*
 * 页面管理器(ui_nav):
 * 所有页面都登记在 ui_nav_pages 表里,切换/返回由栈管理。
 * 每个页面 = create(建内容) + on_key(可选,页面级按键处理)。
 */

/* 页面描述 */
typedef struct
{
    const char *name;                    /* 页面名(状态栏显示,调试用) */
    void (*create)(lv_obj_t *parent);    /* 创建页面内容 */
    void (*on_key)(BSP_Key_t key);       /* 页面级按键处理(可为 NULL) */
    void (*leave)(void);                 /* 离开页面前释放页面资源(可为 NULL) */
} UiNav_Page_t;

/*
 * 页面编号。
 *
 * 此处的编号必须与 ui_nav.c 中 ui_nav_pages[] 注册表的顺序完全一致。
 * 例如：
 * UI_PAGE_TETRIS = 3U，
 * 则 ui_nav_pages[3] 必须就是俄罗斯方块页面。
 */
#define UI_PAGE_BOOT        0U  /* 锁屏页面 */
#define UI_PAGE_DESKTOP     1U  /* 应用桌面 */
#define UI_PAGE_PLACEHOLDER 2U  /* 尚未开发完成的通用占位页面 */
#define UI_PAGE_TETRIS      3U  /* 俄罗斯方块页面 */
#define UI_PAGE_SETTINGS    4U  /* 系统设置页面 */
#define UI_PAGE_MONITOR     5U  /* 系统监控页面 */
#define UI_PAGE_WIFI        6U  /* ESP8266 WiFi 页面 */
/*
 * 页面总数量。
 *
 * 页面编号从 0 开始，目前最大编号为 5，
 * 因此总数必须是 6，而不是 5。
 */
#define UI_PAGE_WEATHER     7U  /* weather frontend page */
#define UI_PAGE_SYSTEM_UPGRADE 9U  /* 系统升级前端演示页面 */
#define UI_PAGE_AT_TEST     10U  /* ESP8266 原始 AT 与 API 诊断 */
#define UI_PAGE_COUNT       11U

void ui_nav_init(void);                          /* 初始化:背景/状态栏/容器,进锁屏 */
void ui_nav_push(uint8_t page_index);            /* 打开新页面(压栈) */
void ui_nav_pop(void);                           /* 返回上一页(弹栈) */
void ui_nav_handle_key(BSP_Key_t key);           /* 按键分发(任务循环调用) */
void ui_nav_lock(void);                           /* 清空页面栈并回到锁屏 */
uint8_t ui_nav_get_current_page(void);            /* 获取当前页面编号 */
uint32_t ui_nav_get_idle_ms(void);                /* 获取无按键活动时间 */

/* 占位页:设置要显示的应用名(桌面按钮点击时调用) */
void ui_page_placeholder_set_title(const char *title);

#endif /* __UI_NAV_H__ */
