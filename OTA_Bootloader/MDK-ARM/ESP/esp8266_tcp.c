#include "esp8266_tcp.h"
#include "iap_uart.h"
#include "main.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

typedef enum
{
    ESP8266_TCP_WAIT_PLUS = 0, // 等待 +IPD 头部的加号。
    ESP8266_TCP_WAIT_I, // 已收到加号，等待字符 I。
    ESP8266_TCP_WAIT_P, // 已收到 I，等待字符 P。
    ESP8266_TCP_WAIT_D, // 已收到 IP，等待字符 D。
    ESP8266_TCP_WAIT_HEADER_COMMA, // 等待 +IPD 与连接编号之间的逗号。
    ESP8266_TCP_READ_LINK_ID, // 读取 TCP 连接编号。
    ESP8266_TCP_READ_LENGTH, // 读取后续 TCP 数据长度。
    ESP8266_TCP_SKIP_REMOTE_INFO, // 跳过可选的远端 IP 和端口字段。
    ESP8266_TCP_READ_PAYLOAD // 按长度消费真实 TCP 负载。
} Esp8266Tcp_ParseState_t;

typedef struct
{
    Esp8266Tcp_ParseState_t state; // 当前 +IPD 解析状态。

    uint32_t link_id; // 当前 TCP 连接编号。
    uint32_t payload_length; // 当前帧声明的负载总长度。
    uint32_t payload_remaining; // 当前帧还没有交给上层的字节数。

    bool link_id_started; // 是否已经读取到至少一个连接编号数字。
    bool length_started; // 是否已经读取到至少一个负载长度数字。
} Esp8266Tcp_Parser_t;

static Esp8266Tcp_Parser_t parser; // 保存整个 +IPD 解析状态机的运行数据。
static Esp8266Tcp_DataHandler_t tcp_data_handler; // 保存收到 TCP 数据后的上层回调。
static Esp8266Tcp_ConnectHandler_t tcp_connect_handler; // 保存 TCP 客户端建立连接后的上层回调。
static bool tcp_raw_echo_enabled; // 标记是否启用不经过 IAP 协议的 TCP 原样回显测试。
static char tcp_status_line[24]; // 缓存 ESP8266 输出的连接状态行，例如 0,CONNECT。
static uint8_t tcp_status_line_length; // 记录连接状态行缓存中已经保存的字符数。
static bool tcp_connect_event_pending; // 记录是否收到一条尚未在主循环中处理的 CONNECT 事件。
static uint8_t tcp_connect_event_link_id; // 保存待处理 CONNECT 事件对应的连接编号。

/*
 * ESP8266 的 +IPD 负载先暂存在这里，收到完整数据包后再交给上层。
 *
 * 不能在逐字节解析 +IPD 的过程中立即调用上层：例如 PING 的最后一个
 * 换行会触发 PONG，而 PONG 需要马上向 ESP8266 发送 AT+CIPSEND 并读取
 * 模块响应。此时若 +IPD 解析器仍处在“正在收包”状态，AT 响应和剩余
 * 串口数据可能会混在一起，导致 TCP 回复丢失。完整缓存后统一回调可
 * 保证解析器先回到空闲状态，再执行可能阻塞的协议回复。
 */
static uint8_t tcp_payload[ESP8266_TCP_MAX_IPD_LENGTH]; // 保存当前 +IPD 帧的完整 TCP 负载。
static uint16_t tcp_payload_length; // 保存已经写入 tcp_payload 的有效字节数。
static bool tcp_packet_pending; // 记录是否有一包完整 TCP 数据尚未在主循环中分发。
static uint8_t tcp_packet_link_id; // 保存待分发 TCP 数据所属的连接编号。
static uint16_t tcp_packet_length; // 保存待分发 TCP 数据的实际长度。

/* 将 +IPD 解析器恢复到等待下一个帧头的初始状态。 */
static void Esp8266Tcp_ResetParser(void)
{
    parser.state = ESP8266_TCP_WAIT_PLUS; // 从下一个加号开始重新寻找帧头。

    parser.link_id = 0U; // 清除旧连接编号。
    parser.payload_length = 0U; // 清除旧负载长度。
    parser.payload_remaining = 0U; // 清除旧剩余字节数。

    parser.link_id_started = false; // 清除连接编号读取标记。
    parser.length_started = false; // 清除长度读取标记。
    tcp_payload_length = 0U; // 丢弃旧数据包的缓存内容，准备接收下一帧。
}

/* 当前字节解析失败时重新寻找下一个加号；若当前字节就是加号则直接复用它。 */
static void Esp8266Tcp_RestartFromByte(uint8_t byte)
{
    Esp8266Tcp_ResetParser(); // 先清除旧帧的所有中间状态。

    if (byte == (uint8_t)'+') // 当前失配字节本身可以作为新帧头起点。
    {
        parser.state = ESP8266_TCP_WAIT_I; // 已经消费了加号，下一步等待 I。
    }
}

/* 保存上层回调并初始化 +IPD 解析器。 */
void Esp8266Tcp_Init(Esp8266Tcp_DataHandler_t data_handler)
{
    tcp_data_handler = data_handler; // 保存收到 TCP 字节后的处理函数。
    tcp_connect_handler = NULL; // 默认不处理 TCP 客户端连接建立通知。
    tcp_raw_echo_enabled = false; // 初始化时默认使用正式 IAP 协议处理路径。
    tcp_status_line_length = 0U; // 清空可能残留的连接状态行。
    tcp_connect_event_pending = false; // 清除上一次运行残留的 CONNECT 事件。
    tcp_connect_event_link_id = 0U; // 清除上一次运行残留的连接编号。
    tcp_packet_pending = false; // 清除上一次运行残留的完整 TCP 包标记。
    tcp_packet_link_id = 0U; // 清除上一次运行残留的数据包连接编号。
    tcp_packet_length = 0U; // 清除上一次运行残留的数据包长度。
    Esp8266Tcp_ResetParser(); // 将解析器置于等待新帧头的状态。
}

/* 保存 TCP 客户端建立连接后的通知函数。 */
void Esp8266Tcp_SetConnectHandler(Esp8266Tcp_ConnectHandler_t connect_handler)
{
    tcp_connect_handler = connect_handler; // 后续识别到 n,CONNECT 时通知上层实际连接编号。
}

/* 设置临时 TCP 原样回显模式，用来单独验证网络收发链路。 */
void Esp8266Tcp_EnableRawEcho(bool enable)
{
    tcp_raw_echo_enabled = enable; // 保存测试开关，下一份完整 TCP 数据包开始生效。
}

/*
 * 在主循环中分发已记录的 TCP 连接建立事件。
 *
 * 不能在识别 "0,CONNECT" 的逐字节解析路径中立刻发送欢迎包，因为
 * AT+CIPSEND 会等待 ESP8266 的后续串口响应。将其放到主循环中执行，
 * 可以确保当前状态行已经全部消费完，避免嵌套串口接收造成数据竞争。
 */
bool Esp8266Tcp_DispatchConnectEvent(void)
{
    uint8_t link_id; // 保存本次要分发的连接编号，防止回调过程中状态被改写。

    if (!tcp_connect_event_pending) // 没有等待处理的连接事件时无需执行任何操作。
    {
        return false; // 明确告诉调用方本次未分发事件。
    }

    link_id = tcp_connect_event_link_id; // 取出解析器识别到的真实连接编号。
    tcp_connect_event_pending = false; // 先清除事件，保证回调期间再次连接也能被记录。

    if (tcp_connect_handler != NULL) // 上层注册了连接建立处理函数才执行回调。
    {
        tcp_connect_handler(link_id); // 由上层在安全的主循环上下文中发送欢迎包。
    }

    return true; // 本次确实处理了一条连接事件。
}

/*
 * 在主循环中分发一包完整的 TCP 负载。
 *
 * 原样回显模式用于验证最小收发链路；正式 OTA 模式则把数据逐字节交给
 * IAP 协议层。两种情况都可能触发 TCP 回包，因此必须避开 +IPD 逐字节
 * 解析过程，以免 CIPSEND 的 AT 响应与尚未处理的输入互相混淆。
 */
bool Esp8266Tcp_DispatchReceivedPacket(void)
{
    uint8_t link_id; // 保存待分发数据包的连接编号。
    uint16_t length; // 保存待分发数据包的长度。
    uint16_t index; // 用于正式 OTA 模式下逐字节调用上层协议。

    if (!tcp_packet_pending) // 缓存中没有完整数据包时无需继续处理。
    {
        return false; // 明确告诉调用方本次没有分发数据包。
    }

    link_id = tcp_packet_link_id; // 取出当前数据包的连接编号。
    length = tcp_packet_length; // 取出当前数据包的有效长度。
    tcp_packet_pending = false; // 先释放缓存使用权，防止回调期间错误重复分发。

    if (tcp_raw_echo_enabled) // 最小联调阶段只验证 TCP 收到什么就回什么。
    {
        (void)Esp8266Tcp_Send(link_id, // 使用收到此包的同一 TCP 连接回传。
                              tcp_payload, // 直接使用已经完整缓存的数据。
                              length, // 回传长度与接收长度完全一致。
                              3000U); // 最多等待三秒完成 ESP8266 发送握手。
        return true; // 无论模块发送结果如何，数据包已经被消费。
    }

    if (tcp_data_handler != NULL) // 正式 OTA 模式已注册协议层处理函数时才继续。
    {
        for (index = 0U; index < length; index++) // 按接收顺序将完整负载交给 IAP 协议。
        {
            tcp_data_handler(link_id, tcp_payload[index]); // 协议层可在换行或帧结束后安全回包。
        }
    }

    return true; // 本次已完成一包 TCP 数据的分发。
}

/* 输入一个串口字节，解析 +IPD 头并把真实 TCP 数据交给上层。 */
bool Esp8266Tcp_ProcessByte(uint8_t byte)
{
    if ((parser.state == ESP8266_TCP_WAIT_PLUS) && // 只有未进入 +IPD 帧头时才解析普通状态文本。
        (byte != (uint8_t)'+')) // 加号必须留给 +IPD 状态机处理。
    {
        if (byte == (uint8_t)'\r') // CR 不属于状态行正文。
        {
            return false; // 等待紧随其后的 LF 完成整行解析。
        }

        if (byte == (uint8_t)'\n') // LF 表示一条 ESP8266 状态行结束。
        {
            if (tcp_status_line_length >= 9U) // 最短连接提示为 0,CONNECT，共九个字符。
            {
                tcp_status_line[tcp_status_line_length] = '\0'; // 把缓存形成可比较的 C 字符串。

                if ((tcp_status_line[0] >= '0') && // 连接编号必须从数字开始。
                    (tcp_status_line[0] <= '4') && // ESP8266 多连接编号范围为零到四。
                    (tcp_status_line[1] == ',') && // 数字后必须紧跟字段分隔逗号。
                    (strcmp(&tcp_status_line[2], "CONNECT") == 0)) // 后续文本必须是连接建立提示。
                {
                    tcp_connect_event_link_id = (uint8_t)(tcp_status_line[0] - '0'); // 保存 ESP8266 实际分配的连接编号。
                    tcp_connect_event_pending = true; // 仅记录事件，稍后由主循环安全地发送欢迎包。
                }
            }

            tcp_status_line_length = 0U; // 无论是否匹配都清空缓存，开始等待下一行。
            return false; // 普通状态行不属于 TCP 负载。
        }

        if (tcp_status_line_length < (uint8_t)(sizeof(tcp_status_line) - 1U)) // 状态行缓存仍有空间。
        {
            tcp_status_line[tcp_status_line_length] = (char)byte; // 保存当前状态文本字节。
            tcp_status_line_length++; // 推进下一字节写入位置。
        }
        else
        {
            tcp_status_line_length = 0U; // 异常长状态行直接丢弃，避免缓存溢出。
        }

        return false; // 普通状态文本不属于 +IPD 数据。
    }

    switch (parser.state) // 根据当前解析阶段处理输入字节。
    {
        case ESP8266_TCP_WAIT_PLUS:
            if (byte == (uint8_t)'+') // 找到 +IPD 帧头的第一个字符。
            {
                parser.state = ESP8266_TCP_WAIT_I; // 下一步等待字符 I。
            }
            return false; // 帧头字符本身不属于真实 TCP 负载。

        case ESP8266_TCP_WAIT_I:
            if (byte == (uint8_t)'I') // 匹配 +IPD 的第二个字符。
            {
                parser.state = ESP8266_TCP_WAIT_P; // 下一步等待字符 P。
            }
            else
            {
                Esp8266Tcp_RestartFromByte(byte); // 失配时重新寻找帧头。
            }
            return false; // 头部字符不交给上层。

        case ESP8266_TCP_WAIT_P:
            if (byte == (uint8_t)'P') // 匹配 +IPD 的第三个字符。
            {
                parser.state = ESP8266_TCP_WAIT_D; // 下一步等待字符 D。
            }
            else
            {
                Esp8266Tcp_RestartFromByte(byte); // 失配时回到寻找帧头。
            }
            return false; // 头部字符不属于 TCP 数据。

        case ESP8266_TCP_WAIT_D:
            if (byte == (uint8_t)'D') // 匹配 +IPD 的第四个字符。
            {
                parser.state = ESP8266_TCP_WAIT_HEADER_COMMA; // 下一步等待头部逗号。
            }
            else
            {
                Esp8266Tcp_RestartFromByte(byte); // 失配时重新寻找帧头。
            }
            return false; // 头部字符不属于 TCP 数据。

        case ESP8266_TCP_WAIT_HEADER_COMMA:
            if (byte == (uint8_t)',') // +IPD 后的逗号表示连接编号即将开始。
            {
                parser.link_id = 0U; // 清空连接编号累加值。
                parser.link_id_started = false; // 标记尚未读取连接编号数字。
                parser.state = ESP8266_TCP_READ_LINK_ID; // 进入连接编号解析阶段。
            }
            else
            {
                Esp8266Tcp_RestartFromByte(byte); // 头部格式错误时重新寻找下一帧。
            }
            return false; // 头部逗号不属于真实 TCP 数据。

        case ESP8266_TCP_READ_LINK_ID:
            if ((byte >= (uint8_t)'0') && // 当前字节是连接编号的数字。
                (byte <= (uint8_t)'9'))
            {
                parser.link_id_started = true; // 标记连接编号已经开始读取。
                parser.link_id = // 按十进制累加当前数字。
                    (parser.link_id * 10U) + // 将已有编号左移一位。
                    (uint32_t)(byte - (uint8_t)'0'); // 加上本次数字值。

                if (parser.link_id > ESP8266_TCP_MAX_LINK_ID) // 拒绝超出模块支持范围的连接编号。
                {
                    Esp8266Tcp_ResetParser(); // 编号异常时丢弃当前帧。
                }
            }
            else if ((byte == (uint8_t)',') && // 逗号表示连接编号字段结束。
                     parser.link_id_started) // 只有读到数字后逗号才有效。
            {
                parser.payload_length = 0U; // 清空负载长度累加值。
                parser.length_started = false; // 标记尚未读取长度数字。
                parser.state = ESP8266_TCP_READ_LENGTH; // 进入负载长度解析阶段。
            }
            else
            {
                Esp8266Tcp_RestartFromByte(byte); // 连接编号格式错误时重新同步。
            }
            return false; // 连接编号头部不交给上层。

        case ESP8266_TCP_READ_LENGTH:
            if ((byte >= (uint8_t)'0') && // 当前字节是负载长度的数字。
                (byte <= (uint8_t)'9'))
            {
                parser.length_started = true; // 标记长度已经开始读取。
                parser.payload_length = // 按十进制累加负载长度。
                    (parser.payload_length * 10U) + // 将已有长度左移一位。
                    (uint32_t)(byte - (uint8_t)'0'); // 加上当前数字值。

                if (parser.payload_length > // 拒绝超过本地缓存上限的数据帧。
                    ESP8266_TCP_MAX_IPD_LENGTH)
                {
                    Esp8266Tcp_ResetParser(); // 长度异常时丢弃当前帧。
                }
            }
            else if ((byte == (uint8_t)':') && // 冒号表示负载长度字段结束。
                     parser.length_started && // 必须已经读取至少一个数字。
                     (parser.payload_length > 0U)) // 空负载帧不进入数据阶段。
            {
                parser.payload_remaining = parser.payload_length; // 初始化剩余负载字节数。
                tcp_payload_length = 0U; // 从缓存首字节开始保存当前 +IPD 的 TCP 数据。
                parser.state = ESP8266_TCP_READ_PAYLOAD; // 进入真实 TCP 数据读取阶段。
            }
            else if ((byte == (uint8_t)',') && // 逗号表示旧版固件附加了远端 IP 和端口。
                     parser.length_started && // 必须已经正确读取负载长度。
                     (parser.payload_length > 0U)) // 空负载不进入后续解析。
            {
                parser.state = ESP8266_TCP_SKIP_REMOTE_INFO; // 忽略远端地址字段直到负载前的冒号。
            }
            else
            {
                Esp8266Tcp_RestartFromByte(byte); // 长度格式错误时重新寻找下一帧。
            }
            return false; // 长度字段本身不属于 TCP 数据。

        case ESP8266_TCP_SKIP_REMOTE_INFO:
            if (byte == (uint8_t)':') // 远端 IP 和端口字段以冒号结束。
            {
                parser.payload_remaining = parser.payload_length; // 使用前面已解析的长度接收真实负载。
                tcp_payload_length = 0U; // 从缓存首字节开始保存 TCP 数据。
                parser.state = ESP8266_TCP_READ_PAYLOAD; // 进入真实 TCP 数据读取阶段。
            }
            return false; // 远端地址信息不属于上层 TCP 负载。

        case ESP8266_TCP_READ_PAYLOAD:
            if (tcp_payload_length >= ESP8266_TCP_MAX_IPD_LENGTH) // 双重检查缓存边界，防止异常响应越界。
            {
                Esp8266Tcp_ResetParser(); // 缓存异常时丢弃本包并重新同步下一帧。
                return false; // 当前异常字节不作为有效 TCP 负载处理。
            }

            tcp_payload[tcp_payload_length] = byte; // 保存当前真实 TCP 负载字节。
            tcp_payload_length++; // 记录缓存中有效数据长度增加一字节。
            parser.payload_remaining--; // 消费一个负载字节。

            if (parser.payload_remaining == 0U) // 该 +IPD 数据帧已经全部读取完成。
            {
                tcp_packet_link_id = (uint8_t)parser.link_id; // 保存已经解析完成的 TCP 连接编号。
                tcp_packet_length = tcp_payload_length; // 保存已经缓存完成的 TCP 负载长度。
                Esp8266Tcp_ResetParser(); // 回到等待下一帧头的状态。
                tcp_packet_pending = true; // 仅标记完整包就绪，交由主循环执行可能阻塞的回传或协议处理。
            }

            return true; // 当前字节确实属于 TCP 负载。

        default:
            Esp8266Tcp_ResetParser(); // 异常状态统一恢复到初始状态。
            return false; // 异常状态下不交付当前字节。
    }
}

/* 等待 ESP8266 返回指定文本，用于确认发送命令和数据的结果。 */
static bool Esp8266Tcp_WaitFor(const char *expected,
                               uint32_t timeout_ms)
{
    uint32_t start_tick; // 保存等待开始时间。
    size_t matched = 0U; // 保存当前匹配长度。
    uint8_t byte; // 保存从 IAP 串口取出的单字节。

    if ((expected == NULL) || (expected[0] == '\0')) // 空响应字符串无法匹配。
    {
        return false; // 参数无效时直接失败。
    }

    start_tick = HAL_GetTick(); // 记录起始节拍。

    while ((HAL_GetTick() - start_tick) < timeout_ms) // 在超时前持续检查串口数据。
    {
        if (IapUart_ReadByte(&byte, 10U)) // 读取一个可用字节。
        {
            if (byte == (uint8_t)expected[matched]) // 当前字节匹配期望文本。
            {
                matched++; // 推进匹配位置。

                if (expected[matched] == '\0') // 期望文本已经完整匹配。
                {
                    return true; // 返回等待成功。
                }
            }
            else
            {
                matched = (byte == (uint8_t)expected[0]) ? 1U : 0U; // 失配后尝试从当前字节重新开始。
            }
        }
    }

    return false; // 超时没有匹配到完整响应。
}

/* 通过指定连接发送 TCP 数据，并等待 ESP8266 的发送确认。 */
bool Esp8266Tcp_Send(uint8_t link_id,
                     const uint8_t *data,
                     uint16_t length,
                     uint32_t timeout_ms)
{
    char command[40]; // 保存 AT+CIPSEND 命令文本。
    int command_length; // 保存格式化输出的长度。

    if ((link_id > ESP8266_TCP_MAX_LINK_ID) || // 检查连接编号是否在模块范围内。
        (data == NULL) || // 检查发送数据指针。
        (length == 0U) || // 不允许发送零长度数据。
        (length > ESP8266_TCP_MAX_IPD_LENGTH)) // 不允许超过单帧最大长度。
    {
        return false; // 参数不合法时直接报告发送失败。
    }

    command_length = snprintf(command, // 生成带连接编号和长度的发送命令。
                              sizeof(command), // 限制命令缓冲长度。
                              "AT+CIPSEND=%u,%u", // 使用 ESP8266 多连接发送格式。
                              (unsigned int)link_id, // 填入目标连接编号。
                              (unsigned int)length); // 填入待发送字节数。

    if ((command_length < 0) || // 检查格式化过程是否失败。
        ((size_t)command_length >= sizeof(command))) // 检查命令是否被截断。
    {
        return false; // 命令不完整时禁止发送。
    }

    IapUart_SendText(command); // 发送 AT+CIPSEND 命令主体。
    IapUart_SendText("\r\n"); // 补充回车换行结束符。

    // ESP8266 返回大于号提示符后，才允许发送真正的数据。
    if (!Esp8266Tcp_WaitFor(">", timeout_ms)) // 等待发送窗口打开。
    {
        return false; // 没有获得发送提示符则停止发送。
    }

    if (!IapUart_SendBytes(data, length)) // 发送实际 TCP 负载。
    {
        return false; // 底层串口发送失败时报告失败。
    }

    // ESP8266 发送成功后返回 SEND OK。
    return Esp8266Tcp_WaitFor("SEND OK", timeout_ms); // 等待模块确认整帧已经发送完成。
}
