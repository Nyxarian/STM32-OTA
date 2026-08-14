#ifndef __IAP_UART_H__
#define __IAP_UART_H__

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief 等待IAP UART接收Enter键
 * 
 * @param timeout_ms 超时时间，单位毫秒
 * @return true 成功
 * @return false 超时
 */
bool IapUart_WaitForEnter(uint32_t timeout_ms);

/**
 * @brief 发送文本到IAP UART
 * 
 * @param text 要发送的文本
 */
void IapUart_SendText(const char* text);

/**
 * @brief 无限循环，将接收到的字符回显到IAP UART
 * 
 */ 
void IapUart_EchoForever(void);

/**
 * @brief 从IAP UART读取一个字节
 * 
 * @param byte 存储读取到的字节的指针
 * @param timeout_ms 超时时间，单位毫秒
 * @return true 成功
 * @return false 超时
 */
bool IapUart_ReadByte(uint8_t *byte, uint32_t timeout_ms);

/**
 * @brief 发送多个字节到IAP UART
 * 
 * @param data 要发送的字节数组
 * @param length 要发送的字节数
 * @return true 成功
 * @return false 失败
 */
bool IapUart_SendBytes(const uint8_t *data, uint16_t length);

#endif
