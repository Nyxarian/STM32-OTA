#include "at_test_page.h"

#include "wifi_at_test.h"
#include "ui_components.h"
#include "ui_nav.h"
#include "ui_theme.h"
#include "lv_port_indev.h"

#include <stdio.h>

#define AT_TEST_RESPONSE_LINE_COUNT 6U

static lv_obj_t *at_test_parent;                                       /* 页面根容器。 */
static lv_timer_t *at_test_refresh_timer;                              /* 原始回包刷新定时器。 */
static lv_obj_t *at_test_step_label;                                   /* 当前步骤标签。 */
static lv_obj_t *at_test_command_label;                                /* 当前 AT 命令标签。 */
static lv_obj_t *at_test_status_label;                                 /* WAIT / OK / FAILED 状态标签。 */
static lv_obj_t *at_test_response_labels[AT_TEST_RESPONSE_LINE_COUNT]; /* 最近六行原始回包。 */
static uint32_t at_test_last_revision;                                 /* 已显示的数据版本。 */

/* 将 AT 后端当前状态同步到 LCD 标签。 */
static void at_test_page_refresh(void)
{
    char step_text[40];
    uint8_t index;
    uint8_t count;

    if (at_test_parent == NULL)
    {
        return;                                                         /* 离页后控件已被删除，不再访问。 */
    }

    index = WifiAtTest_GetSelectedIndex();                              /* 当前步骤从零开始的下标。 */
    count = WifiAtTest_GetCommandCount();                               /* 固定测试步骤总数。 */
    (void)snprintf(step_text,
                   sizeof(step_text),
                   "STEP %02u/%02u  %s",
                   (unsigned int)(index + 1U),
                   (unsigned int)count,
                   WifiAtTest_GetStepName());                           /* 拼出带序号的步骤标题。 */
    lv_label_set_text(at_test_step_label, step_text);                    /* 更新步骤标题。 */
    lv_label_set_text(at_test_command_label, WifiAtTest_GetCommandText()); /* 更新将发送的命令。 */
    lv_label_set_text(at_test_status_label, WifiAtTest_GetStatusText()); /* 更新当前结果。 */
    lv_obj_set_style_text_color(at_test_status_label,
                                (WifiAtTest_IsBusy() != 0U) ? UI_COLOR_LIGHT_BLUE : UI_COLOR_TEXT,
                                LV_PART_MAIN);                           /* 等待时使用浅蓝，结束后白色。 */

    for (index = 0U; index < AT_TEST_RESPONSE_LINE_COUNT; index++)
    {
        lv_label_set_text(at_test_response_labels[index],
                          WifiAtTest_GetResponseLine(index));            /* 逐行显示 ESP 原始输出。 */
    }
}

/* 后端接收到新字节时才更新界面，减少 LVGL 文本分配。 */
static void at_test_page_refresh_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);

    if (at_test_last_revision != WifiAtTest_GetRevision())
    {
        at_test_last_revision = WifiAtTest_GetRevision();                /* 记录已显示版本。 */
        at_test_page_refresh();                                          /* 有新数据再刷新。 */
    }
}

/* 创建 AT 测试固定页面布局。 */
void ui_page_at_test_create(lv_obj_t *parent)
{
    lv_group_t *group;
    uint8_t index;

    at_test_parent = parent;                                              /* 保存容器给刷新函数使用。 */
    group = lv_port_indev_get_group();                                    /* 取得硬件按键焦点组。 */
    if (group != NULL)
    {
        lv_group_remove_all_objs(group);                                  /* 该页直接处理按键，不保留旧焦点。 */
    }

    WifiAtTest_Enter();                                                    /* 暂停普通状态机并接管 ESP 串口。 */
    at_test_last_revision = 0U;                                           /* 强制首次完整刷新。 */

    ui_create_page_header(parent, "AT TEST", "ESP8266 / API CHECK"); /* 使用统一应用页头。 */
    at_test_step_label = ui_create_text(parent, "", 12, 47, &ui_font_12, UI_COLOR_TEXT); /* 步骤行。 */
    at_test_command_label = ui_create_text(parent, "", 12, 66, &ui_font_10, UI_COLOR_LIGHT_BLUE); /* 命令行。 */
    lv_obj_set_width(at_test_command_label, 296);                          /* 长建连命令在页面内换行。 */
    lv_label_set_long_mode(at_test_command_label, LV_LABEL_LONG_WRAP);     /* 不让文字越过右侧。 */

    ui_create_shape(parent, 12, 92, 296, 1, UI_COLOR_AUX_BLUE);           /* 上下区域分隔线。 */
    ui_create_text(parent, "RESPONSE", 12, 100, &ui_font_10, UI_COLOR_TEXT_DIM); /* 原始回包标题。 */
    at_test_status_label = ui_create_text(parent, "", 210, 100, &ui_font_10, UI_COLOR_LIGHT_BLUE); /* 结果状态。 */
    ui_create_outline(parent, 12, 114, 296, 91, UI_COLOR_AUX_BLUE);       /* 固定高度回包窗口。 */

    for (index = 0U; index < AT_TEST_RESPONSE_LINE_COUNT; index++)
    {
        at_test_response_labels[index] = ui_create_text(parent,
                                                         "",
                                                         18,
                                                         (int16_t)(120 + index * 14U),
                                                         &ui_font_10,
                                                         UI_COLOR_TEXT);  /* 每行使用固定坐标，回包不会撑大页面。 */
    }

    at_test_page_refresh();                                                /* 立即显示第一条测试命令。 */
    at_test_last_revision = WifiAtTest_GetRevision();                     /* 与后端当前版本同步。 */
    at_test_refresh_timer = lv_timer_create(at_test_page_refresh_timer_cb, 100U, NULL); /* 低延迟显示回包。 */
}

/* 左右选择、确认发送、上键清屏、ESC 返回网络页面。 */
void ui_page_at_test_on_key(BSP_Key_t key)
{
    if (key == BSP_KEY_ESC)
    {
        ui_nav_pop();                                                      /* 导航器会先调用本页 leave()。 */
    }
    else if (key == BSP_KEY_LEFT)
    {
        WifiAtTest_Select(-1);                                             /* 选择上一条测试。 */
        at_test_page_refresh();
        at_test_last_revision = WifiAtTest_GetRevision();
    }
    else if (key == BSP_KEY_RIGHT)
    {
        WifiAtTest_Select(1);                                              /* 选择下一条测试。 */
        at_test_page_refresh();
        at_test_last_revision = WifiAtTest_GetRevision();
    }
    else if (key == BSP_KEY_ENTER)
    {
        WifiAtTest_SendSelected();                                         /* STM32 通过 USART1 发送当前 AT 命令。 */
        at_test_page_refresh();
        at_test_last_revision = WifiAtTest_GetRevision();
    }
    else if (key == BSP_KEY_UP)
    {
        WifiAtTest_ClearResponse();                                        /* 便于重复查看新的原始结果。 */
        at_test_page_refresh();
        at_test_last_revision = WifiAtTest_GetRevision();
    }
}

/* 删除刷新定时器，并把 USART1 还给普通无线网络功能。 */
void ui_page_at_test_leave(void)
{
    if (at_test_refresh_timer != NULL)
    {
        lv_timer_delete(at_test_refresh_timer);                            /* 防止离页后回调访问旧标签。 */
        at_test_refresh_timer = NULL;
    }

    WifiAtTest_Leave();                                                     /* 恢复普通 WiFi 状态查询。 */
    at_test_parent = NULL;                                                  /* 标记页面已经失效。 */
    at_test_step_label = NULL;
    at_test_command_label = NULL;
    at_test_status_label = NULL;
}
