#ifndef IAP_TRANSPORT_H
#define IAP_TRANSPORT_H

#include <stdbool.h>
#include <stdint.h>

void IapTransport_Init(void);

void IapTransport_UseUart(void);

bool IapTransport_UseEsp8266(uint8_t link_id);

bool IapTransport_SendBytes(const uint8_t *data,
                            uint16_t length);

bool IapTransport_SendText(const char *text);

#endif
