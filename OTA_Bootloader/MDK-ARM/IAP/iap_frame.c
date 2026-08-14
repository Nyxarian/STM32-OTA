#include "iap_frame.h"
#include "iap_uart.h"
#include "iap_transport.h"

#define IAP_FRAME_SOF0            0xA5U
#define IAP_FRAME_SOF1            0x5AU

typedef enum
{
    IAP_FRAME_WAIT_SOF0 = 0,
    IAP_FRAME_WAIT_SOF1,
    IAP_FRAME_READ_TYPE,
    IAP_FRAME_READ_SEQ_LOW,
    IAP_FRAME_READ_SEQ_HIGH,
    IAP_FRAME_READ_LENGTH_LOW,
    IAP_FRAME_READ_LENGTH_HIGH,
    IAP_FRAME_READ_PAYLOAD,
    IAP_FRAME_READ_CRC_LOW,
    IAP_FRAME_READ_CRC_HIGH
} IapFrameState_t;

typedef struct
{
    IapFrameState_t state;
    uint16_t index;
    uint16_t calculated_crc;
    uint16_t received_crc;
    IapFrame_t frame;
} IapFrameReceiver_t;

static IapFrameReceiver_t frame_receiver;
static IapFrame_Handler_t frame_handler;

static uint16_t IapFrame_Crc16Update(uint16_t crc, uint8_t data)
{
    uint8_t bit;

    crc ^= (uint16_t)data << 8U;

    for (bit = 0U; bit < 8U; bit++)
    {
        if ((crc & 0x8000U) != 0U)
        {
            crc = (uint16_t)((crc << 1U) ^ 0x1021U);
        }
        else
        {
            crc = (uint16_t)(crc << 1U);
        }
    }

    return crc;
}

static void IapFrame_Reset(void)
{
    frame_receiver.state = IAP_FRAME_WAIT_SOF0;
    frame_receiver.frame.type = 0U;
    frame_receiver.frame.sequence = 0U;
    frame_receiver.frame.length = 0U;
    frame_receiver.frame.crc_valid = false;
    frame_receiver.index = 0U;
    frame_receiver.calculated_crc = 0xFFFFU;
    frame_receiver.received_crc = 0U;
}

static void IapFrame_NotifyHandler(bool crc_valid)
{
    frame_receiver.frame.crc_valid = crc_valid;
    if (frame_handler != 0)
    {
        frame_handler(&frame_receiver.frame);
    }
}

void IapFrame_Init(IapFrame_Handler_t handler)
{
    frame_handler = handler;
    IapFrame_Reset();
}

bool IapFrame_Send(uint8_t type,
                   uint16_t sequence,
                   const uint8_t *payload,
                   uint16_t length)
{
    uint8_t frame[IAP_FRAME_MAX_PAYLOAD + 9U];
    uint16_t crc = 0xFFFFU;
    uint16_t index;

    if ((length > IAP_FRAME_MAX_PAYLOAD) ||
        ((length > 0U) && (payload == 0)))
    {
        return false;
    }

    frame[0] = IAP_FRAME_SOF0;
    frame[1] = IAP_FRAME_SOF1;
    frame[2] = type;
    frame[3] = (uint8_t)(sequence & 0xFFU);
    frame[4] = (uint8_t)(sequence >> 8U);
    frame[5] = (uint8_t)(length & 0xFFU);
    frame[6] = (uint8_t)(length >> 8U);

    for (index = 2U; index <= 6U; index++)
    {
        crc = IapFrame_Crc16Update(crc, frame[index]);
    }

    for (index = 0U; index < length; index++)
    {
        frame[7U + index] = payload[index];
        crc = IapFrame_Crc16Update(crc, payload[index]);
    }

    frame[7U + length] = (uint8_t)(crc & 0xFFU);
    frame[8U + length] = (uint8_t)(crc >> 8U);

    return IapTransport_SendBytes(frame, (uint16_t)(length + 9U));
}

bool IapFrame_ProcessByte(uint8_t byte)
{
    switch (frame_receiver.state)
    {
        case IAP_FRAME_WAIT_SOF0:
            if (byte == IAP_FRAME_SOF0)
            {
                frame_receiver.state = IAP_FRAME_WAIT_SOF1;
                return true;
            }
            return false;

        case IAP_FRAME_WAIT_SOF1:
            if (byte == IAP_FRAME_SOF1)
            {
                frame_receiver.state = IAP_FRAME_READ_TYPE;
                frame_receiver.calculated_crc = 0xFFFFU;
            }
            else if (byte != IAP_FRAME_SOF0)
            {
                IapFrame_Reset();
            }
            return true;

        case IAP_FRAME_READ_TYPE:
            frame_receiver.frame.type = byte;
            frame_receiver.calculated_crc =
                IapFrame_Crc16Update(frame_receiver.calculated_crc, byte);
            frame_receiver.state = IAP_FRAME_READ_SEQ_LOW;
            return true;

        case IAP_FRAME_READ_SEQ_LOW:
            frame_receiver.frame.sequence = byte;
            frame_receiver.calculated_crc =
                IapFrame_Crc16Update(frame_receiver.calculated_crc, byte);
            frame_receiver.state = IAP_FRAME_READ_SEQ_HIGH;
            return true;

        case IAP_FRAME_READ_SEQ_HIGH:
            frame_receiver.frame.sequence |= (uint16_t)byte << 8U;
            frame_receiver.calculated_crc =
                IapFrame_Crc16Update(frame_receiver.calculated_crc, byte);
            frame_receiver.state = IAP_FRAME_READ_LENGTH_LOW;
            return true;

        case IAP_FRAME_READ_LENGTH_LOW:
            frame_receiver.frame.length = byte;
            frame_receiver.calculated_crc =
                IapFrame_Crc16Update(frame_receiver.calculated_crc, byte);
            frame_receiver.state = IAP_FRAME_READ_LENGTH_HIGH;
            return true;

        case IAP_FRAME_READ_LENGTH_HIGH:
            frame_receiver.frame.length |= (uint16_t)byte << 8U;
            frame_receiver.calculated_crc =
                IapFrame_Crc16Update(frame_receiver.calculated_crc, byte);

            if (frame_receiver.frame.length > IAP_FRAME_MAX_PAYLOAD)
            {
                IapFrame_NotifyHandler(false);
                IapFrame_Reset();
            }
            else if (frame_receiver.frame.length == 0U)
            {
                frame_receiver.state = IAP_FRAME_READ_CRC_LOW;
            }
            else
            {
                frame_receiver.index = 0U;
                frame_receiver.state = IAP_FRAME_READ_PAYLOAD;
            }
            return true;

        case IAP_FRAME_READ_PAYLOAD:
            frame_receiver.frame.payload[frame_receiver.index] = byte;
            frame_receiver.index++;
            frame_receiver.calculated_crc =
                IapFrame_Crc16Update(frame_receiver.calculated_crc, byte);

            if (frame_receiver.index >= frame_receiver.frame.length)
            {
                frame_receiver.state = IAP_FRAME_READ_CRC_LOW;
            }
            return true;

        case IAP_FRAME_READ_CRC_LOW:
            frame_receiver.received_crc = byte;
            frame_receiver.state = IAP_FRAME_READ_CRC_HIGH;
            return true;

        case IAP_FRAME_READ_CRC_HIGH:
            frame_receiver.received_crc |= (uint16_t)byte << 8U;

            IapFrame_NotifyHandler(frame_receiver.received_crc ==
                                   frame_receiver.calculated_crc);

            IapFrame_Reset();
            return true;

        default:
            IapFrame_Reset();
            return false;
    }
}
