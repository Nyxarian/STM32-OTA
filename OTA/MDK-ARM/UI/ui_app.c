#include "ui_app.h"
#include "ui_theme.h"
#include "ui_nav.h"
#include "settings_page.h"
#include "lv_port_lcd_stm32.h"
#include "lvgl.h"
#include "main.h"
#include "BSP_Key.h"
#include "boot_flag.h"

/*
 * 升级入口(临时方案,后续 M5 会做成正式的"系统升级"页面):
 * 长按返回键(ESC)2 秒 → 写 RTC backup register 升级标志 → 软件复位。
 * Bootloader 读到标志后直接进入升级模式(不再需要串口发送 IAP?)。
 */
#define UPDATE_KEY_TIMER_MS   100U
#define UPDATE_KEY_HOLD_TICKS 20U   /* 100ms x 20 = 2 秒 */
#define AUTO_LOCK_CHECK_MS    1000U /* 每秒检查一次自动锁屏条件 */

static void update_key_timer_cb(lv_timer_t *timer)
{
    static uint8_t esc_press_count = 0U;

    LV_UNUSED(timer);

    if (BSP_Key_Read() == BSP_KEY_ESC)
    {
        esc_press_count++;
        if (esc_press_count >= UPDATE_KEY_HOLD_TICKS)
        {
            esc_press_count = 0U;
            BootFlag_SetUpdateRequest();
            NVIC_SystemReset();
        }
    }
    else
    {
        esc_press_count = 0U;
    }
}

/*
 * 定期检查是否需要自动锁屏。
 *
 * 当前页面已经是锁屏页时不重复重建页面；
 * 设置项为“关闭”时 timeout_ms 返回 0，因此直接跳过。
 */
static void auto_lock_timer_cb(lv_timer_t *timer)
{
    uint32_t timeout_ms; // 保存当前设置的自动锁屏超时时间

    LV_UNUSED(timer); // 本回调不需要读取 LVGL 定时器对象

    timeout_ms = Settings_GetAutoLockMs(); // 读取设置页选择的锁屏超时时间

    if (timeout_ms == 0UL) // 判断用户是否关闭了自动锁屏
    {
        return; // 关闭自动锁屏时不执行后续检查
    }

    if (ui_nav_get_current_page() == UI_PAGE_BOOT) // 判断终端是否已经处于锁屏页
    {
        return; // 已经锁屏时不重复创建锁屏页面
    }

    if (ui_nav_get_idle_ms() >= timeout_ms) // 判断无按键活动时间是否达到设定值
    {
        ui_nav_lock(); // 清空当前导航栈并返回锁屏页
    }
}

void ui_app_init(void)
{
    Settings_Init(); // 在创建页面前恢复上次确认保存的全局设置
    lv_port_lcd_set_brightness(Settings_GetBrightnessPercent()); // 将恢复的亮度应用到背光驱动
    ui_theme_apply_screen(lv_screen_active());

    /* 页面管理器:进入锁屏页 */
    ui_nav_init();

    /* 长按 ESC 2 秒进升级模式的检测定时器 */
    lv_timer_create(update_key_timer_cb, UPDATE_KEY_TIMER_MS, NULL);

    /* 每秒读取一次全局设置，根据用户选择执行自动锁屏。 */
    lv_timer_create(auto_lock_timer_cb, AUTO_LOCK_CHECK_MS, NULL);
}
