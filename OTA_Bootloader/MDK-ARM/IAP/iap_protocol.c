#include "iap_protocol.h"
#include "iap_uart.h"
#include "bootloader.h"
#include <stdbool.h>
#include <string.h>
#include "main.h"
#include "iap_frame.h"
#include "iap_flash.h"
#include "iap_transport.h"
#include "esp8266_tcp.h"
#include "esp8266_at.h"
#include "boot_display.h"


#define IAP_PROTOCOL_LINE_MAX 32U
#define IAP_PROTOCOL_START_PAYLOAD_LENGTH 8U
#define IAP_PROTOCOL_START_SEQUENCE       0U

typedef struct
{
    bool active;
    uint32_t image_length;
    uint32_t image_crc32;
    uint32_t calculated_crc32;
    uint32_t received_length;
    uint16_t next_sequence;
} IapProtocol_UpdateState_t;

static IapProtocol_UpdateState_t update_state;

static char protocol_line[IAP_PROTOCOL_LINE_MAX];
static uint32_t protocol_line_length;
static bool protocol_discard_line;

/**
 * @brief 发送IAP协议信息
 * 
 */
static void IapProtocol_SendInfo(void)
{
    (void)IapTransport_SendText("BL=1.0\r\n");
    (void)IapTransport_SendText("APP_ADDR=0x08010000\r\n");

    if (Bootloader_IsAppValid())
    {
        (void)IapTransport_SendText("APP=VALID\r\n");
    }
    else
    {
        (void)IapTransport_SendText("APP=EMPTY\r\n");
    }
}

/**
 * @brief 发送IAP协议原始测试回复
 * 
 */
static void IapProtocol_SendRawTest(void)
{
    static const uint8_t reply[] =
    {
        'R', 'A', 'W', '_', 'O', 'K', '\r', '\n'
    };

    (void)IapTransport_SendBytes(reply, sizeof(reply));
}

/**
 * @brief 发送IAP协议确认
 * 
 * @param sequence 序列号
 */
static void IapProtocol_SendAck(uint16_t sequence)
{
    (void)IapFrame_Send(IAP_FRAME_TYPE_ACK, sequence, 0, 0U);
}

/**
 * @brief 发送IAP协议拒绝
 * 
 * @param sequence 序列号
 */
static void IapProtocol_SendNack(uint16_t sequence)
{
    (void)IapFrame_Send(IAP_FRAME_TYPE_NACK, sequence, 0, 0U);
}

/**
 * @brief 重置IAP协议更新状态
 * 
 */
static void IapProtocol_ResetUpdateState(void)
{
    update_state.active = false;
    update_state.image_length = 0U;
    update_state.image_crc32 = 0U;
    update_state.calculated_crc32 = 0xFFFFFFFFU;
    update_state.received_length = 0U;
    update_state.next_sequence = 0U;
}

/**
 * @brief 从字节序读取32位无符号整数
 * 
 * @param data 要读取的字节
 * @return 无符号整数
 */
static uint32_t IapProtocol_ReadUint32LE(const uint8_t *data)
{
    return ((uint32_t)data[0]) |
           ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) |
           ((uint32_t)data[3] << 24U);
}

/**
 * @brief 更新CRC32校验值
 * 
 * @param crc 当前CRC32校验值
 * @param data 要更新的字节
 * @param length 数据长度
 * @return 更新后的CRC32校验值
 */
static uint32_t IapProtocol_Crc32Update(uint32_t crc,
                                        const uint8_t *data,
                                        uint16_t length)
{
    uint16_t index;
    uint8_t bit;

    for (index = 0U; index < length; index++)
    {
        crc ^= (uint32_t)data[index];

        for (bit = 0U; bit < 8U; bit++)
        {
            if ((crc & 1U) != 0U)
            {
                crc = (crc >> 1U) ^ 0xEDB88320U;
            }
            else
            {
                crc >>= 1U;
            }
        }
    }

    return crc;
}

/**
 * @brief 处理IAP协议开始帧
 * 
 * @param frame 要处理的开始帧
 */
static void IapProtocol_HandleStart(const IapFrame_t *frame)
{
    uint32_t image_length;
    uint32_t image_crc32;

    if ((frame->sequence != IAP_PROTOCOL_START_SEQUENCE) ||
        (frame->length != IAP_PROTOCOL_START_PAYLOAD_LENGTH))
    {
        IapProtocol_SendNack(frame->sequence);
        return;
    }

    if (update_state.active)
    {
        IapProtocol_SendNack(frame->sequence);
        return;
    }

    image_length = IapProtocol_ReadUint32LE(&frame->payload[0]);
    image_crc32 = IapProtocol_ReadUint32LE(&frame->payload[4]);

    if ((image_length == 0U) || ((image_length % 4U) != 0U) || (image_length > (IAP_FLASH_APP_END - IAP_FLASH_APP_START)))
    {
        IapProtocol_SendNack(frame->sequence);
        return;
    }

    if (!IapFlash_EraseApplication())
    {
        BootDisplay_ShowFailed();
        IapProtocol_SendNack(frame->sequence);
        return;
    }

    update_state.active = true;
    update_state.image_length = image_length;
    update_state.image_crc32 = image_crc32;
    update_state.calculated_crc32 = 0xFFFFFFFFU;
    update_state.received_length = 0U;
    update_state.next_sequence = 1U;

    BootDisplay_ShowProgress(0U, update_state.image_length);
    IapProtocol_SendAck(frame->sequence);
}

/**
 * @brief 处理IAP协议数据帧
 * 
 * @param frame 要处理的数据帧
 */
static void IapProtocol_HandleData(const IapFrame_t *frame)
{
    uint32_t remaining;
    uint32_t expected_length;
    uint32_t write_address;

    if (!update_state.active)
    {
        IapProtocol_SendNack(frame->sequence);
        return;
    }

    if (frame->sequence != update_state.next_sequence)
    {
        IapProtocol_SendNack(frame->sequence);
        return;
    }

    remaining = update_state.image_length - update_state.received_length;

    if (remaining > IAP_FRAME_MAX_PAYLOAD)
    {
        expected_length = IAP_FRAME_MAX_PAYLOAD;
    }
    else
    {
        expected_length = remaining;
    }

    if ((frame->length != expected_length) ||
        ((frame->length % 4U) != 0U))
    {
        IapProtocol_SendNack(frame->sequence);
        return;
    }

    write_address = IAP_FLASH_APP_START + update_state.received_length;

    if (!IapFlash_Write(write_address,
                        frame->payload,
                        frame->length))
    {
        BootDisplay_ShowFailed();
        IapProtocol_ResetUpdateState();
        IapProtocol_SendNack(frame->sequence);
        return;
    }

    update_state.calculated_crc32 =
    IapProtocol_Crc32Update(update_state.calculated_crc32, frame->payload, frame->length);
    update_state.received_length += frame->length;
    update_state.next_sequence++;

    BootDisplay_ShowProgress(update_state.received_length, update_state.image_length);
    IapProtocol_SendAck(frame->sequence);
}

/**
 * @brief 处理IAP协议结束帧
 * 
 * @param frame 要处理的结束帧
 */
static void IapProtocol_HandleEnd(const IapFrame_t *frame)
{
    uint32_t calculated_crc32;

    if (!update_state.active)
    {
        IapProtocol_SendNack(frame->sequence);
        return;
    }

    if (frame->sequence != update_state.next_sequence)
    {
        IapProtocol_SendNack(frame->sequence);
        return;
    }

    if (frame->length != 0U)
    {
        IapProtocol_SendNack(frame->sequence);
        return;
    }

    if (update_state.received_length != update_state.image_length)
    {
        IapProtocol_SendNack(frame->sequence);
        return;
    }

    BootDisplay_ShowVerifying();
    calculated_crc32 = update_state.calculated_crc32 ^ 0xFFFFFFFFU;

    if (calculated_crc32 != update_state.image_crc32)
    {
        BootDisplay_ShowFailed();
        IapProtocol_ResetUpdateState();
        IapProtocol_SendNack(frame->sequence);
        return;
    }

    if (!Bootloader_AreAppVectorsValid())
    {
        BootDisplay_ShowFailed();
        IapProtocol_ResetUpdateState();
        IapProtocol_SendNack(frame->sequence);
        return;
    }

    if (!IapFlash_CommitImage(update_state.image_length,
                            update_state.image_crc32))
    {
        BootDisplay_ShowFailed();
        IapProtocol_ResetUpdateState();
        IapProtocol_SendNack(frame->sequence);
        return;
    }

    BootDisplay_ShowRestarting();
    IapProtocol_SendAck(frame->sequence);
    HAL_Delay(200U);
    IapProtocol_ResetUpdateState();

    /*
     * 升级成功后清理 ESP8266 的 TCP 服务器和残留连接。
     * 否则多连接服务器状态会留给新启动的 App，导致其初始化配置链
     * 收到非预期响应而进入失败重试。
     */
    (void)Esp8266At_SendCommand("AT+CIPCLOSE=0", "OK\r\n", 500U);
    (void)Esp8266At_StopTcpServer(500U);

    Bootloader_JumpToApp();
}

/**
 * @brief 处理IAP协议帧
 * 
 * @param frame 要处理的帧
 */
static void IapProtocol_HandleFrame(const IapFrame_t *frame)
{
    if ((frame == 0) || !frame->crc_valid)
    {
        if (frame != 0)
        {
            IapProtocol_SendNack(frame->sequence);
        }
        return;
    }

    if ((frame->type == IAP_FRAME_TYPE_PING) &&
        (frame->length == 0U))
    {
        IapProtocol_SendAck(frame->sequence);
    }
    else if (frame->type == IAP_FRAME_TYPE_START)
    {
        IapProtocol_HandleStart(frame);
    }
    else if (frame->type == IAP_FRAME_TYPE_DATA)
    {
        IapProtocol_HandleData(frame);
    }
    else if (frame->type == IAP_FRAME_TYPE_END)
    {
        IapProtocol_HandleEnd(frame);
    }
    else
    {
        IapProtocol_SendNack(frame->sequence);
    }
}

static void IapProtocol_ResetInputState(void)
{
    memset(protocol_line, 0, sizeof(protocol_line));
    protocol_line_length = 0U;
    protocol_discard_line = false;
}

/**
 * @brief 处理IAP协议行
 * 
 * @param line 要处理的行
 */
static void IapProtocol_HandleLine(const char *line)
{
    if (strcmp(line, "PING") == 0)
    {
        (void)IapTransport_SendText("PONG\r\n");
    }
    else if (strcmp(line, "INFO?") == 0)
    {
        IapProtocol_SendInfo();
    }
    else if (strcmp(line, "RAW?") == 0)
    {
        IapProtocol_SendRawTest();
    }
    else
    {
        (void)IapTransport_SendText("ERR\r\n");
    }
}

static void IapProtocol_ProcessPayloadByte(uint8_t byte)
{
    if (IapFrame_ProcessByte(byte))
    {
        return;
    }

    if (byte == '\r')
    {
        return;
    }

    if (byte == '\n')
    {
        if (!protocol_discard_line &&
            (protocol_line_length > 0U))
        {
            protocol_line[protocol_line_length] = '\0';
            IapProtocol_HandleLine(protocol_line);
        }

        protocol_line_length = 0U;
        protocol_discard_line = false;
        return;
    }

    if (protocol_discard_line)
    {
        return;
    }

    if (protocol_line_length <
        (IAP_PROTOCOL_LINE_MAX - 1U))
    {
        protocol_line[protocol_line_length] = (char)byte;
        protocol_line_length++;
    }
    else
    {
        (void)IapTransport_SendText("ERR:LINE_TOO_LONG\r\n");

        protocol_line_length = 0U;
        protocol_discard_line = true;
    }
}

static void IapProtocol_HandleEsp8266Byte(uint8_t link_id,
                                          uint8_t byte)
{
    if (!IapTransport_UseEsp8266(link_id))
    {
        return;
    }

    IapProtocol_ProcessPayloadByte(byte);
}

/* TCP 客户端连接成功后主动发送欢迎文本，用来单独验证板端发送链路。 */
static void IapProtocol_HandleEsp8266Connect(uint8_t link_id)
{
    static const uint8_t ready_text[] = // 定义连接建立后立即返回电脑的诊断文本。
    {
        'B', 'L', '_', 'T', 'C', 'P', '_', 'R', 'E', 'A', 'D', 'Y', '\r', '\n' // 电脑无需先发送任何内容即可读取此文本。
    };

    (void)Esp8266Tcp_Send(link_id, // 使用 ESP8266 实际分配给当前客户端的连接编号。
                          ready_text, // 发送固定欢迎文本，避免受到 IAP 协议状态影响。
                          (uint16_t)sizeof(ready_text), // 发送完整文本及回车换行。
                          3000U); // 最多等待三秒完成 CIPSEND 发送握手。
}

void IapProtocol_Run(void)
{
    uint8_t byte;

    IapTransport_Init();
    IapTransport_UseUart();

    IapProtocol_ResetUpdateState();
    IapProtocol_ResetInputState();
    IapFrame_Init(IapProtocol_HandleFrame);
    Esp8266Tcp_Init(IapProtocol_HandleEsp8266Byte);

    (void)IapTransport_SendText("IAP_PROTOCOL_READY\r\n");

    for (;;)
    {
        if (!IapUart_ReadByte(&byte, HAL_MAX_DELAY))
        {
            continue;
        }

        /*
         * 双通道并存:
         * 先交给 ESP8266 的 +IPD 解析器。若该字节属于 +IPD 数据,
         * Esp8266Tcp_ProcessByte() 内部已通过回调把帧交给协议层,
         * 且返回 true 表示本字节已被消费。
         */
        if (Esp8266Tcp_ProcessByte(byte))
        {
            continue;
        }

        /*
         * 非 +IPD 字节:可能是串口直连的文本命令/二进制帧,
         * 也可能是 ESP8266 的 AT 响应文本(如 OK/ERROR)。
         * 第一版允许 AT 响应文本按未知命令处理(回复 ERR,从串口发出),
         * 不影响串口与 TCP 两条升级通道的功能。
         */
        IapProtocol_ProcessPayloadByte(byte);
    }
}

void IapProtocol_RunEsp8266(void)
{
    uint8_t byte;

    IapTransport_Init();

    IapProtocol_ResetUpdateState();
    IapProtocol_ResetInputState();
    IapFrame_Init(IapProtocol_HandleFrame);

    Esp8266Tcp_Init(IapProtocol_HandleEsp8266Byte); // 初始化 +IPD 解析器并保存正式协议回调。
    Esp8266Tcp_SetConnectHandler(IapProtocol_HandleEsp8266Connect); // 识别真实连接号并主动发送连接欢迎包。
    Esp8266Tcp_EnableRawEcho(false); // TCP 负载进入正式 IAP 帧解析，返回 ACK/NACK 并执行 OTA 状态机。

    for (;;)
    {
        if (!IapUart_ReadByte(&byte, HAL_MAX_DELAY))
        {
            continue;
        }

        (void)Esp8266Tcp_ProcessByte(byte); // 仅解析当前串口字节，不在解析函数内部嵌套发送 AT 命令。

        /*
         * 连接事件与数据包都在解析完成后才由主循环分发。
         *
         * 这两个函数内部可能执行 AT+CIPSEND 并等待 ESP8266 返回 '>' 或
         * 'SEND OK'。将它们与逐字节 +IPD 解析分开，能够避免一边处理
         * "0,CONNECT" 或 "+IPD"，一边又重入 HAL_UART_Receive() 的时序问题。
         */
        (void)Esp8266Tcp_DispatchConnectEvent(); // 若本次已完整识别 CONNECT，则安全地发送欢迎包。
        (void)Esp8266Tcp_DispatchReceivedPacket(); // 若本次已完整接收 TCP 数据，则安全地执行原样回显。
    }
}
