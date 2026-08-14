#include "boot_display.h"

#include "main.h"
#include "spi.h"

#include <stddef.h>
#include <string.h>

#define LCD_WIDTH               320U
#define LCD_HEIGHT              240U
#define LCD_TIMEOUT_MS          100U
#define LCD_COLOR_BUFFER_PIXELS 64U

#define LCD_COLOR_BG        0x0000U
#define LCD_COLOR_TEXT      0xFFFFU
#define LCD_COLOR_DIM       0x8410U
#define LCD_COLOR_BLUE      0x4D9FU
#define LCD_COLOR_BAR_BG    0x18E3U

typedef struct
{
    char character;
    uint8_t columns[5];
} BootDisplay_Glyph_t;

static const BootDisplay_Glyph_t boot_display_glyphs[] =
{
    {' ', {0x00U, 0x00U, 0x00U, 0x00U, 0x00U}},
    {'%', {0x63U, 0x13U, 0x08U, 0x64U, 0x63U}},
    {'-', {0x08U, 0x08U, 0x08U, 0x08U, 0x08U}},
    {'0', {0x3EU, 0x51U, 0x49U, 0x45U, 0x3EU}},
    {'1', {0x00U, 0x42U, 0x7FU, 0x40U, 0x00U}},
    {'2', {0x42U, 0x61U, 0x51U, 0x49U, 0x46U}},
    {'3', {0x21U, 0x41U, 0x45U, 0x4BU, 0x31U}},
    {'4', {0x18U, 0x14U, 0x12U, 0x7FU, 0x10U}},
    {'5', {0x27U, 0x45U, 0x45U, 0x45U, 0x39U}},
    {'6', {0x3CU, 0x4AU, 0x49U, 0x49U, 0x30U}},
    {'7', {0x01U, 0x71U, 0x09U, 0x05U, 0x03U}},
    {'8', {0x36U, 0x49U, 0x49U, 0x49U, 0x36U}},
    {'9', {0x06U, 0x49U, 0x49U, 0x29U, 0x1EU}},
    {'A', {0x7EU, 0x11U, 0x11U, 0x11U, 0x7EU}},
    {'C', {0x3EU, 0x41U, 0x41U, 0x41U, 0x22U}},
    {'D', {0x7FU, 0x41U, 0x41U, 0x22U, 0x1CU}},
    {'E', {0x7FU, 0x49U, 0x49U, 0x49U, 0x41U}},
    {'F', {0x7FU, 0x09U, 0x09U, 0x09U, 0x01U}},
    {'G', {0x3EU, 0x41U, 0x49U, 0x49U, 0x7AU}},
    {'I', {0x00U, 0x41U, 0x7FU, 0x41U, 0x00U}},
    {'N', {0x7FU, 0x02U, 0x0CU, 0x10U, 0x7FU}},
    {'O', {0x3EU, 0x41U, 0x41U, 0x41U, 0x3EU}},
    {'P', {0x7FU, 0x09U, 0x09U, 0x09U, 0x06U}},
    {'R', {0x7FU, 0x09U, 0x19U, 0x29U, 0x46U}},
    {'S', {0x46U, 0x49U, 0x49U, 0x49U, 0x31U}},
    {'T', {0x01U, 0x01U, 0x7FU, 0x01U, 0x01U}},
    {'U', {0x3FU, 0x40U, 0x40U, 0x40U, 0x3FU}},
    {'V', {0x1FU, 0x20U, 0x40U, 0x20U, 0x1FU}},
    {'W', {0x3FU, 0x40U, 0x38U, 0x40U, 0x3FU}},
    {'Y', {0x07U, 0x08U, 0x70U, 0x08U, 0x07U}}
};

static uint8_t boot_display_color_buffer[LCD_COLOR_BUFFER_PIXELS * 2U];
static uint8_t boot_display_ready;
static uint8_t boot_display_progress_percent;

static uint8_t BootDisplay_Transmit(const uint8_t *data, uint16_t length)
{
    return (HAL_SPI_Transmit(&hspi1,
                             (uint8_t *)data,
                             length,
                             LCD_TIMEOUT_MS) == HAL_OK) ? 1U : 0U;
}

static void BootDisplay_SendCommand(uint8_t command,
                                    const uint8_t *data,
                                    uint16_t length)
{
    HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, GPIO_PIN_RESET);
    (void)BootDisplay_Transmit(&command, 1U);

    if ((data != NULL) && (length != 0U))
    {
        HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, GPIO_PIN_SET);
        (void)BootDisplay_Transmit(data, length);
    }

    HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_SET);
}

static void BootDisplay_SetWindow(uint16_t x,
                                  uint16_t y,
                                  uint16_t width,
                                  uint16_t height)
{
    uint8_t column[4];
    uint8_t page[4];
    uint8_t write_memory = 0x2CU;
    uint16_t x_end = (uint16_t)(x + width - 1U);
    uint16_t y_end = (uint16_t)(y + height - 1U);

    column[0] = (uint8_t)(x >> 8U);
    column[1] = (uint8_t)x;
    column[2] = (uint8_t)(x_end >> 8U);
    column[3] = (uint8_t)x_end;
    page[0] = (uint8_t)(y >> 8U);
    page[1] = (uint8_t)y;
    page[2] = (uint8_t)(y_end >> 8U);
    page[3] = (uint8_t)y_end;

    BootDisplay_SendCommand(0x2AU, column, sizeof(column));
    BootDisplay_SendCommand(0x2BU, page, sizeof(page));

    HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, GPIO_PIN_RESET);
    (void)BootDisplay_Transmit(&write_memory, 1U);
    HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, GPIO_PIN_SET);
}

static void BootDisplay_FillRect(uint16_t x,
                                 uint16_t y,
                                 uint16_t width,
                                 uint16_t height,
                                 uint16_t color)
{
    uint32_t pixels;
    uint16_t buffer_pixels;

    if ((boot_display_ready == 0U) || (x >= LCD_WIDTH) || (y >= LCD_HEIGHT))
    {
        return;
    }

    if ((uint32_t)x + width > LCD_WIDTH)
    {
        width = (uint16_t)(LCD_WIDTH - x);
    }
    if ((uint32_t)y + height > LCD_HEIGHT)
    {
        height = (uint16_t)(LCD_HEIGHT - y);
    }
    if ((width == 0U) || (height == 0U))
    {
        return;
    }

    for (buffer_pixels = 0U; buffer_pixels < LCD_COLOR_BUFFER_PIXELS; buffer_pixels++)
    {
        boot_display_color_buffer[buffer_pixels * 2U] = (uint8_t)(color >> 8U);
        boot_display_color_buffer[buffer_pixels * 2U + 1U] = (uint8_t)color;
    }

    BootDisplay_SetWindow(x, y, width, height);
    pixels = (uint32_t)width * height;
    while (pixels != 0U)
    {
        buffer_pixels = (pixels > LCD_COLOR_BUFFER_PIXELS) ?
                        LCD_COLOR_BUFFER_PIXELS : (uint16_t)pixels;
        (void)BootDisplay_Transmit(boot_display_color_buffer,
                                   (uint16_t)(buffer_pixels * 2U));
        pixels -= buffer_pixels;
    }
    HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_SET);
}

static const uint8_t *BootDisplay_FindGlyph(char character)
{
    uint32_t index;

    for (index = 0U; index < (sizeof(boot_display_glyphs) / sizeof(boot_display_glyphs[0])); index++)
    {
        if (boot_display_glyphs[index].character == character)
        {
            return boot_display_glyphs[index].columns;
        }
    }

    return boot_display_glyphs[0].columns;
}

static void BootDisplay_DrawChar(uint16_t x,
                                 uint16_t y,
                                 char character,
                                 uint8_t scale,
                                 uint16_t color)
{
    const uint8_t *columns = BootDisplay_FindGlyph(character);
    uint8_t column;
    uint8_t row;

    for (column = 0U; column < 5U; column++)
    {
        for (row = 0U; row < 7U; row++)
        {
            if ((columns[column] & (1U << row)) != 0U)
            {
                BootDisplay_FillRect((uint16_t)(x + column * scale),
                                     (uint16_t)(y + row * scale),
                                     scale,
                                     scale,
                                     color);
            }
        }
    }
}

static uint16_t BootDisplay_TextWidth(const char *text, uint8_t scale)
{
    size_t length = strlen(text);

    if (length == 0U)
    {
        return 0U;
    }

    return (uint16_t)((length * 6U - 1U) * scale);
}

static void BootDisplay_DrawCentered(uint16_t y,
                                     const char *text,
                                     uint8_t scale,
                                     uint16_t color)
{
    uint16_t width = BootDisplay_TextWidth(text, scale);
    uint16_t x = (width < LCD_WIDTH) ? (uint16_t)((LCD_WIDTH - width) / 2U) : 0U;

    while (*text != '\0')
    {
        BootDisplay_DrawChar(x, y, *text, scale, color);
        x = (uint16_t)(x + 6U * scale);
        text++;
    }
}

static void BootDisplay_ShowPhase(const char *text)
{
    BootDisplay_FillRect(0U, 0U, LCD_WIDTH, LCD_HEIGHT, LCD_COLOR_BG);
    BootDisplay_FillRect(24U, 42U, 272U, 2U, LCD_COLOR_BLUE);
    BootDisplay_DrawCentered(66U, "OTA UPDATE", 3U, LCD_COLOR_TEXT);
    BootDisplay_DrawCentered(132U, text, 2U, LCD_COLOR_DIM);
    boot_display_progress_percent = 0xFFU;
}

static void BootDisplay_DrawPercent(uint8_t percent)
{
    char text[5];
    uint8_t length = 0U;

    if (percent >= 100U)
    {
        text[length++] = '1';
        text[length++] = '0';
        text[length++] = '0';
    }
    else if (percent >= 10U)
    {
        text[length++] = (char)('0' + percent / 10U);
        text[length++] = (char)('0' + percent % 10U);
    }
    else
    {
        text[length++] = (char)('0' + percent);
    }
    text[length++] = '%';
    text[length] = '\0';

    BootDisplay_FillRect(82U, 104U, 156U, 42U, LCD_COLOR_BG);
    BootDisplay_DrawCentered(104U, text, 5U, LCD_COLOR_TEXT);
}

bool BootDisplay_Init(void)
{
    static const uint8_t gamma_positive[] =
    {
        0xD0U, 0x00U, 0x02U, 0x07U, 0x0AU, 0x28U, 0x32U,
        0x44U, 0x42U, 0x06U, 0x0EU, 0x12U, 0x14U, 0x17U
    };
    static const uint8_t gamma_negative[] =
    {
        0xD0U, 0x00U, 0x02U, 0x07U, 0x0AU, 0x28U, 0x31U,
        0x54U, 0x47U, 0x0EU, 0x1CU, 0x17U, 0x1BU, 0x1EU
    };
    static const uint8_t madctl = 0x60U;
    static const uint8_t pixel_format = 0x55U;
    static const uint8_t gctrl = 0x44U;
    static const uint8_t vcoms = 0x24U;
    static const uint8_t vrhs = 0x13U;
    static const uint8_t pwctrl1[] = {0xA4U, 0xA1U};
    static const uint8_t ramctrl[] = {0x00U, 0xC0U};
    static const uint8_t gamma = 0x01U;

    boot_display_ready = 0U;
    HAL_GPIO_WritePin(LCD_BLK_GPIO_Port, LCD_BLK_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, GPIO_PIN_SET);

    HAL_GPIO_WritePin(LCD_RST_GPIO_Port, LCD_RST_Pin, GPIO_PIN_RESET);
    HAL_Delay(20U);
    HAL_GPIO_WritePin(LCD_RST_GPIO_Port, LCD_RST_Pin, GPIO_PIN_SET);
    HAL_Delay(120U);

    BootDisplay_SendCommand(0x01U, NULL, 0U);
    HAL_Delay(150U);
    BootDisplay_SendCommand(0x11U, NULL, 0U);
    HAL_Delay(150U);
    BootDisplay_SendCommand(0x13U, NULL, 0U);
    BootDisplay_SendCommand(0x36U, &madctl, 1U);
    BootDisplay_SendCommand(0x3AU, &pixel_format, 1U);
    BootDisplay_SendCommand(0xB7U, &gctrl, 1U);
    BootDisplay_SendCommand(0xBBU, &vcoms, 1U);
    BootDisplay_SendCommand(0xC3U, &vrhs, 1U);
    BootDisplay_SendCommand(0xD0U, pwctrl1, sizeof(pwctrl1));
    BootDisplay_SendCommand(0xB0U, ramctrl, sizeof(ramctrl));
    BootDisplay_SendCommand(0xE0U, gamma_positive, sizeof(gamma_positive));
    BootDisplay_SendCommand(0xE1U, gamma_negative, sizeof(gamma_negative));
    BootDisplay_SendCommand(0x26U, &gamma, 1U);
    BootDisplay_SendCommand(0x29U, NULL, 0U);

    boot_display_ready = 1U;
    boot_display_progress_percent = 0xFFU;
    BootDisplay_FillRect(0U, 0U, LCD_WIDTH, LCD_HEIGHT, LCD_COLOR_BG);
    HAL_GPIO_WritePin(LCD_BLK_GPIO_Port, LCD_BLK_Pin, GPIO_PIN_SET);
    return true;
}

void BootDisplay_ShowConnecting(void)
{
    BootDisplay_ShowPhase("WIFI CONNECTING");
}

void BootDisplay_ShowWaiting(void)
{
    BootDisplay_ShowPhase("WAITING FOR PC");
}

void BootDisplay_ShowProgress(uint32_t received_length, uint32_t image_length)
{
    uint8_t percent;
    uint16_t previous_width;
    uint16_t current_width;

    if ((boot_display_ready == 0U) || (image_length == 0U))
    {
        return;
    }

    percent = (uint8_t)((received_length * 100U) / image_length);
    if (percent > 100U)
    {
        percent = 100U;
    }
    if (percent == boot_display_progress_percent)
    {
        return;
    }

    if (boot_display_progress_percent == 0xFFU)
    {
        BootDisplay_ShowPhase("UPDATING");
        BootDisplay_FillRect(22U, 164U, 276U, 22U, LCD_COLOR_DIM);
        BootDisplay_FillRect(24U, 166U, 272U, 18U, LCD_COLOR_BAR_BG);
        previous_width = 0U;
    }
    else
    {
        previous_width = (uint16_t)((272U * boot_display_progress_percent) / 100U);
    }

    current_width = (uint16_t)((272U * percent) / 100U);
    if (current_width > previous_width)
    {
        BootDisplay_FillRect((uint16_t)(24U + previous_width),
                             166U,
                             (uint16_t)(current_width - previous_width),
                             18U,
                             LCD_COLOR_BLUE);
    }

    BootDisplay_DrawPercent(percent);
    boot_display_progress_percent = percent;
}

void BootDisplay_ShowVerifying(void)
{
    BootDisplay_ShowPhase("VERIFYING");
}

void BootDisplay_ShowRestarting(void)
{
    BootDisplay_ShowPhase("RESTARTING");
}

void BootDisplay_ShowFailed(void)
{
    BootDisplay_ShowPhase("UPDATE FAILED");
}
