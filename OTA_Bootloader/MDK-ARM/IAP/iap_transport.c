#include "iap_transport.h"

#include "iap_uart.h"
#include "esp8266_tcp.h"

#include <stddef.h>
#include <string.h>

#define IAP_TRANSPORT_TCP_TIMEOUT 3000U

typedef enum
{
    IAP_TRANSPORT_MODE_UART = 0,
    IAP_TRANSPORT_MODE_ESP8266_TCP
} IapTransport_Mode_t;

static IapTransport_Mode_t transport_mode;
static uint8_t tcp_link_id;

void IapTransport_Init(void)
{
    transport_mode = IAP_TRANSPORT_MODE_UART;
    tcp_link_id = 0U;
}

void IapTransport_UseUart(void)
{
    transport_mode = IAP_TRANSPORT_MODE_UART;
    tcp_link_id = 0U;
}

bool IapTransport_UseEsp8266(uint8_t link_id)
{
    if (link_id > ESP8266_TCP_MAX_LINK_ID)
    {
        return false;
    }

    tcp_link_id = link_id;
    transport_mode = IAP_TRANSPORT_MODE_ESP8266_TCP;

    return true;
}

bool IapTransport_SendBytes(const uint8_t *data,
                            uint16_t length)
{
    if ((data == NULL) || (length == 0U))
    {
        return false;
    }

    if (transport_mode == IAP_TRANSPORT_MODE_ESP8266_TCP)
    {
        return Esp8266Tcp_Send(tcp_link_id,
                               data,
                               length,
                               IAP_TRANSPORT_TCP_TIMEOUT);
    }

    return IapUart_SendBytes(data, length);
}

bool IapTransport_SendText(const char *text)
{
    size_t length;

    if (text == NULL)
    {
        return false;
    }

    length = strlen(text);

    if ((length == 0U) || (length > 0xFFFFU))
    {
        return false;
    }

    return IapTransport_SendBytes((const uint8_t *)text,
                                  (uint16_t)length);
}
