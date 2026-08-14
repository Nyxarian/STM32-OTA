#include "esp8266_at.h"
#include "esp8266_config.h"
#include "iap_uart.h"
#include "main.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define ESP8266_AT_RESPONSE_LINE_MAX  128U /* 单行 AT 响应的本地缓存上限。 */
#define ESP8266_AT_JOIN_TIMEOUT_MS  20000U /* WPA 认证和 DHCP 获取地址需要较长等待时间。 */

/*
 * 从 USART1 读取一条以 LF 结束的 AT 响应行。
 *
 * ESP8266 的 AT 响应通常为 CRLF；此函数丢弃 CR，并跳过空行。
 * 函数只在 Bootloader 的 AT 初始化阶段使用，初始化完成后由 +IPD
 * 解析器接管所有串口字节。
 */
static bool Esp8266At_ReadResponseLine(char *line,
                                       uint16_t line_size,
                                       uint32_t timeout_ms)
{
    uint32_t start_tick; /* 保存本行开始等待的系统节拍。 */
    uint16_t length = 0U; /* 保存当前已经写入缓存的字符数。 */
    uint8_t byte; /* 保存从 USART1 接收到的单个字节。 */

    if ((line == NULL) || (line_size < 2U)) /* 没有足够空间保存至少一个字符和结束符时拒绝读取。 */
    {
        return false; /* 参数无效时直接返回失败。 */
    }

    line[0] = '\0'; /* 先保证调用方始终得到一个合法字符串。 */
    start_tick = HAL_GetTick(); /* 记录本行超时判断的起始时刻。 */

    while ((HAL_GetTick() - start_tick) < timeout_ms) /* 在本行超时前持续读取串口。 */
    {
        if (!IapUart_ReadByte(&byte, 10U)) /* 每次最多等待 10 ms，便于及时检查总超时。 */
        {
            continue; /* 本轮没有数据时继续等待。 */
        }

        if (byte == (uint8_t)'\r') /* CR 只是行结束的一部分，不写入缓存。 */
        {
            continue; /* 继续等待 LF 或下一行内容。 */
        }

        if (byte == (uint8_t)'\n') /* LF 表示一行响应结束。 */
        {
            if (length == 0U) /* 连续 CRLF 会形成空行。 */
            {
                continue; /* 空行没有 AT 语义，继续读取下一行。 */
            }

            line[length] = '\0'; /* 为当前响应行补上 C 字符串结束符。 */
            return true; /* 成功得到一条非空响应行。 */
        }

        if (length < (uint16_t)(line_size - 1U)) /* 仅在缓存未满时保存可见响应字符。 */
        {
            line[length] = (char)byte; /* 保存当前响应字节。 */
            length++; /* 推进下一字符写入位置。 */
        }
        else /* 响应行异常过长时不允许越界写入。 */
        {
            length = 0U; /* 丢弃本行已缓存内容。 */
            line[0] = '\0'; /* 恢复为空字符串，等待下一行重新同步。 */
        }
    }

    return false; /* 在规定时间内没有得到完整非空行时报告超时。 */
}

/*
 * 等待 AT 命令的最终成功或失败行。
 *
 * "OK" 是成功结束；"ERROR"、"FAIL" 和 "+CWJAP:" 是失败结束。
 * 连接 WiFi 时 ESP8266 还会先输出 WIFI CONNECTED、WIFI GOT IP，
 * 它们都是中间状态，因此继续等待最终 OK。
 */
static bool Esp8266At_WaitForCommandResult(uint32_t timeout_ms)
{
    uint32_t start_tick; /* 保存整个命令的开始时刻。 */
    char line[ESP8266_AT_RESPONSE_LINE_MAX]; /* 保存每次读取到的一条 AT 响应。 */

    start_tick = HAL_GetTick(); /* 记录总超时起点。 */

    while ((HAL_GetTick() - start_tick) < timeout_ms) /* 在命令超时前逐行处理响应。 */
    {
        if (!Esp8266At_ReadResponseLine(line,
                                         (uint16_t)sizeof(line),
                                         100U)) /* 每次读取一行最多等待 100 ms。 */
        {
            continue; /* 当前 100 ms 未成行时继续等待总超时。 */
        }

        if (strcmp(line, "OK") == 0) /* AT 命令的标准成功结束符。 */
        {
            return true; /* 确认整个命令执行成功。 */
        }

        if ((strcmp(line, "ERROR") == 0) || /* 通用 AT 错误。 */
            (strcmp(line, "FAIL") == 0) || /* WiFi 加入失败。 */
            (strncmp(line, "+CWJAP:", 7U) == 0)) /* 旧 AT 固件用 +CWJAP:n 报告加入失败原因。 */
        {
            return false; /* 命令已明确失败，不再等待。 */
        }
    }

    return false; /* 超时没有得到 OK 时按失败处理。 */
}

/*
 * 发送 AT+CWJAP? 并读取模块当前关联的 SSID。
 *
 * 返回 true 仅表示拿到了完整查询响应；SSID 为空表示当前没有连接热点。
 */
static bool Esp8266At_QueryCurrentSsid(char *ssid,
                                       uint16_t ssid_size,
                                       uint32_t timeout_ms)
{
    uint32_t start_tick; /* 保存查询命令总超时的起点。 */
    char line[ESP8266_AT_RESPONSE_LINE_MAX]; /* 保存当前 AT 响应行。 */
    const char *first_quote; /* 保存 SSID 开始引号位置。 */
    const char *second_quote; /* 保存 SSID 结束引号位置。 */
    uint16_t length; /* 保存 SSID 的实际字节长度。 */
    bool no_access_point = false; /* 记录模块是否明确报告当前没有关联热点。 */

    if ((ssid == NULL) || (ssid_size < 2U)) /* 调用方必须提供 SSID 输出缓存。 */
    {
        return false; /* 参数无效时不能执行查询。 */
    }

    ssid[0] = '\0'; /* 默认将结果设为空，表示尚未检测到关联热点。 */
    IapUart_SendText("AT+CWJAP?\r\n"); /* 请求 ESP8266 返回当前接入点信息。 */
    start_tick = HAL_GetTick(); /* 开始统计查询总超时。 */

    while ((HAL_GetTick() - start_tick) < timeout_ms) /* 在超时前逐行解析查询结果。 */
    {
        if (!Esp8266At_ReadResponseLine(line,
                                         (uint16_t)sizeof(line),
                                         100U)) /* 单次读行最多等待 100 ms。 */
        {
            continue; /* 当前未形成完整行时继续等待。 */
        }

        if (strncmp(line, "+CWJAP:", 7U) == 0) /* 找到当前接入点信息行。 */
        {
            first_quote = strchr(line, '"'); /* 查找 SSID 起始引号。 */
            if (first_quote == NULL) /* 缺少起始引号说明响应格式异常。 */
            {
                continue; /* 保留空结果，继续等待最终命令状态。 */
            }

            second_quote = strchr(first_quote + 1, '"'); /* 查找 SSID 结束引号。 */
            if (second_quote == NULL) /* 缺少结束引号说明本行不完整。 */
            {
                continue; /* 等待后续响应。 */
            }

            length = (uint16_t)(second_quote - first_quote - 1); /* 计算 SSID 正文长度。 */
            if (length >= ssid_size) /* 为字符串结束符保留一个字节。 */
            {
                length = (uint16_t)(ssid_size - 1U); /* 过长 SSID 截断保存，避免越界。 */
            }

            memcpy(ssid, first_quote + 1, length); /* 复制 SSID 正文。 */
            ssid[length] = '\0'; /* 形成安全的 C 字符串。 */
            continue; /* 继续等待最终 OK，确认查询命令正常结束。 */
        }

        if (strcmp(line, "OK") == 0) /* 查询命令已正常结束。 */
        {
            return (no_access_point == false); /* 只有未报告 No AP 时才表示当前查询到了有效关联状态。 */
        }

        if (strcmp(line, "No AP") == 0) /* 模块明确没有关联热点。 */
        {
            no_access_point = true; /* 保留未关联结果，但继续读取可能紧随其后的 OK，避免残留响应污染下一条命令。 */
            continue; /* 等待这条查询命令的完整结束。 */
        }

        if (strcmp(line, "ERROR") == 0) /* 模块不接受该查询命令。 */
        {
            return false; /* 查询失败或未关联时交给调用方重新连接。 */
        }
    }

    return false; /* 查询超时按失败处理。 */
}

/*
 * 发送带 SSID 和密码的 AT+CWJAP 命令，等待 WPA 认证和 DHCP 完成。
 */
static bool Esp8266At_JoinAccessPoint(const char *ssid,
                                      const char *password,
                                      uint32_t timeout_ms)
{
    char command[112]; /* 保存格式化后的 AT+CWJAP 命令。 */
    int command_length; /* 保存 snprintf 的返回长度。 */

    if ((ssid == NULL) || (password == NULL) || (ssid[0] == '\0')) /* 空热点名称无法建立连接。 */
    {
        return false; /* 参数无效时禁止发送命令。 */
    }

    command_length = snprintf(command,
                              sizeof(command),
                              "AT+CWJAP=\"%s\",\"%s\"\r\n",
                              ssid,
                              password); /* 生成 ESP8266 AT 固件要求的连接命令。 */
    if ((command_length < 0) || ((size_t)command_length >= sizeof(command))) /* 检查命令生成失败或被截断。 */
    {
        return false; /* 不发送不完整的账号密码文本。 */
    }

    IapUart_SendText(command); /* 将完整连接命令发送给 ESP8266。 */
    return Esp8266At_WaitForCommandResult(timeout_ms); /* 等待认证、DHCP 与最终 OK。 */
}

/*
 * 查询 ESP8266 当前 STA IPv4 地址。
 *
 * 只有收到 +CIFSR:STAIP 行且最终收到 OK 才返回 true，避免仅凭 WiFi
 * 已关联就误认为 TCP Server 已经可以被局域网电脑访问。
 */
static bool Esp8266At_ReadStationIp(char *station_ip,
                                    uint16_t station_ip_size,
                                    uint32_t timeout_ms)
{
    uint32_t start_tick; /* 保存地址查询的总超时起点。 */
    char line[ESP8266_AT_RESPONSE_LINE_MAX]; /* 保存当前 AT 响应行。 */
    const char *first_quote; /* 保存 IP 地址起始引号位置。 */
    const char *second_quote; /* 保存 IP 地址结束引号位置。 */
    uint16_t length; /* 保存 IP 文本长度。 */
    bool ip_received = false; /* 标记是否已解析到合法 STAIP 行。 */

    if ((station_ip == NULL) || (station_ip_size < 2U)) /* 调用方必须提供 IP 输出缓存。 */
    {
        return false; /* 参数无效时不能查询地址。 */
    }

    station_ip[0] = '\0'; /* 先清空旧 IP，避免失败时误用上次数据。 */
    IapUart_SendText("AT+CIFSR\r\n"); /* 请求 ESP8266 返回站点模式 IP 地址。 */
    start_tick = HAL_GetTick(); /* 开始统计查询超时。 */

    while ((HAL_GetTick() - start_tick) < timeout_ms) /* 在超时前逐行读取查询结果。 */
    {
        if (!Esp8266At_ReadResponseLine(line,
                                         (uint16_t)sizeof(line),
                                         100U)) /* 每次等待一行最长 100 ms。 */
        {
            continue; /* 当前未成行时继续等待。 */
        }

        if (strncmp(line, "+CIFSR:STAIP,\"", 14U) == 0) /* 找到站点模式 IPv4 地址行。 */
        {
            first_quote = strchr(line, '"'); /* 查找 IP 地址起始引号。 */
            if (first_quote == NULL) /* 响应格式不完整时不复制数据。 */
            {
                continue; /* 继续等待最终响应。 */
            }

            second_quote = strchr(first_quote + 1, '"'); /* 查找 IP 地址结束引号。 */
            if (second_quote == NULL) /* 缺少结束引号时忽略本行。 */
            {
                continue; /* 继续解析后续响应。 */
            }

            length = (uint16_t)(second_quote - first_quote - 1); /* 计算 IPv4 正文长度。 */
            if (length >= station_ip_size) /* 为字符串结束符预留空间。 */
            {
                length = (uint16_t)(station_ip_size - 1U); /* 异常长文本安全截断。 */
            }

            memcpy(station_ip, first_quote + 1, length); /* 复制 IPv4 文本。 */
            station_ip[length] = '\0'; /* 补字符串结束符。 */
            ip_received = (station_ip[0] != '\0'); /* 标记已经得到非空 IP。 */
            continue; /* 继续等待最终 OK。 */
        }

        if (strcmp(line, "OK") == 0) /* 查询命令正常结束。 */
        {
            return ip_received; /* 只有实际收到 STAIP 才报告联网成功。 */
        }

        if (strcmp(line, "ERROR") == 0) /* 地址查询执行失败。 */
        {
            return false; /* 直接报告失败。 */
        }
    }

    return false; /* 超时未得到完整 IP 信息时报告失败。 */
}

/* 设置 ESP8266 为 Station 模式，使其可以加入路由器 WiFi。 */
static bool Esp8266At_SetStationMode(uint32_t timeout_ms)
{
    return Esp8266At_SendCommand("AT+CWMODE=1", "OK\r\n", timeout_ms); /* 1 表示 Station 模式。 */
}

/*
 * 确保 ESP8266 已关联到预存热点并获得 IPv4 地址。
 *
 * 如果模块已经连接到目标 SSID，则仅查询 IP，不重复认证；连接到了
 * 其他热点、没有热点或查询失败时，才使用预存账号密码重新加入。
 */
static bool Esp8266At_EnsureDefaultWifi(char *station_ip,
                                        uint16_t station_ip_size,
                                        uint32_t timeout_ms)
{
    char current_ssid[33]; /* 保存 AT+CWJAP? 返回的当前热点名称。 */

    if (!Esp8266At_SetStationMode(timeout_ms)) /* 先确保模块处于可入网的 Station 模式。 */
    {
        return false; /* 模式设置失败时不能继续联网。 */
    }

    if (Esp8266At_QueryCurrentSsid(current_ssid,
                                    (uint16_t)sizeof(current_ssid),
                                    timeout_ms) && /* 查询命令成功返回。 */
        (strcmp(current_ssid, ESP8266_CONFIG_WIFI_SSID) == 0) && /* 当前正是目标 WiFi。 */
        Esp8266At_ReadStationIp(station_ip, station_ip_size, timeout_ms)) /* 并且已获得有效 IPv4。 */
    {
        return true; /* 已经联网，无需再次执行 AT+CWJAP。 */
    }

    if (!Esp8266At_JoinAccessPoint(ESP8266_CONFIG_WIFI_SSID,
                                   ESP8266_CONFIG_WIFI_PASSWORD,
                                   ESP8266_AT_JOIN_TIMEOUT_MS)) /* 未联网或热点不符时使用预存凭据加入。 */
    {
        return false; /* WPA 认证、密码或 DHCP 失败时停止初始化。 */
    }

    return Esp8266At_ReadStationIp(station_ip, station_ip_size, timeout_ms); /* 加入完成后必须再次确认获得了 IP。 */
}

/* 在限定时间内从 IAP 串口接收数据，直到匹配完整的期望响应字符串。 */
static bool Esp8266At_WaitFor(const char *expected,
                              uint32_t timeout_ms)
{
    uint32_t start_tick; // 保存等待开始时的系统节拍。
    size_t matched = 0U; // 保存当前已经匹配的期望字符串长度。
    uint8_t byte; // 保存从串口读取的单个字节。

    if ((expected == NULL) || (expected[0] == '\0')) // 期望字符串为空时无法进行匹配。
    {
        return false; // 参数无效，直接报告失败。
    }

    start_tick = HAL_GetTick(); // 记录起始时间，用于后续超时判断。

    while ((HAL_GetTick() - start_tick) < timeout_ms) // 在超时之前持续读取响应。
    {
        if (IapUart_ReadByte(&byte, 10U)) // 非阻塞读取一个 IAP 串口字节。
        {
            if (byte == (uint8_t)expected[matched]) // 当前字节与期望字符串当前位置一致。
            {
                matched++; // 匹配长度向后推进一位。

                if (expected[matched] == '\0') // 已经匹配完整的期望字符串。
                {
                    return true; // 告知上层命令响应成功。
                }
            }
            else
            {
                matched = (byte == (uint8_t)expected[0]) ? 1U : 0U; // 失配后从当前字节重新尝试匹配开头。
            }
        }
    }

    return false; // 超时仍未匹配完整响应，报告失败。
}

/* 发送一条 AT 命令并等待指定响应。 */
bool Esp8266At_SendCommand(const char *command,
                           const char *expected,
                           uint32_t timeout_ms)
{
    if ((command == NULL) || (expected == NULL)) // 检查命令和期望文本指针。
    {
        return false; // 参数无效时不操作串口。
    }

    IapUart_SendText(command); // 发送 AT 命令主体。
    IapUart_SendText("\r\n"); // 补充 ESP8266 要求的回车换行结束符。

    return Esp8266At_WaitFor(expected, timeout_ms); // 等待并返回响应匹配结果。
}

/* 检查 ESP8266 是否能够响应基础 AT 命令。 */
bool Esp8266At_Test(uint32_t timeout_ms)
{
    return Esp8266At_SendCommand("AT", "OK\r\n", timeout_ms); // 模块返回 OK 即表示通信正常。
}

/* 关闭 ESP8266 命令回显。 */
bool Esp8266At_DisableEcho(uint32_t timeout_ms)
{
    return Esp8266At_SendCommand("ATE0", "OK\r\n", timeout_ms); // 关闭回显后便于解析响应内容。
}

/* 设置 ESP8266 为普通传输模式。 */
bool Esp8266At_SetNormalMode(uint32_t timeout_ms)
{
    return Esp8266At_SendCommand("AT+CIPMODE=0", "OK\r\n", timeout_ms); // 0 表示普通模式而不是透传模式。
}

/* 强制恢复 ESP8266 的主动接收模式，避免只收到 +IPD 长度而没有真实数据。 */
bool Esp8266At_SetActiveReceiveMode(uint32_t timeout_ms)
{
    return Esp8266At_SendCommand("AT+CIPRECVMODE=0", "OK\r\n", timeout_ms); // 0 表示收到 TCP 数据后主动通过串口输出 +IPD 和负载。
}

/* 开启 ESP8266 多连接模式。 */
bool Esp8266At_EnableMultipleConnections(uint32_t timeout_ms)
{
    return Esp8266At_SendCommand("AT+CIPMUX=1", "OK\r\n", timeout_ms); // 允许服务器同时维护多个连接编号。
}

/* 设置 +IPD 数据通知中是否包含远端 IP 和端口信息。 */
bool Esp8266At_SetIpdInfo(uint8_t enable, uint32_t timeout_ms)
{
    if (enable > 1U) // 该参数只接受 0 或 1。
    {
        return false; // 参数超出范围时拒绝发送命令。
    }

    return Esp8266At_SendCommand(enable ? "AT+CIPDINFO=1" // 根据开关选择对应 AT 命令。
                                        : "AT+CIPDINFO=0",
                                 "OK\r\n", // 期望模块返回 OK。
                                 timeout_ms); // 使用调用者提供的超时时间。
}

/* 设置 TCP 服务器允许同时存在的最大连接数量。 */
bool Esp8266At_SetServerMaxConnections(uint8_t max_connections,
                                       uint32_t timeout_ms)
{
    char command[32]; // 保存拼接后的最大连接数命令。
    int length; // 保存格式化输出长度。

    if ((max_connections == 0U) || (max_connections > 5U)) // ESP8266 AT 固件允许 1 到 5 个连接。
    {
        return false; // 参数越界时不发送命令。
    }

    length = snprintf(command, // 生成设置最大连接数的命令字符串。
                      sizeof(command), // 限制写入长度，防止缓冲区溢出。
                      "AT+CIPSERVERMAXCONN=%u", // 使用 ESP8266 的标准 AT 命令格式。
                      (unsigned int)max_connections); // 写入调用者指定的连接数量。

    if ((length < 0) || ((size_t)length >= sizeof(command))) // 检查格式化失败或文本被截断。
    {
        return false; // 命令不完整时拒绝发送。
    }

    return Esp8266At_SendCommand(command, "OK\r\n", timeout_ms); // 发送设置命令并等待确认。
}

/* 在指定端口启动 ESP8266 TCP 服务器。 */
bool Esp8266At_StartTcpServer(uint16_t port, uint32_t timeout_ms)
{
    char command[32]; // 保存拼接后的服务器启动命令。
    int length; // 保存格式化输出长度。

    if (port == 0U) // 端口号不能为零。
    {
        return false; // 参数无效时不启动服务器。
    }

    length = snprintf(command, // 生成启动 TCP 服务器的命令。
                      sizeof(command), // 限制命令字符串长度。
                      "AT+CIPSERVER=1,%u", // 1 表示启动服务器。
                      (unsigned int)port); // 写入服务器监听端口。

    if ((length < 0) || ((size_t)length >= sizeof(command))) // 检查字符串是否生成成功。
    {
        return false; // 命令不完整时报告失败。
    }

    return Esp8266At_SendCommand(command, "OK\r\n", timeout_ms); // 发送启动命令并等待 OK。
}

/* 停止 ESP8266 当前运行的 TCP 服务器。 */
bool Esp8266At_StopTcpServer(uint32_t timeout_ms)
{
    return Esp8266At_SendCommand("AT+CIPSERVER=0", // 0 表示停止服务器。
                                 "OK\r\n", // 期望模块返回 OK。
                                 timeout_ms); // 使用调用者提供的等待时长。
}

/**
 * @brief 使用预存 WiFi 凭据联网并启动 Bootloader TCP Server。
 *
 * @param port TCP Server 监听端口。
 * @param station_ip 用于接收 ESP8266 当前 IPv4 地址的缓存，可为 NULL。
 * @param station_ip_size station_ip 缓存大小；station_ip 为 NULL 时传 0。
 * @param timeout_ms 普通 AT 命令的超时时间，单位为毫秒。
 * @return true 模块已联网且 TCP Server 已在指定端口监听；false 初始化失败。
 */
bool Esp8266At_PrepareWifiTcpServer(uint16_t port,
                                    char *station_ip,
                                    uint16_t station_ip_size,
                                    uint32_t timeout_ms)
{
    uint8_t attempt; // 保存当前第几次基础通信尝试。
    bool module_ready = false; // 记录模块是否已经成功响应 AT。
    char ignored_ip[16]; // 当调用方不关心 IP 时使用本地缓存完成联网校验。
    char *ip_output; // 保存最终用于接收 IP 的有效缓存地址。
    uint16_t ip_output_size; // 保存最终 IP 缓存大小。

    if ((port == 0U) || ((station_ip == NULL) && (station_ip_size != 0U))) // 检查端口和可选输出缓存的参数组合。
    {
        return false; // 参数无效时不操作 ESP8266。
    }

    ip_output = (station_ip != NULL) ? station_ip : ignored_ip; // 调用方未提供缓存时使用本地缓存。
    ip_output_size = (station_ip != NULL) ? station_ip_size : (uint16_t)sizeof(ignored_ip); // 同步有效缓存容量。
    if (ip_output_size < 2U) // IPv4 文本至少需要一个字符和字符串结束符。
    {
        return false; // 缓存太小时不能安全返回 IP。
    }

    // ESP8266 和 STM32 同时上电时，ESP8266 启动通常更慢，这里最多尝试 5 次 AT 通信。
    for (attempt = 0U; attempt < 5U; attempt++) // 循环等待模块完成启动。
    {
        if (Esp8266At_Test(timeout_ms)) // 尝试发送 AT 并等待 OK。
        {
            module_ready = true; // 标记模块已经可以正常通信。
            break; // 不需要继续重复测试。
        }

        HAL_Delay(500U); // 本次失败后等待半秒再重试。
    }

    if (!module_ready) // 五次尝试仍然失败时终止初始化。
    {
        return false; // 上层可以据此显示网络不可用。
    }

    // 关闭命令回显，防止返回内容中混入刚发送的 AT 指令。
    if (!Esp8266At_DisableEcho(timeout_ms)) // 关闭回显失败则不能继续可靠解析。
    {
        return false; // 返回初始化失败。
    }

    if (!Esp8266At_EnsureDefaultWifi(ip_output, ip_output_size, timeout_ms)) // 确认 ESP8266 已连接预存 WiFi 并获得 IPv4。
    {
        return false; // 未能联网时启动 Server 没有局域网意义，直接报告失败。
    }

    // 如果之前已经启动过服务器，必须先关闭；否则 ESP8266 会拒绝下面的 AT+CIPMODE 切换。
    // 第一次运行时没有残留服务器，可能返回 ERROR，因此不检查返回值。
    (void)Esp8266At_StopTcpServer(timeout_ms); // 清理上一次残留的服务器状态。

    // TCP Server 只能工作在普通传输模式和多连接模式。
    if (!Esp8266At_SetNormalMode(timeout_ms)) // 设置普通传输模式。
    {
        return false; // 模式设置失败时终止初始化。
    }

    /* ESP8266 可能在上一次运行中保留被动接收模式，重新启动 Server 前尽力恢复主动接收。 */
    (void)Esp8266At_SetActiveReceiveMode(timeout_ms); // 0 表示由 ESP8266 通过 +IPD 主动输出 TCP 负载；旧固件不支持时保留默认主动模式。

    if (!Esp8266At_EnableMultipleConnections(timeout_ms)) // 开启多连接模式。
    {
        return false; // 多连接设置失败时终止初始化。
    }

    // 下面两条是辅助配置，即使固件不支持也不阻止服务器继续启动。
    (void)Esp8266At_SetIpdInfo(0U, timeout_ms); // 关闭附加远端地址信息，简化 +IPD 头解析。
    (void)Esp8266At_SetServerMaxConnections(1U, timeout_ms); // 当前只保留一个 TCP 客户端连接。

    return Esp8266At_StartTcpServer(port, timeout_ms); // 启动最终的 TCP 服务器并返回结果。
}

/**
 * @brief 兼容旧接口：使用预存 WiFi 启动 TCP Server，但忽略返回的 IP。
 *
 * @param port TCP Server 监听端口。
 * @param timeout_ms 普通 AT 命令超时时间，单位为毫秒。
 * @return true 初始化成功；false 初始化失败。
 */
bool Esp8266At_PrepareTcpServer(uint16_t port,
                                uint32_t timeout_ms)
{
    return Esp8266At_PrepareWifiTcpServer(port, NULL, 0U, timeout_ms); // 将旧调用统一转到新的完整联网流程。
}
