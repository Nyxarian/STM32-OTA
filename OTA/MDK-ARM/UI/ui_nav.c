#include "ui_nav.h"
#include "ui_theme.h"
#include "lv_port_indev.h"
#include "tetris_page.h"
#include "settings_page.h"
#include "monitor_page.h"
#include "wifi_page.h"
#include "at_test_page.h"
#include "weather_page.h"
#include "system_upgrade_page.h"

#define UI_NAV_STACK_MAX 8U

static lv_obj_t *page_container;                 /* 所有页面共用的容器 */
static uint8_t page_stack[UI_NAV_STACK_MAX];     /* 页面栈 */
static uint8_t stack_depth;                      /* 当前栈深度 */
static uint32_t nav_last_activity_tick;          /* 最近一次按键活动的 LVGL tick */
static uint8_t visible_page_index;               /* 当前正在显示的页面编号 */
static uint8_t page_is_visible;                  /* 当前是否已经创建过页面 */

/* 状态栏控件 */
/* 页面函数声明(实现分散在各 ui_pages_*.c) */
extern void ui_page_boot_create(lv_obj_t *parent);
extern void ui_page_boot_on_key(BSP_Key_t key);
extern void ui_page_desktop_create(lv_obj_t *parent);
extern void ui_page_desktop_on_key(BSP_Key_t key);
extern void ui_page_placeholder_create(lv_obj_t *parent);
extern void ui_page_placeholder_on_key(BSP_Key_t key);

/*
 * 页面注册表。
 *
 * 数组下标必须与 ui_nav.h 中的页面编号保持一致：
 *
 * 下标 0：锁屏页面
 * 下标 1：桌面页面
 * 下标 2：通用占位页面
 * 下标 3：俄罗斯方块页面
 * 下标 4：系统设置页面
 * 下标 5：系统监控页面
 */
const UiNav_Page_t ui_nav_pages[UI_PAGE_COUNT] =
{
    /* UI_PAGE_BOOT */
    {
        "LOCK",
        ui_page_boot_create,
        ui_page_boot_on_key,
        NULL
    },
    /* UI_PAGE_DESKTOP */
    {
        "DESKTOP",
        ui_page_desktop_create,
        ui_page_desktop_on_key,
        NULL
    },
    /* UI_PAGE_PLACEHOLDER */
    {
        "APP",
        ui_page_placeholder_create,
        ui_page_placeholder_on_key,
        NULL
    },
    /* UI_PAGE_TETRIS */
    {
        "TETRIS",
        ui_page_tetris_create,
        ui_page_tetris_on_key,
        ui_page_tetris_leave
    },
    /* UI_PAGE_SETTINGS */
    {
        "SETTINGS",
        ui_page_settings_create,
        ui_page_settings_on_key,
        NULL
    },
    /* UI_PAGE_MONITOR */
    {
        "MONITOR",
        ui_page_monitor_create,
        ui_page_monitor_on_key,
        ui_page_monitor_leave
    },
    {
        "WIFI",
        ui_page_wifi_create,
        ui_page_wifi_on_key,
        ui_page_wifi_leave
    },
    {
        "WEATHER",
        ui_page_weather_create,
        ui_page_weather_on_key,
        ui_page_weather_leave
    },
    /* UI_PAGE_RESERVED_8: 为历史页面编号保留一个空位，保持导航编号稳定。 */
    {
        "RESERVED",
        ui_page_placeholder_create,
        ui_page_placeholder_on_key,
        NULL
    },
    /* UI_PAGE_SYSTEM_UPGRADE */
    {
        "SYSTEM UPGRADE",
        ui_page_system_upgrade_create,
        ui_page_system_upgrade_on_key,
        ui_page_system_upgrade_leave
    },
    /* UI_PAGE_AT_TEST */
    {
        "AT TEST",
        ui_page_at_test_create,
        ui_page_at_test_on_key,
        ui_page_at_test_leave
    }
};

/* 创建科技网格背景(20px 间距,克莱因蓝,低透明度) */
static void ui_nav_create_grid(lv_obj_t *screen)
{
    lv_obj_t *line;
    uint16_t x;

    for (x = 0U; x < 320U; x += 20U)
    {
        line = lv_obj_create(screen);
        lv_obj_set_size(line, 1, 240);
        lv_obj_set_pos(line, x, 0);
        lv_obj_set_style_bg_color(line, UI_COLOR_KL, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(line, LV_OPA_10, LV_PART_MAIN);
        lv_obj_set_style_border_width(line, 0, LV_PART_MAIN);
        lv_obj_remove_flag(line, LV_OBJ_FLAG_SCROLLABLE);
    }

    /* 横向网格线会穿过页面文字，因此只保留纵向网格线。 */
}

/* 顶部/底部装饰线 */
static void ui_nav_create_deco_lines(lv_obj_t *screen)
{
    lv_obj_t *top = lv_obj_create(screen);
    lv_obj_set_size(top, 304, 1);
    lv_obj_set_pos(top, 8, 1); // 将顶部装饰线放在文字上方
    lv_obj_set_style_bg_color(top, UI_COLOR_KL, LV_PART_MAIN);
    lv_obj_set_style_border_width(top, 0, LV_PART_MAIN);
    lv_obj_remove_flag(top, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *bottom = lv_obj_create(screen);
    lv_obj_set_size(bottom, 304, 1);
    lv_obj_set_pos(bottom, 8, 239); // 将底部装饰线下移到屏幕最底部
    lv_obj_set_style_bg_color(bottom, UI_COLOR_KL, LV_PART_MAIN);
    lv_obj_set_style_border_width(bottom, 0, LV_PART_MAIN);
    lv_obj_remove_flag(bottom, LV_OBJ_FLAG_SCROLLABLE);
}

/* 显示指定页面:清空容器并重建页面 */
static void ui_nav_show_page(uint8_t index)
{
    if ((page_is_visible != 0U) &&
        (visible_page_index < UI_PAGE_COUNT) &&
        (ui_nav_pages[visible_page_index].leave != NULL))
    {
        ui_nav_pages[visible_page_index].leave(); /* 删除旧页面前释放其定时器等资源 */
    }

    lv_obj_clean(page_container);
    page_is_visible = 0U; /* 清空容器后暂时不存在有效页面 */

    if ((index < UI_PAGE_COUNT) && (ui_nav_pages[index].create != NULL))
    {
        visible_page_index = index; /* 保存即将显示的页面编号 */
        page_is_visible = 1U; /* 标记页面容器已经拥有有效页面 */
        ui_nav_pages[index].create(page_container);
    }

}

void ui_nav_init(void)
{
    lv_obj_t *screen = lv_screen_active();

    lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF); // 关闭根屏幕的水平和垂直滚动条
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE); // 根屏幕不参与页面滚动

    /* 背景层:网格 + 装饰线(在所有页面之下) */
    ui_nav_create_grid(screen);
    ui_nav_create_deco_lines(screen);

    /* 页面容器:铺满屏幕,透明背景 */
    page_container = lv_obj_create(screen);
    lv_obj_set_size(page_container, 320, 240);
    lv_obj_set_pos(page_container, 0, 0);
    lv_obj_remove_flag(page_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(page_container, LV_SCROLLBAR_MODE_OFF); // 关闭页面容器的滚动条
    lv_obj_set_style_bg_opa(page_container, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(page_container, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(page_container, 0, LV_PART_MAIN);

    stack_depth = 0U;
    page_is_visible = 0U; /* 初始化时页面容器尚未创建任何业务页面 */
    nav_last_activity_tick = lv_tick_get(); /* 从锁屏页面创建时开始统计无操作时间 */
    ui_nav_push(UI_PAGE_BOOT);   /* 开机进锁屏 */
}

void ui_nav_push(uint8_t index)
{
    if (stack_depth >= UI_NAV_STACK_MAX)
    {
        return;   /* 栈满,丢弃(正常不会发生) */
    }

    page_stack[stack_depth] = index;
    stack_depth++;
    ui_nav_show_page(index);
    lv_port_indev_suppress_key_until_release();
}

void ui_nav_pop(void)
{
    if (stack_depth <= 1U)
    {
        return;   /* 至少保留根页面,不能弹空 */
    }

    stack_depth--;
    ui_nav_show_page(page_stack[stack_depth - 1U]);
    lv_port_indev_suppress_key_until_release();
}

void ui_nav_handle_key(BSP_Key_t key)
{
    const UiNav_Page_t *page; /* 保存当前栈顶页面的函数表指针 */

    if (key == BSP_KEY_NONE) /* 无按键不应刷新自动锁屏计时 */
    {
        return; /* 没有按键时直接结束分发 */
    }

    if (stack_depth == 0U) /* 防止导航器尚未初始化时访问空栈 */
    {
        return; /* 没有当前页面时不能继续分发按键 */
    }

    nav_last_activity_tick = lv_tick_get(); /* 任何有效按键都刷新无操作计时 */
    page = &ui_nav_pages[page_stack[stack_depth - 1U]]; /* 根据栈顶编号取得当前页面表项 */

    if (page->on_key != NULL)
    {
        page->on_key(key);
    }

    return; /* 明确结束本次按键分发，避免 ARMCC 将函数尾部错误优化为循环 */
}

/*
 * 清空当前页面栈并重新显示锁屏页。
 *
 * 自动锁屏和后续系统级安全操作都调用本函数，
 * 这样不会在桌面、设置或某个应用页面中残留旧的导航路径。
 */
void ui_nav_lock(void)
{
    stack_depth = 0U; /* 丢弃当前桌面和应用页面栈 */
    nav_last_activity_tick = lv_tick_get(); /* 锁屏后重新开始计算无操作时间 */
    ui_nav_push(UI_PAGE_BOOT); /* 将锁屏页作为新的根页面显示 */
}

/*
 * 获取当前页面编号。
 *
 * 导航栈为空时返回锁屏页编号，避免调用者访问空栈。
 */
uint8_t ui_nav_get_current_page(void)
{
    if (stack_depth == 0U) /* 判断导航器是否已经初始化 */
    {
        return UI_PAGE_BOOT; /* 未初始化时按锁屏状态处理 */
    }

    return page_stack[stack_depth - 1U]; /* 返回栈顶页面编号 */
}

/*
 * 获取最近一次按键后的无操作时间，单位为毫秒。
 */
uint32_t ui_nav_get_idle_ms(void)
{
    return lv_tick_get() - nav_last_activity_tick; /* 使用无符号减法兼容 tick 溢出 */
}
