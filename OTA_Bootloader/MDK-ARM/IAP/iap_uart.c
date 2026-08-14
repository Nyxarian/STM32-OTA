#include "iap_uart.h"
#include "usart.h"
#include <string.h>

static const char enter_command[] = "IAP?\r\n";

/**
 * @brief 等待IAP UART接收Enter键
 * 
 * @param timeout_ms 超时时间，单位毫秒
 * @return true 成功
 * @return false 超时
 */
bool IapUart_WaitForEnter(uint32_t timeout_ms)
{
    uint32_t start_tick = HAL_GetTick();
    size_t matched = 0U;
    uint8_t ch;

    while (HAL_GetTick() - start_tick < timeout_ms)
    {
        if (HAL_UART_Receive(&huart1, &ch, 1U, 10U) == HAL_OK)
        {
            if (ch == (uint8_t)enter_command[matched])
            {
                matched++;
                if (matched == (sizeof(enter_command) - 1U))
                {
                    return true;
                }
            }
            else
            {
                matched = (ch == (uint8_t)enter_command[0]) ? 1U : 0U;
            }
        }
    }
    return false;
}

/**
 * @brief 发送文本到IAP UART
 * 
 * @param text 要发送的文本
 */
void IapUart_SendText(const char* text)
{
    if (text != NULL)
    {
        (void)HAL_UART_Transmit(&huart1, (uint8_t *)text, (uint16_t)strlen(text), 1000U);
    }
}

/**
 * @brief 无限循环，将接收到的字符回显到IAP UART
 * 
 */ 
void IapUart_EchoForever(void)
{
    uint8_t ch;
    while (1)
    {
        if (HAL_UART_Receive(&huart1, &ch, 1U, HAL_MAX_DELAY) == HAL_OK)
        {
        (void)HAL_UART_Transmit(&huart1, &ch, 1U, 1000U);
        }
    }
}


/**
 * @brief 从IAP UART读取一个字节
 * 
 * @param byte 存储读取到的字节的指针
 * @param timeout_ms 超时时间，单位毫秒
 * @return true 成功
 * @return false 超时
 */
bool IapUart_ReadByte(uint8_t *byte, uint32_t timeout_ms)
{
    if (byte == NULL)
    {
        return false;
    }

    return HAL_UART_Receive(&huart1, byte, 1U, timeout_ms) == HAL_OK;
}


/**
 * @brief 发送多个字节到IAP UART
 * 
 * @param data 要发送的字节数组
 * @param length 要发送的字节数
 * @return true 成功
 * @return false 失败
 */
bool IapUart_SendBytes(const uint8_t *data, uint16_t length)
{
    if ((data == NULL) || (length == 0U))
    {
        return false;
    }

    return HAL_UART_Transmit(&huart1, (uint8_t *)data, length, 1000U) == HAL_OK;
}
