/*
 * 系统监控运行时统计实现。
 *
 * FreeRTOS 的 Idle Hook 在没有更高优先级任务运行时被调用。
 * 通过统计一段时间内 Idle Hook 的调用次数，可以得到一个
 * 不依赖具体 CPU 主频的相对空闲比例：
 *
 *   CPU 使用率 = 100% - 当前空闲计数增量 / 基准空闲计数增量
 *
 * 该方法用于显示趋势和负载级别，不替代硬件级周期分析工具。
 */
#include "monitor_stats.h"
#include "FreeRTOS.h"

/* 该变量会在中断或任务切换之外被 Idle Hook 更新，因此使用 volatile。 */
static volatile uint32_t monitor_idle_counter;

/*
 * FreeRTOS 空闲任务回调。
 *
 * FreeRTOSConfig.h 中的 configUSE_IDLE_HOOK 必须设置为 1，
 * 内核才会在空闲任务循环中调用此函数。
 */
void vApplicationIdleHook(void)
{
    monitor_idle_counter++; // 每次进入空闲任务时累计一次空闲计数
}

/*
 * 返回当前空闲任务累计计数。
 *
 * uint32_t 在 Cortex-M4 上是原子读取宽度，
 * 监控页面读取时不会得到半个计数值。
 */
uint32_t SystemMonitor_GetIdleCounter(void)
{
    return monitor_idle_counter; // 返回当前空闲计数快照
}
