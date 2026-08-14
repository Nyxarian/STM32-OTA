#ifndef __IAP_FRAME_H__
#define __IAP_FRAME_H__

#include <stdbool.h>
#include <stdint.h>

#define IAP_FRAME_MAX_PAYLOAD 128U

#define IAP_FRAME_TYPE_PING   0x01U
#define IAP_FRAME_TYPE_START  0x02U
#define IAP_FRAME_TYPE_DATA   0x03U
#define IAP_FRAME_TYPE_END    0x04U
#define IAP_FRAME_TYPE_ABORT  0x05U

#define IAP_FRAME_TYPE_ACK    0x80U
#define IAP_FRAME_TYPE_NACK   0x81U

typedef struct
{
    uint8_t type;
    uint16_t sequence;
    uint16_t length;
    uint8_t payload[IAP_FRAME_MAX_PAYLOAD];
    bool crc_valid;
} IapFrame_t;

typedef void (*IapFrame_Handler_t)(const IapFrame_t *frame);

void IapFrame_Init(IapFrame_Handler_t handler);
bool IapFrame_ProcessByte(uint8_t byte);
bool IapFrame_Send(uint8_t type,
                   uint16_t sequence,
                   const uint8_t *payload,
                   uint16_t length);

#endif
