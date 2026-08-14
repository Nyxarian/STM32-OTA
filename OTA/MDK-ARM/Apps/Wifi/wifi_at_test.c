#include "wifi_at_test.h"

#include "wifi_manager.h"
#include "wifi_uart.h"
#include "main.h"

#include <stddef.h>
#include <string.h>

/* LCD 固定显示 6 行，每行最多保留 47 个可打印字符。 */
#define WIFI_AT_TEST_VIEW_LINES        6U
#define WIFI_AT_TEST_LINE_SIZE         48U
#define WIFI_AT_TEST_RX_PER_CYCLE      128U
#define WIFI_AT_TEST_NORMAL_TIMEOUT_MS 4000U
#define WIFI_AT_TEST_CONNECT_TIMEOUT_MS 10000U
#define WIFI_AT_TEST_HTTP_TIMEOUT_MS   12000U

/* 这个 HTTP 报文固定为 142 字节，最后两个 CRLF 表示 HTTP 请求头结束。 */
static const uint8_t wifi_at_test_http_request[] =
    "GET /v1/forecast?latitude=35.30&longitude=113.93&current=temperature_2m,weather_code HTTP/1.1\r\n"
    "Host: api.open-meteo.com\r\n"
    "Connection: close\r\n"
    "\r\n";

typedef enum
{
    WIFI_AT_TEST_NORMAL = 0,
    WIFI_AT_TEST_HTTP_SEND,
    WIFI_AT_TEST_SSL_START
} WifiAtTestKind_t;

typedef struct
{
    const char *name;             /* LCD 显示的步骤名称。 */
    const char *command;          /* 实际发送给 ESP8266 的 AT 命令，末尾包含 CRLF。 */
    const char *display_command;  /* LCD 显示概要，不包含不可见的换行。 */
    uint32_t timeout_ms;          /* 当前步骤允许等待的最大时间。 */
    WifiAtTestKind_t kind;        /* 普通命令、HTTP 两阶段发送或 SSL 建连。 */
} WifiAtTestCommand_t;

/*
 * 依次验证模块、当前 WiFi 状态、普通 TCP/HTTP 和 SSL。
 * 不把 WiFi 密码写入诊断代码；网络连接仍由无线网络页面完成。
 */
static const WifiAtTestCommand_t wifi_at_test_commands[] =
{
    { "MODULE",     "AT\r\n",                                           "AT",                                WIFI_AT_TEST_NORMAL_TIMEOUT_MS,  WIFI_AT_TEST_NORMAL },
    { "VERSION",    "AT+GMR\r\n",                                       "AT+GMR",                            WIFI_AT_TEST_NORMAL_TIMEOUT_MS,  WIFI_AT_TEST_NORMAL },
    { "WIFI MODE",  "AT+CWMODE?\r\n",                                   "AT+CWMODE?",                        WIFI_AT_TEST_NORMAL_TIMEOUT_MS,  WIFI_AT_TEST_NORMAL },
    { "WIFI AP",    "AT+CWJAP?\r\n",                                    "AT+CWJAP?",                         WIFI_AT_TEST_NORMAL_TIMEOUT_MS,  WIFI_AT_TEST_NORMAL },
    { "LOCAL IP",   "AT+CIFSR\r\n",                                     "AT+CIFSR",                          WIFI_AT_TEST_NORMAL_TIMEOUT_MS,  WIFI_AT_TEST_NORMAL },
    { "SINGLE",     "AT+CIPMUX=0\r\n",                                  "AT+CIPMUX=0",                       WIFI_AT_TEST_NORMAL_TIMEOUT_MS,  WIFI_AT_TEST_NORMAL },
    { "NORMAL MODE", "AT+CIPMODE=0\r\n",                                "AT+CIPMODE=0",                      WIFI_AT_TEST_NORMAL_TIMEOUT_MS,  WIFI_AT_TEST_NORMAL },
    { "TCP API",    "AT+CIPSTART=\"TCP\",\"api.open-meteo.com\",80\r\n", "AT+CIPSTART TCP :80",     WIFI_AT_TEST_CONNECT_TIMEOUT_MS, WIFI_AT_TEST_NORMAL },
    { "HTTP DATA",  "AT+CIPSEND=142\r\n",                               "AT+CIPSEND=142 / WEATHER",          WIFI_AT_TEST_HTTP_TIMEOUT_MS,    WIFI_AT_TEST_HTTP_SEND },
    { "SSL BUFFER", "AT+CIPSSLSIZE=4096\r\n",                           "AT+CIPSSLSIZE=4096",                WIFI_AT_TEST_NORMAL_TIMEOUT_MS,  WIFI_AT_TEST_NORMAL },
    { "SSL API",    "AT+CIPSTART=\"SSL\",\"api.open-meteo.com\",443\r\n", "AT+CIPSTART SSL :443",    WIFI_AT_TEST_CONNECT_TIMEOUT_MS, WIFI_AT_TEST_SSL_START }
};

#define WIFI_AT_TEST_COMMAND_COUNT ((uint8_t)(sizeof(wifi_at_test_commands) / sizeof(wifi_at_test_commands[0])))

static uint8_t wifi_at_test_active;                                        /* 当前是否已接管串口。 */
static uint8_t wifi_at_test_busy;                                          /* 当前命令是否尚未完成。 */
static uint8_t wifi_at_test_selected;                                      /* 当前用户选中的步骤。 */
static uint8_t wifi_at_test_payload_sent;                                  /* HTTP 正文是否已在 '>' 后发出。 */
static uint8_t wifi_at_test_http_response_seen;                            /* 是否收到过 +IPD 数据。 */
static uint8_t wifi_at_test_ssl_connect_seen;                              /* SSL 测试是否收到 CONNECT。 */
static uint32_t wifi_at_test_deadline;                                     /* 当前命令的超时刻度。 */
static uint32_t wifi_at_test_revision;                                     /* 页面刷新版本号。 */
static char wifi_at_test_status[WIFI_AT_TEST_LINE_SIZE];                   /* 当前操作状态。 */
static char wifi_at_test_current_line[WIFI_AT_TEST_LINE_SIZE];             /* 正在接收的一行文本。 */
static uint8_t wifi_at_test_current_length;                                /* 当前行长度。 */
static char wifi_at_test_lines[WIFI_AT_TEST_VIEW_LINES][WIFI_AT_TEST_LINE_SIZE]; /* 最近六行返回。 */

/* 写入状态字符串并标记 LCD 需要刷新。 */
static void wifi_at_test_set_status(const char *text)
{
    if (text == NULL)
    {
        wifi_at_test_status[0] = '\0';                                    /* 空指针按空文本处理。 */
    }
    else
    {
        strncpy(wifi_at_test_status, text, sizeof(wifi_at_test_status) - 1U); /* 保留结束符位置。 */
        wifi_at_test_status[sizeof(wifi_at_test_status) - 1U] = '\0';       /* 强制字符串结束。 */
    }

    wifi_at_test_revision++;                                               /* 通知页面更新状态标签。 */
}

/* 清空当前半行和已经显示的六行历史。 */
static void wifi_at_test_clear_lines(void)
{
    memset(wifi_at_test_lines, 0, sizeof(wifi_at_test_lines));             /* 清空历史行。 */
    memset(wifi_at_test_current_line, 0, sizeof(wifi_at_test_current_line)); /* 清空半行。 */
    wifi_at_test_current_length = 0U;                                      /* 半行长度归零。 */
    wifi_at_test_revision++;                                               /* 通知页面清屏。 */
}

/* 把当前完整的一行压入固定的六行显示窗口。 */
static void wifi_at_test_commit_line(void)
{
    uint8_t index;

    if (wifi_at_test_current_length == 0U)
    {
        return;                                                             /* 忽略 CRLF 产生的空行。 */
    }

    wifi_at_test_current_line[wifi_at_test_current_length] = '\0';        /* 转为可供字符串函数使用的文本。 */
    for (index = 0U; index < (WIFI_AT_TEST_VIEW_LINES - 1U); index++)
    {
        memcpy(wifi_at_test_lines[index],
               wifi_at_test_lines[index + 1U],
               WIFI_AT_TEST_LINE_SIZE);                                    /* 旧行逐行向上滚动。 */
    }
    strncpy(wifi_at_test_lines[WIFI_AT_TEST_VIEW_LINES - 1U],
            wifi_at_test_current_line,
            WIFI_AT_TEST_LINE_SIZE - 1U);                                  /* 最新一行放到窗口底部。 */
    wifi_at_test_lines[WIFI_AT_TEST_VIEW_LINES - 1U][WIFI_AT_TEST_LINE_SIZE - 1U] = '\0';
    wifi_at_test_current_length = 0U;                                      /* 为下一行重新计数。 */
    wifi_at_test_current_line[0] = '\0';                                  /* 清除旧内容首字符。 */
    wifi_at_test_revision++;                                               /* 新原始回包到达。 */
}

/* 标记一条测试命令结束，并保留原始回包供用户查看。 */
static void wifi_at_test_finish(uint8_t success, const char *success_text)
{
    wifi_at_test_busy = 0U;                                                 /* 允许用户选择或重复发送下一步。 */
    wifi_at_test_payload_sent = 0U;                                        /* 复位 HTTP 两阶段标志。 */
    wifi_at_test_deadline = 0U;                                            /* 关闭本步骤超时检测。 */
    if (success_text != NULL)
    {
        wifi_at_test_set_status(success_text);                              /* 优先显示 HTTP、SSL 或超时的具体结论。 */
    }
    else
    {
        wifi_at_test_set_status((success != 0U) ? "OK" : "FAILED");      /* 没有专用文字时使用通用结果。 */
    }
}

/* 根据完整 AT 返回行判断当前命令是否成功、失败或仍需等待。 */
static void wifi_at_test_handle_line(const char *line)
{
    const WifiAtTestCommand_t *command;

    if ((line == NULL) || (wifi_at_test_busy == 0U))
    {
        return;                                                             /* 空行和非等待状态不改变测试。 */
    }

    command = &wifi_at_test_commands[wifi_at_test_selected];               /* 读取当前步骤处理规则。 */
    if (command->kind == WIFI_AT_TEST_HTTP_SEND)
    {
        if ((strstr(line, "SEND FAIL") != NULL) || (strstr(line, "ERROR") != NULL))
        {
            wifi_at_test_finish(0U, "HTTP SEND FAILED");                  /* ESP 未能发出 HTTP 数据。 */
        }
        else if (strstr(line, "+IPD") != NULL)
        {
            wifi_at_test_http_response_seen = 1U;                           /* 收到服务器首段数据，说明 TCP/HTTP 已通。 */
            wifi_at_test_set_status("HTTP RESPONSE");                     /* 继续等待 CLOSED 收尾。 */
        }
        else if (strcmp(line, "SEND OK") == 0)
        {
            wifi_at_test_set_status("SENT / WAIT HTTP");                  /* 数据已从 ESP 发出。 */
        }
        else if (strcmp(line, "CLOSED") == 0)
        {
            wifi_at_test_finish(wifi_at_test_http_response_seen,
                                (wifi_at_test_http_response_seen != 0U) ? "HTTP CLOSED" : "HTTP NO RESPONSE");
        }
        return;                                                             /* HTTP 不使用普通 OK 作为结束。 */
    }

    if (strstr(line, "CONNECT") != NULL)
    {
        wifi_at_test_ssl_connect_seen = 1U;                                 /* TCP 或 SSL 成功建连均会出现 CONNECT。 */
    }

    if ((strstr(line, "ERROR") != NULL) || (strstr(line, "FAIL") != NULL))
    {
        wifi_at_test_finish(0U, "AT ERROR");                               /* 模块明确报告失败。 */
    }
    else if (strcmp(line, "OK") == 0)
    {
        if ((command->kind == WIFI_AT_TEST_SSL_START) && (wifi_at_test_ssl_connect_seen == 0U))
        {
            wifi_at_test_finish(0U, "SSL CLOSED");                         /* CLOSED + OK 不等于 SSL 已连接。 */
        }
        else
        {
            wifi_at_test_finish(1U, "OK");                                 /* 普通 AT 命令成功结束。 */
        }
    }
}

/* 处理一个原始 UART 字节，保留可显示文本并识别 CIPSEND 的 '>' 提示。 */
static void wifi_at_test_handle_byte(uint8_t byte)
{
    const WifiAtTestCommand_t *command;
    char display_char;

    command = &wifi_at_test_commands[wifi_at_test_selected];               /* 当前步骤决定是否要等待 '>'。 */
    if ((wifi_at_test_busy != 0U) &&
        (command->kind == WIFI_AT_TEST_HTTP_SEND) &&
        (wifi_at_test_payload_sent == 0U) &&
        (byte == (uint8_t)'>'))
    {
        wifi_at_test_current_line[0] = '>';
        wifi_at_test_current_length = 1U;
        wifi_at_test_commit_line();                                        /* 在 LCD 留下 ESP 的发送提示符。 */
        WifiUart_SendBytes(wifi_at_test_http_request,
                           (uint16_t)(sizeof(wifi_at_test_http_request) - 1U)); /* 严格发送 129 字节请求。 */
        wifi_at_test_payload_sent = 1U;                                    /* 防止重复发送 API 请求正文。 */
        wifi_at_test_deadline = HAL_GetTick() + WIFI_AT_TEST_HTTP_TIMEOUT_MS; /* 正文发出后重新开始 HTTP 计时。 */
        wifi_at_test_set_status("HTTP SENT");                              /* 告知用户已进入等待服务器阶段。 */
        return;
    }

    if (byte == (uint8_t)'\r')
    {
        return;                                                             /* CR 与后续 LF 共同组成换行，不单独显示。 */
    }

    if (byte == (uint8_t)'\n')
    {
        wifi_at_test_current_line[wifi_at_test_current_length] = '\0';    /* 确保当前行可作为字符串分析。 */
        wifi_at_test_handle_line(wifi_at_test_current_line);               /* 先判断是否完成当前步骤。 */
        wifi_at_test_commit_line();                                        /* 再把本行显示到 LCD。 */
        return;
    }

    display_char = ((byte >= 32U) && (byte <= 126U)) ? (char)byte : '.';    /* 控制字符或乱码以点号保留位置。 */
    if (wifi_at_test_current_length < (WIFI_AT_TEST_LINE_SIZE - 1U))
    {
        wifi_at_test_current_line[wifi_at_test_current_length++] = display_char; /* 追加一字节可视文本。 */
        wifi_at_test_current_line[wifi_at_test_current_length] = '\0';    /* 保证下方 strstr() 始终读取到完整字符串结尾。 */
    }

    if ((command->kind == WIFI_AT_TEST_SSL_START) &&
        (strstr(wifi_at_test_current_line, "CONNECT") != NULL))
    {
        wifi_at_test_ssl_connect_seen = 1U;                                 /* CONNECT 可能在行结束前已经到达。 */
    }
}

/* 初始化模块静态变量；该函数本身不发送任何 AT 命令。 */
void WifiAtTest_Init(void)
{
    wifi_at_test_active = 0U;                                               /* 默认由普通 WiFi 状态机管理串口。 */
    wifi_at_test_busy = 0U;                                                 /* 启动时没有待完成命令。 */
    wifi_at_test_selected = 0U;                                             /* 默认选中最基础的 AT 应答。 */
    wifi_at_test_payload_sent = 0U;                                        /* HTTP 正文未发送。 */
    wifi_at_test_http_response_seen = 0U;                                  /* 尚未收到 HTTP 数据。 */
    wifi_at_test_ssl_connect_seen = 0U;                                    /* 尚未收到 SSL CONNECT。 */
    wifi_at_test_deadline = 0U;                                            /* 未启用超时。 */
    wifi_at_test_revision = 0U;                                            /* 页面首次进入时强制刷新。 */
    wifi_at_test_clear_lines();                                            /* 清除旧回包。 */
    wifi_at_test_set_status("READY");                                     /* 给页面提供初始状态。 */
}

/* 进入测试页时暂停普通 WiFi 自动状态机。 */
void WifiAtTest_Enter(void)
{
    WifiManager_SetAtTestMode(1U);                                         /* 避免普通状态机消耗原始 AT 回包。 */
    WifiUart_ClearRx();                                                     /* 丢弃页面切换前遗留的字节。 */
    wifi_at_test_active = 1U;                                              /* 允许 Process() 开始读取串口。 */
    wifi_at_test_busy = 0U;                                                /* 不自动发送，等待用户按确认。 */
    wifi_at_test_selected = 0U;                                            /* 每次从 AT 基础测试开始。 */
    wifi_at_test_payload_sent = 0U;
    wifi_at_test_http_response_seen = 0U;
    wifi_at_test_ssl_connect_seen = 0U;
    wifi_at_test_deadline = 0U;
    wifi_at_test_clear_lines();                                            /* 清空上一次进入的返回。 */
    wifi_at_test_set_status("READY");
}

/* 离开测试页时恢复 WiFi 页面自己的 AT 状态机。 */
void WifiAtTest_Leave(void)
{
    if (wifi_at_test_active == 0U)
    {
        return;                                                             /* 防止导航重复调用导致状态机重复恢复。 */
    }

    wifi_at_test_active = 0U;                                              /* 先停止诊断层抢占接收缓冲。 */
    wifi_at_test_busy = 0U;                                                /* 放弃未完成命令。 */
    WifiUart_ClearRx();                                                     /* 残余 HTTP 数据不带回普通页面。 */
    WifiManager_SetAtTestMode(0U);                                         /* 恢复普通联网检测。 */
}

/* 周期消费 ESP8266 的原始回包，并处理超时。 */
void WifiAtTest_Process(void)
{
    uint8_t byte;
    uint16_t byte_count = 0U;

    if (wifi_at_test_active == 0U)
    {
        return;                                                             /* 普通模式下不从 WiFi 管理器抢串口数据。 */
    }

    while ((byte_count < WIFI_AT_TEST_RX_PER_CYCLE) && (WifiUart_ReadByte(&byte) != 0U))
    {
        wifi_at_test_handle_byte(byte);                                    /* 保存并分析一个 ESP 原始字节。 */
        byte_count++;                                                      /* 限制单轮开销，保持 LVGL 流畅。 */
    }

    if ((wifi_at_test_busy != 0U) && ((int32_t)(HAL_GetTick() - wifi_at_test_deadline) >= 0))
    {
        wifi_at_test_finish(0U, "TIMEOUT");                               /* 指定时间内没有完整响应。 */
    }
}

/* 切换上一条或下一条测试步骤；执行过程中不允许切换。 */
void WifiAtTest_Select(int8_t step)
{
    if ((wifi_at_test_active == 0U) || (wifi_at_test_busy != 0U) || (step == 0))
    {
        return;
    }

    if (step < 0)
    {
        wifi_at_test_selected = (wifi_at_test_selected == 0U) ?
                                (uint8_t)(WIFI_AT_TEST_COMMAND_COUNT - 1U) :
                                (uint8_t)(wifi_at_test_selected - 1U);    /* 向前循环。 */
    }
    else
    {
        wifi_at_test_selected = (uint8_t)(wifi_at_test_selected + 1U);     /* 先递增，再处理末尾回绕。 */
        if (wifi_at_test_selected >= WIFI_AT_TEST_COMMAND_COUNT)
        {
            wifi_at_test_selected = 0U;
        }
    }

    wifi_at_test_clear_lines();                                            /* 不混淆不同命令的原始回包。 */
    wifi_at_test_set_status("READY");
}

/* 发送当前选中的 AT 步骤。 */
void WifiAtTest_SendSelected(void)
{
    const WifiAtTestCommand_t *command;

    if ((wifi_at_test_active == 0U) || (wifi_at_test_busy != 0U))
    {
        return;                                                             /* 不在测试页或仍等待时拒绝重复发送。 */
    }

    command = &wifi_at_test_commands[wifi_at_test_selected];               /* 获取当前命令内容和超时。 */
    wifi_at_test_clear_lines();                                            /* 当前步骤从空响应窗口开始。 */
    WifiUart_ClearRx();                                                     /* 清除切换步骤前残余字节。 */
    wifi_at_test_payload_sent = 0U;
    wifi_at_test_http_response_seen = 0U;
    wifi_at_test_ssl_connect_seen = 0U;
    WifiUart_SendText(command->command);                                  /* STM32 通过 USART1 将 AT 发给 ESP8266。 */
    wifi_at_test_busy = 1U;                                                /* 开始等待原始返回。 */
    wifi_at_test_deadline = HAL_GetTick() + command->timeout_ms;           /* 设定本步骤超时。 */
    wifi_at_test_set_status("WAIT RESPONSE");
}

/* 手动清除显示，不改变当前选择。 */
void WifiAtTest_ClearResponse(void)
{
    if (wifi_at_test_busy != 0U)
    {
        return;                                                             /* 执行中清屏会丢失诊断依据。 */
    }

    wifi_at_test_clear_lines();
    wifi_at_test_set_status("READY");
}

uint8_t WifiAtTest_IsBusy(void) { return wifi_at_test_busy; }
uint8_t WifiAtTest_GetSelectedIndex(void) { return wifi_at_test_selected; }
uint8_t WifiAtTest_GetCommandCount(void) { return WIFI_AT_TEST_COMMAND_COUNT; }
const char *WifiAtTest_GetStepName(void) { return wifi_at_test_commands[wifi_at_test_selected].name; }
const char *WifiAtTest_GetCommandText(void) { return wifi_at_test_commands[wifi_at_test_selected].display_command; }
const char *WifiAtTest_GetStatusText(void) { return wifi_at_test_status; }

const char *WifiAtTest_GetResponseLine(uint8_t line_index)
{
    return (line_index < WIFI_AT_TEST_VIEW_LINES) ? wifi_at_test_lines[line_index] : "";
}

uint32_t WifiAtTest_GetRevision(void) { return wifi_at_test_revision; }
