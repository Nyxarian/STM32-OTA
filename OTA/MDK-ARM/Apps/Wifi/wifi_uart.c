#include "wifi_uart.h"

#include "usart.h"

#include <stddef.h>
#include <string.h>

#define WIFI_UART_RX_BUFFER_SIZE  2048U // 环形接收缓冲总容量，必须是 2 的幂次方。
#define WIFI_UART_RX_BUFFER_MASK  (WIFI_UART_RX_BUFFER_SIZE - 1U) // 通过位与实现环形索引回绕。

/* 中断接收使用的单字节暂存区。 */
static uint8_t wifi_uart_rx_byte;

/* 中断服务函数写入 head，UI 任务读取 tail。 */
static volatile uint16_t wifi_uart_rx_head;
static volatile uint16_t wifi_uart_rx_tail;
static uint8_t wifi_uart_rx_buffer[WIFI_UART_RX_BUFFER_SIZE];

/* 初始化软件缓冲，并启动 USART1 的单字节中断接收。 */
void WifiUart_Init(void)
{
    wifi_uart_rx_head = 0U; // 将写入位置复位到缓冲区开头。
    wifi_uart_rx_tail = 0U; // 将读取位置复位到缓冲区开头。
    memset(wifi_uart_rx_buffer, 0, sizeof(wifi_uart_rx_buffer)); // 清空旧的接收数据。
    __HAL_UART_CLEAR_OREFLAG(&huart1); // 清除可能遗留的串口溢出标志。
    (void)HAL_UART_Receive_IT(&huart1, &wifi_uart_rx_byte, 1U); // 启动 USART1 单字节中断接收。
}

/* 发送一条 AT 命令字符串。 */
void WifiUart_SendText(const char *text)
{
    if (text != NULL) // 只有有效字符串才允许发送。
    {
        (void)HAL_UART_Transmit(&huart1, // 选择连接 ESP8266 的 USART1。
                                (uint8_t *)text, // 将只读字符串转换为 HAL 需要的字节指针。
                                (uint16_t)strlen(text), // 计算实际发送的字符串长度。
                                100U); // 最多等待 100 毫秒完成发送。
    }
}

/* 发送指定长度的原始数据。 */
void WifiUart_SendBytes(const uint8_t *data, uint16_t length)
{
    if ((data != NULL) && (length > 0U)) // 指针有效且存在数据时才发送。
    {
        (void)HAL_UART_Transmit(&huart1, (uint8_t *)data, length, 100U); // 按指定长度发送原始字节。
    }
}

/* 从环形缓冲中非阻塞地取出一个字节。 */
uint8_t WifiUart_ReadByte(uint8_t *byte)
{
    uint16_t tail; // 暂存读取位置，减少对 volatile 变量的重复访问。

    if (byte == NULL) // 调用者没有提供保存字节的地址。
    {
        return 0U; // 返回失败，避免空指针写入。
    }

    tail = wifi_uart_rx_tail; // 读取当前环形缓冲的尾部位置。
    if (tail == wifi_uart_rx_head) // 读写位置相同说明缓冲区为空。
    {
        return 0U; // 当前没有可取出的数据。
    }

    *byte = wifi_uart_rx_buffer[tail]; // 取出尾部位置保存的一个字节。
    wifi_uart_rx_tail = (uint16_t)((tail + 1U) & WIFI_UART_RX_BUFFER_MASK); // 尾部位置循环移动到下一个槽位。
    return 1U; // 告知调用者成功获得一个字节。
}

/* 丢弃尚未读取的串口数据，供 AT 测试切换时隔离旧响应。 */
void WifiUart_ClearRx(void)
{
    wifi_uart_rx_tail = wifi_uart_rx_head; // 让读指针追上写指针，环形缓冲区立即变为空。
}

/* 接收一个字节后写入环形缓冲，并继续接收下一个字节。 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    uint16_t next_head; // 保存写指针移动后的候选位置。

    if ((huart == NULL) || (huart->Instance != USART1)) // 只处理 WiFi 使用的 USART1 中断。
    {
        return; // 其他串口中断不属于本模块。
    }

    next_head = (uint16_t)((wifi_uart_rx_head + 1U) & // 计算循环缓冲中的下一个写位置。
                           WIFI_UART_RX_BUFFER_MASK); // 利用掩码完成容量为 2 的幂次方的回绕。
    if (next_head != wifi_uart_rx_tail) // 下一个位置未追上读指针说明缓冲区尚未满。
    {
        wifi_uart_rx_buffer[wifi_uart_rx_head] = wifi_uart_rx_byte; // 将刚收到的字节写入当前槽位。
        wifi_uart_rx_head = next_head; // 提交新的写入位置。
    }

    (void)HAL_UART_Receive_IT(&huart1, &wifi_uart_rx_byte, 1U); // 立即重新挂起下一次单字节接收。
}

/* UART 出现溢出等错误时清除错误标志并恢复接收。 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if ((huart == NULL) || (huart->Instance != USART1)) // 只恢复 WiFi 串口发生的错误。
    {
        return; // 其他串口交给各自模块处理。
    }

    __HAL_UART_CLEAR_OREFLAG(&huart1); // 清除溢出标志，解除接收阻塞。
    (void)HAL_UART_Receive_IT(&huart1, &wifi_uart_rx_byte, 1U); // 重新启动单字节中断接收。
}
