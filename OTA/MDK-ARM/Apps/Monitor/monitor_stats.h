/*
 * 系统监控运行时统计接口。
 *
 * 本文件只暴露系统监控需要的低层统计数据：
 * - FreeRTOS Idle Hook 计数；
 * - 由监控页面使用的采样接口。
 *
 * 页面绘制和按键处理放在 monitor_page.c，
 * 这样统计来源不会和 LVGL 控件生命周期耦合。
 */
#ifndef __MONITOR_STATS_H__
#define __MONITOR_STATS_H__

#include <stdint.h>

/*
 * 获取 FreeRTOS 空闲任务累计运行计数。
 *
 * 该计数由 vApplicationIdleHook() 每次进入空闲任务时递增。
 * 监控页面通过相邻两个采样周期的差值估算 CPU 空闲比例。
 */
uint32_t SystemMonitor_GetIdleCounter(void);

#endif /* __MONITOR_STATS_H__ */
