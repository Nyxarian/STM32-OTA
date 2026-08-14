#ifndef __WIFI_UART_H__
#define __WIFI_UART_H__

#include <stdint.h>

/* 初始化 USART1，并启动中断接收环形缓冲。 */
void WifiUart_Init(void);

/* 发送一条已经包含结尾符的 AT 命令字符串。 */
void WifiUart_SendText(const char *text);

/* 发送指定长度的原始数据。 */
void WifiUart_SendBytes(const uint8_t *data, uint16_t length);

/* 从环形缓冲读取一个字节；无数据时立即返回 0。 */
uint8_t WifiUart_ReadByte(uint8_t *byte);

/* 丢弃当前环形接收缓冲区中尚未被处理的旧数据。 */
void WifiUart_ClearRx(void);

#endif
