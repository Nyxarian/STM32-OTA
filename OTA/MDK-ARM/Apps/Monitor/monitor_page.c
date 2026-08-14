/*
 * 系统监控页面。
 *
 * 页面职责：
 * 1. 创建 320x240 横屏 LCD 上的监控布局；
 * 2. 每秒读取一次 FreeRTOS、LVGL 和系统运行数据；
 * 3. 将最新数据刷新到已经创建的标签；
 * 4. 响应 ESC 返回桌面，并在离开时释放刷新定时器。
 *
 * 页面不负责创建任务，也不负责修改调度器配置。
 * 统计来源由 monitor_stats.c、FreeRTOS 和 LVGL 提供。
 */
#include "monitor_page.h"
#include "monitor_stats.h"
#include "ui_components.h"
#include "ui_nav.h"
#include "ui_theme.h"
#include "main.h"

#include "FreeRTOS.h"
#include "task.h"
#include "portable.h"
#include "src/stdlib/lv_mem.h"

#include <stdint.h>
#include <stddef.h>

/* 页面刷新周期，单位为毫秒。 */
#define MONITOR_REFRESH_PERIOD_MS 1000U

/* 监控页面最多读取的任务数量。 */
#define MONITOR_TASK_MAX_COUNT 8U

/* 监控页面最多显示的任务行数。 */
#define MONITOR_TASK_ROW_COUNT 3U

/* 监控页面的四个主要指标面板。 */
#define MONITOR_PANEL_W 146
#define MONITOR_PANEL_H 53
#define MONITOR_PANEL_LEFT_X 12
#define MONITOR_PANEL_RIGHT_X 162
#define MONITOR_PANEL_TOP_Y 40
#define MONITOR_PANEL_BOTTOM_Y 99

/* 监控页面底部摘要和任务明细的布局坐标。 */
#define MONITOR_TASK_TITLE_Y        155
#define MONITOR_TASK_SUMMARY_Y      169
#define MONITOR_TASK_DIVIDER_Y      185
#define MONITOR_TASK_DETAIL_TITLE_Y 188
#define MONITOR_TASK_FIRST_Y        202
#define MONITOR_TASK_ROW_GAP        12

/* 保存四个主要指标的动态标签。 */
static lv_obj_t *monitor_cpu_label;
static lv_obj_t *monitor_ram_label;
static lv_obj_t *monitor_lvgl_label;
static lv_obj_t *monitor_uptime_label;

/* 保存三个摘要数值的动态标签：最小堆、UI 栈余量、任务数量。 */
static lv_obj_t *monitor_summary_labels[3];

/* 保存三个任务信息行的动态标签。 */
static lv_obj_t *monitor_task_labels[MONITOR_TASK_ROW_COUNT];

/* 保存周期刷新定时器，离开页面时必须删除。 */
static lv_timer_t *monitor_refresh_timer;

/* 保存 CPU 采样的上一次 Idle Hook 计数。 */
static uint32_t monitor_last_idle_counter;

/* 保存 CPU 采样基准周期内的 Idle Hook 增量。 */
static uint32_t monitor_idle_baseline;

/* 标记 CPU 基准是否已经完成第一次采样。 */
static uint8_t monitor_cpu_baseline_ready;

/* 标记页面刚创建后的第一次刷新，第一次刷新不参与 CPU 基准计算。 */
static uint8_t monitor_cpu_warmup;

/*
 * 向字符串缓冲区追加一个无符号十进制整数。
 *
 * 该函数不使用 sprintf，避免为一个小型监控页面引入较大的格式化库。
 * buffer 必须至少能够容纳 value 的十进制字符和结尾的 '\0'。
 */
static uint8_t monitor_append_uint(char *buffer, uint8_t position, uint32_t value)
{
    char digits[10]; // 保存倒序生成的十进制数字
    uint8_t digit_count; // 保存当前数字的位数
    uint8_t i; // 保存反向复制数字时的循环下标

    digit_count = 0U; // 初始时还没有生成数字

    if (value == 0UL) // 单独处理数值 0
    {
        buffer[position] = '0'; // 写入唯一的零字符
        return (uint8_t)(position + 1U); // 返回追加后的下一个位置
    }

    while (value > 0UL) // 按低位到高位分解十进制数字
    {
        digits[digit_count] = (char)('0' + (value % 10UL)); // 保存当前最低位数字
        digit_count++; // 记录已经保存的数字位数
        value /= 10UL; // 删除已经处理的最低位
    }

    for (i = 0U; i < digit_count; i++) // 将倒序数字重新按正常顺序写入目标缓冲区
    {
        buffer[position] = digits[digit_count - 1U - i]; // 复制当前应该显示的数字
        position++; // 移动到下一个目标位置
    }

    return position; // 返回追加完成后的下一个位置
}

/*
 * 格式化百分比文本，例如 0、68、100。
 */
static void monitor_format_percent(char *buffer, uint32_t percent)
{
    uint8_t position; // 保存当前写入位置

    if (percent > 100UL) // 限制百分比最大值
    {
        percent = 100UL; // 超出范围时按 100% 显示
    }

    position = monitor_append_uint(buffer, 0U, percent); // 写入百分比数字
    buffer[position] = '%'; // 写入百分号
    buffer[position + 1U] = '\0'; // 写入字符串结束符
}

/*
 * 格式化一个不带单位的无符号整数。
 *
 * 该函数专门用于任务数量等纯数字字段，
 * 与百分比和内存格式化函数分开，避免刷新逻辑重复拼接字符串结束符。
 */
static void monitor_format_uint(char *buffer, uint32_t value)
{
    uint8_t position; // 保存当前已经写入的字符数量

    position = monitor_append_uint(buffer, 0U, value); // 将数值写入字符串缓冲区
    buffer[position] = '\0'; // 在数字末尾补上 C 字符串结束符
}

/*
 * 格式化内存文本。
 *
 * 大于等于 1 KB 时显示为 K，小于 1 KB 时显示为 B，
 * 避免把 600 多字节错误显示成没有意义的 0K。
 */
static void monitor_format_memory(char *buffer, uint32_t bytes)
{
    uint8_t position; // 保存当前写入位置

    if (bytes >= 1024UL) // 判断是否达到 1 KB
    {
        position = monitor_append_uint(buffer, 0U, bytes / 1024UL); // 将字节数换算为整数 KB
        buffer[position] = 'K'; // 写入 KB 单位
    }
    else
    {
        position = monitor_append_uint(buffer, 0U, bytes); // 小于 1 KB 时直接写入字节数
        buffer[position] = 'B'; // 写入字节单位
    }

    buffer[position + 1U] = '\0'; // 写入字符串结束符
}

/*
 * 格式化运行时间。
 *
 * 显示格式为 HH:MM:SS，小时超过 99 后循环显示，
 * 这样可以在固定宽度的 LCD 上保持布局稳定。
 */
static void monitor_format_uptime(char *buffer, uint32_t milliseconds)
{
    uint32_t total_seconds; // 保存总运行秒数
    uint32_t hours; // 保存小时数
    uint32_t minutes; // 保存分钟数
    uint32_t seconds; // 保存秒数

    total_seconds = milliseconds / 1000UL; // 将系统 tick 换算成秒
    hours = (total_seconds / 3600UL) % 100UL; // 取两位小时数
    minutes = (total_seconds / 60UL) % 60UL; // 取分钟数
    seconds = total_seconds % 60UL; // 取秒数

    buffer[0] = (char)('0' + ((hours / 10UL) % 10UL)); // 写入小时十位
    buffer[1] = (char)('0' + (hours % 10UL)); // 写入小时个位
    buffer[2] = ':'; // 写入小时和分钟分隔符
    buffer[3] = (char)('0' + ((minutes / 10UL) % 10UL)); // 写入分钟十位
    buffer[4] = (char)('0' + (minutes % 10UL)); // 写入分钟个位
    buffer[5] = ':'; // 写入分钟和秒分隔符
    buffer[6] = (char)('0' + ((seconds / 10UL) % 10UL)); // 写入秒十位
    buffer[7] = (char)('0' + (seconds % 10UL)); // 写入秒个位
    buffer[8] = '\0'; // 写入字符串结束符
}

/*
 * 复制任务名并补齐为固定宽度。
 *
 * 固定宽度可以避免任务名变化时后面的栈余量字段左右跳动。
 */
static uint8_t monitor_copy_task_name(char *buffer,
                                      uint8_t position,
                                      const char *name)
{
    uint8_t copied; // 保存已经复制的任务名字节数

    copied = 0U; // 初始还没有复制任何字符

    while ((name != NULL) && (name[copied] != '\0') && (copied < 8U)) // 最多复制八个 ASCII 字符
    {
        buffer[position] = name[copied]; // 复制当前任务名字节
        position++; // 移动到下一个目标位置
        copied++; // 累加已复制字符数
    }

    while (copied < 8U) // 任务名不足八个字符时补空格
    {
        buffer[position] = ' '; // 使用空格保持后续字段对齐
        position++; // 移动到下一个目标位置
        copied++; // 累加补齐字符数
    }

    return position; // 返回任务名字段之后的位置
}

/*
 * 将 FreeRTOS 任务状态转换为短文本。
 */
static const char *monitor_task_state_text(eTaskState state)
{
    switch (state) // 根据 FreeRTOS 状态枚举选择显示文字
    {
        case eRunning:
            return "RUN"; // 当前正在运行

        case eReady:
            return "RDY"; // 已就绪等待调度

        case eBlocked:
            return "BLK"; // 阻塞等待事件或延时

        case eSuspended:
            return "SUS"; // 被挂起

        case eDeleted:
            return "DEL"; // 已删除但尚未回收

        default:
            return "UNK"; // 未知或无效状态
    }
}

/*
 * 格式化单条任务信息。
 *
 * 示例：
 *   uiTask   RDY 325W
 */
static void monitor_format_task_row(char *buffer,
                                    const TaskStatus_t *status)
{
    uint8_t position; // 保存当前写入位置
    const char *state_text; // 保存任务状态短文本

    position = monitor_copy_task_name(buffer, 0U, status->pcTaskName); // 写入固定宽度任务名
    buffer[position] = ' '; // 分隔任务名和状态
    position++; // 移动到状态字段

    state_text = monitor_task_state_text(status->eCurrentState); // 取得任务状态文字
    buffer[position] = state_text[0]; // 写入状态第一个字符
    buffer[position + 1U] = state_text[1]; // 写入状态第二个字符
    buffer[position + 2U] = state_text[2]; // 写入状态第三个字符
    position += 3U; // 移动到栈余量字段
    buffer[position] = ' '; // 分隔状态和栈余量
    position++; // 移动到栈余量数字
    position = monitor_append_uint(buffer,
                                   position,
                                   (uint32_t)status->usStackHighWaterMark); // 写入栈余量
    buffer[position] = 'W'; // 写入 FreeRTOS stack word 单位
    buffer[position + 1U] = '\0'; // 写入字符串结束符
}

/*
 * 根据 Idle Hook 计数增量估算 CPU 使用率。
 *
 * 第一次刷新只建立基准，不显示突变的假数据；
 * 后续以“当前周期空闲量 / 基准周期空闲量”计算空闲比例。
 */
static uint32_t monitor_calculate_cpu_percent(void)
{
    uint32_t current_idle_counter; // 保存本次 Idle Hook 计数
    uint32_t idle_delta; // 保存本周期的 Idle Hook 增量
    uint32_t idle_percent; // 保存估算出的 CPU 空闲百分比
    uint32_t baseline_per_percent; // 保存基准计数对应的百分之一

    current_idle_counter = SystemMonitor_GetIdleCounter(); // 读取当前空闲计数
    idle_delta = current_idle_counter - monitor_last_idle_counter; // 计算本周期空闲计数增量
    monitor_last_idle_counter = current_idle_counter; // 保存当前值供下一周期使用

    if (monitor_cpu_warmup != 0U) // 判断是否正在处理页面创建后的第一次刷新
    {
        monitor_cpu_warmup = 0U; // 后续刷新开始进入正常采样流程
        return 0UL; // 第一次刷新不使用极短时间窗口的数据
    }

    if (monitor_cpu_baseline_ready == 0U) // 判断是否还没有建立 CPU 空闲基准
    {
        if (idle_delta == 0UL) // 页面刚创建时可能还没有经过一个完整采样周期
        {
            return 0UL; // 不使用零增量建立基准，等待下一次定时刷新
        }

        monitor_idle_baseline = idle_delta; // 使用第一个完整周期作为基准
        monitor_cpu_baseline_ready = 1U; // 标记基准已经建立
        return 0UL; // 第一次采样返回 0，避免显示未校准数据
    }

    if (monitor_idle_baseline == 0UL) // 防止基准为零时发生除零
    {
        return 0UL; // 无法计算时返回 0%
    }

    if (idle_delta >= monitor_idle_baseline) // 判断当前空闲量是否达到或超过基准
    {
        idle_percent = 100UL; // 空闲量达到基准时认为 CPU 使用率接近 0%
    }
    else
    {
        baseline_per_percent = monitor_idle_baseline / 100UL; // 计算一个百分点对应的空闲计数

        if (baseline_per_percent == 0UL) // 判断基准是否太小无法分成百分之一
        {
            idle_percent = 0UL; // 基准过小时使用保守值
        }
        else
        {
            idle_percent = idle_delta / baseline_per_percent; // 计算当前 CPU 空闲百分比
        }
    }

    if (idle_percent > 100UL) // 再次限制计算结果范围
    {
        idle_percent = 100UL; // 防止异常数据超过百分之百
    }

    return 100UL - idle_percent; // 使用率等于 100% 减去空闲率
}

/*
 * 创建一个监控指标面板。
 *
 * 返回值是面板内部用于动态刷新数值的标签指针。
 */
static lv_obj_t *monitor_create_metric(lv_obj_t *parent,
                                        int16_t x,
                                        int16_t y,
                                        const char *title,
                                        const char *initial_value)
{
    lv_obj_t *value_label; // 保存面板内部的动态数值标签

    ui_create_outline(parent,
                      x,
                      y,
                      MONITOR_PANEL_W,
                      MONITOR_PANEL_H,
                      UI_COLOR_AUX_BLUE); // 创建指标面板边框
    ui_create_text(parent,
                   title,
                   x + 10,
                   y + 7,
                   &ui_font_10,
                   UI_COLOR_LIGHT_BLUE); // 创建指标标题
    value_label = ui_create_text(parent,
                                 initial_value,
                                 x + 10,
                                 y + 25,
                                 &ui_font_14,
                                 UI_COLOR_TEXT); // 创建动态指标数值

    return value_label; // 返回数值标签供刷新函数保存
}

/*
 * 刷新监控页面上的全部动态数据。
 */
static void monitor_refresh_values(void)
{
    char cpu_text[8]; // 保存 CPU 百分比文本
    char ram_text[8]; // 保存 FreeRTOS 堆占用百分比文本
    char lvgl_text[8]; // 保存 LVGL 内存占用百分比文本
    char uptime_text[12]; // 保存系统运行时间文本
    char heap_text[12]; // 保存 FreeRTOS 剩余堆文本
    char stack_text[12]; // 保存 UI 任务栈余量文本
    char task_count_text[8]; // 保存任务数量文本
    char task_rows[MONITOR_TASK_ROW_COUNT][32]; // 保存任务列表的临时文本
    TaskStatus_t task_status[MONITOR_TASK_MAX_COUNT]; // 保存 FreeRTOS 任务快照
    lv_mem_monitor_t lvgl_memory; // 保存 LVGL 内存快照
    size_t free_heap; // 保存当前 FreeRTOS 剩余堆大小
    size_t minimum_heap; // 保存 FreeRTOS 历史最小剩余堆大小
    uint32_t free_heap_percent; // 保存 FreeRTOS 堆使用百分比
    UBaseType_t task_count; // 保存当前任务总数
    UBaseType_t task_index; // 保存任务列表循环下标
    UBaseType_t ui_stack_watermark; // 保存当前 UI 任务栈最低剩余量
    uint32_t cpu_percent; // 保存当前 CPU 使用率

    cpu_percent = monitor_calculate_cpu_percent(); // 计算 CPU 使用率
    monitor_format_percent(cpu_text, cpu_percent); // 将 CPU 百分比转换为文本
    lv_label_set_text(monitor_cpu_label, cpu_text); // 刷新 CPU 标签

    free_heap = xPortGetFreeHeapSize(); // 读取 FreeRTOS 当前剩余堆
    minimum_heap = xPortGetMinimumEverFreeHeapSize(); // 读取 FreeRTOS 历史最低剩余堆
    if (free_heap >= configTOTAL_HEAP_SIZE) // 防止堆统计出现无效的负数
    {
        free_heap_percent = 0UL; // 剩余堆异常时按零占用处理
    }
    else
    {
        free_heap_percent = ((uint32_t)(configTOTAL_HEAP_SIZE - free_heap) * 100UL) /
                            (uint32_t)configTOTAL_HEAP_SIZE; // 计算 FreeRTOS 堆占用率
    }
    monitor_format_percent(ram_text, free_heap_percent); // 将堆占用率转换为文本
    lv_label_set_text(monitor_ram_label, ram_text); // 刷新 RAM 标签

    lv_mem_monitor(&lvgl_memory); // 读取 LVGL 内存池状态
    monitor_format_percent(lvgl_text, lvgl_memory.used_pct); // 将 LVGL 使用率转换为文本
    lv_label_set_text(monitor_lvgl_label, lvgl_text); // 刷新 LVGL 标签

    monitor_format_uptime(uptime_text, HAL_GetTick()); // 将系统 tick 转换为时分秒
    lv_label_set_text(monitor_uptime_label, uptime_text); // 刷新运行时间标签

    monitor_format_memory(heap_text, (uint32_t)minimum_heap); // 将历史最小剩余堆格式化为 K 或 B
    lv_label_set_text(monitor_summary_labels[0], heap_text); // 刷新历史最小剩余堆摘要

    ui_stack_watermark = uxTaskGetStackHighWaterMark(NULL); // 获取当前 UI 任务最低剩余栈空间
    monitor_format_memory(stack_text,
                          (uint32_t)ui_stack_watermark * sizeof(StackType_t)); // 将 UI 栈余量格式化为 K 或 B
    lv_label_set_text(monitor_summary_labels[1], stack_text); // 刷新 UI 任务栈余量摘要

    task_count = uxTaskGetSystemState(task_status,
                                      MONITOR_TASK_MAX_COUNT,
                                      NULL); // 获取当前 FreeRTOS 任务快照
    monitor_format_uint(task_count_text, (uint32_t)task_count); // 将任务数量转换为文本
    lv_label_set_text(monitor_summary_labels[2], task_count_text); // 刷新当前任务数量摘要

    if (task_count > MONITOR_TASK_MAX_COUNT) // 防止返回数量超过临时数组容量
    {
        task_count = MONITOR_TASK_MAX_COUNT; // 只处理能够安全保存的任务数量
    }

    for (task_index = 0U; task_index < MONITOR_TASK_ROW_COUNT; task_index++) // 遍历三个任务显示行
    {
        if (task_index < task_count) // 判断当前行是否有对应任务
        {
            monitor_format_task_row(task_rows[task_index],
                                    &task_status[task_index]); // 格式化当前任务信息
            lv_label_set_text(monitor_task_labels[task_index],
                              task_rows[task_index]); // 将任务信息写入对应标签
        }
        else
        {
            lv_label_set_text(monitor_task_labels[task_index],
                              "---"); // 没有对应任务时显示占位符
        }
    }
}

/*
 * 监控页面周期刷新回调。
 */
static void monitor_refresh_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer); // 本回调不需要访问定时器对象
    monitor_refresh_values(); // 读取系统快照并刷新页面文字
}

/*
 * 创建系统监控页面。
 */
void ui_page_monitor_create(lv_obj_t *parent)
{
    uint8_t index; // 保存任务行创建循环下标

    monitor_cpu_label = 0; // 清空旧页面留下的 CPU 标签指针
    monitor_ram_label = 0; // 清空旧页面留下的 RAM 标签指针
    monitor_lvgl_label = 0; // 清空旧页面留下的 LVGL 标签指针
    monitor_uptime_label = 0; // 清空旧页面留下的运行时间标签指针
    monitor_refresh_timer = 0; // 清空旧页面留下的定时器指针
    monitor_cpu_baseline_ready = 0U; // 重新进入页面时重新校准 CPU 基准
    monitor_cpu_warmup = 1U; // 跳过页面创建后第一次过短的 CPU 采样窗口
    monitor_idle_baseline = 0UL; // 清空旧的 CPU 空闲基准
    monitor_last_idle_counter = SystemMonitor_GetIdleCounter(); // 记录进入页面时的空闲计数

    for (index = 0U; index < MONITOR_TASK_ROW_COUNT; index++) // 清空旧页面的任务标签指针
    {
        monitor_summary_labels[index] = 0; // 清空旧页面留下的摘要标签指针
        monitor_task_labels[index] = 0; // 页面容器即将重新创建任务标签
    }

    ui_create_page_header(parent,
                          "\xe7\xb3\xbb\xe7\xbb\x9f\xe7\x9b\x91\xe6\x8e\xa7",
                          "\xe8\xbf\x90\xe8\xa1\x8c\xe7\x8a\xb6\xe6\x80\x81 / 06"); // 创建系统监控页头

    monitor_cpu_label = monitor_create_metric(parent,
                                              MONITOR_PANEL_LEFT_X,
                                              MONITOR_PANEL_TOP_Y,
                                              "CPU",
                                              "--%"); // 创建 CPU 指标面板
    monitor_ram_label = monitor_create_metric(parent,
                                              MONITOR_PANEL_RIGHT_X,
                                              MONITOR_PANEL_TOP_Y,
                                              "FREERTOS RAM",
                                              "--%"); // 创建 FreeRTOS 堆指标面板
    monitor_lvgl_label = monitor_create_metric(parent,
                                               MONITOR_PANEL_LEFT_X,
                                               MONITOR_PANEL_BOTTOM_Y,
                                               "LVGL MEM",
                                               "--%"); // 创建 LVGL 内存指标面板
    monitor_uptime_label = monitor_create_metric(parent,
                                                 MONITOR_PANEL_RIGHT_X,
                                                 MONITOR_PANEL_BOTTOM_Y,
                                                 "UPTIME",
                                                 "--:--:--"); // 创建运行时间指标面板

    ui_create_text(parent,
                   "\xe8\xbf\x90\xe8\xa1\x8c\xe8\xb5\x84\xe6\xba\x90",
                   12,
                   MONITOR_TASK_TITLE_Y,
                   &ui_font_10,
                   UI_COLOR_LIGHT_BLUE); // 创建运行资源区域标题

    ui_create_text(parent,
                   "\xe6\x9c\x80\xe5\xb0\x8f\xe5\xa0\x86",
                   12,
                   MONITOR_TASK_SUMMARY_Y,
                   &ui_font_10,
                   UI_COLOR_TEXT_DIM); // 创建历史最小剩余堆标题
    monitor_summary_labels[0] = ui_create_text(parent,
                                               "---",
                                               58,
                                               MONITOR_TASK_SUMMARY_Y,
                                               &ui_font_10,
                                               UI_COLOR_TEXT); // 创建最小堆统计标签
    ui_create_text(parent,
                   "UI\xe6\xa0\x88",
                   112,
                   MONITOR_TASK_SUMMARY_Y,
                   &ui_font_10,
                   UI_COLOR_TEXT_DIM); // 创建 UI 任务栈标题
    monitor_summary_labels[1] = ui_create_text(parent,
                                               "---",
                                               150,
                                               MONITOR_TASK_SUMMARY_Y,
                                               &ui_font_10,
                                               UI_COLOR_TEXT); // 创建 UI 栈统计标签
    ui_create_text(parent,
                   "\xe4\xbb\xbb\xe5\x8a\xa1",
                   235,
                   MONITOR_TASK_SUMMARY_Y,
                   &ui_font_10,
                   UI_COLOR_TEXT_DIM); // 创建当前任务数量标题
    monitor_summary_labels[2] = ui_create_text(parent,
                                               "---",
                                               270,
                                               MONITOR_TASK_SUMMARY_Y,
                                               &ui_font_10,
                                               UI_COLOR_TEXT); // 创建任务数统计标签

    ui_create_shape(parent,
                    12,
                     MONITOR_TASK_DIVIDER_Y,
                     296,
                     1,
                     UI_COLOR_AUX_BLUE); // 创建任务列表上方分隔线
    ui_create_text(parent,
                   "\xe4\xbb\xbb\xe5\x8a\xa1 / STATE / STACK",
                   12,
                   MONITOR_TASK_DETAIL_TITLE_Y,
                   &ui_font_10,
                   UI_COLOR_TEXT_DIM); // 创建任务明细标题

    for (index = 0U; index < MONITOR_TASK_ROW_COUNT; index++) // 创建三个任务明细行
    {
        monitor_task_labels[index] = ui_create_text(parent,
                                                    "---",
                                                    12,
                                                     MONITOR_TASK_FIRST_Y + index * MONITOR_TASK_ROW_GAP,
                                                    &ui_font_10,
                                                    UI_COLOR_TEXT); // 创建任务明细动态标签
    }

    monitor_refresh_timer = lv_timer_create(monitor_refresh_timer_cb,
                                            MONITOR_REFRESH_PERIOD_MS,
                                            NULL); // 创建每秒刷新一次的监控定时器
    monitor_refresh_values(); // 页面第一次显示时立即填充当前数据
}

/*
 * 处理系统监控页面按键。
 */
void ui_page_monitor_on_key(BSP_Key_t key)
{
    if (key == BSP_KEY_ESC) // 判断是否按下返回键
    {
        ui_nav_pop(); // 返回桌面并触发页面离开清理
    }
}

/*
 * 离开系统监控页面前释放周期刷新定时器。
 */
void ui_page_monitor_leave(void)
{
    if (monitor_refresh_timer != 0) // 判断周期刷新定时器是否存在
    {
        lv_timer_delete(monitor_refresh_timer); // 删除定时器，防止访问已销毁的标签
        monitor_refresh_timer = 0; // 清空定时器指针
    }
}
