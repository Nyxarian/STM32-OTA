#include "wifi_page.h"

#include "wifi_config.h"
#include "wifi_manager.h"
#include "ui_components.h"
#include "ui_nav.h"
#include "ui_theme.h"
#include "lv_port_indev.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/*
 * 无线网络应用页面。
 *
 * 顶部显示当前联网状态，下半区显示附近 WiFi 扫描结果。
 * 页面不再使用白色、蓝色双层外框，也不再包含 AT 诊断入口。
 * 当前选中的网络使用克莱因蓝底色和白色文字，不绘制额外边框。
 */

#define WIFI_SCAN_VISIBLE_ROWS  4U
#define WIFI_PASSWORD_MAX_LEN   32U

/* 320 x 240 横屏页面布局常量。 */
#define WIFI_CONTENT_X          12
#define WIFI_CONTENT_W          296
#define WIFI_TITLE_Y            9
#define WIFI_TITLE_LINE_Y       31
#define WIFI_STATUS_TOP_Y       40
#define WIFI_STATUS_LINE_Y      92
#define WIFI_LIST_TOP_Y         101
#define WIFI_LIST_ROW_HEIGHT    23
#define WIFI_LIST_ROW_H         21
#define WIFI_FOOTER_LINE_Y      217
#define WIFI_HINT_Y             222

/* 页面中文文本统一集中，便于字体生成工具收集这些字符。 */
#define WIFI_TEXT_TITLE         "\xe6\x97\xa0\xe7\xba\xbf\xe7\xbd\x91\xe7\xbb\x9c"
#define WIFI_TEXT_CONNECTED     "\xe5\xb7\xb2\xe8\xbf\x9e\xe6\x8e\xa5"
#define WIFI_TEXT_CONNECTING    "\xe8\xbf\x9e\xe6\x8e\xa5\xe4\xb8\xad"
#define WIFI_TEXT_ERROR         "\xe9\x94\x99\xe8\xaf\xaf"
#define WIFI_TEXT_CHECKING      "\xe6\xa3\x80\xe6\xb5\x8b\xe4\xb8\xad"
#define WIFI_TEXT_DISCONNECTED  "\xe6\x9c\xaa\xe8\xbf\x9e\xe6\x8e\xa5"
#define WIFI_TEXT_STARTING      "\xe5\x90\xaf\xe5\x8a\xa8\xe4\xb8\xad"
#define WIFI_TEXT_NETWORK       "\xe7\xbd\x91\xe7\xbb\x9c: "
#define WIFI_TEXT_ADDRESS       "IP: "
#define WIFI_TEXT_OPEN          "\xe5\xbc\x80\xe6\x94\xbe"
#define WIFI_TEXT_ENCRYPTED     "\xe5\x8a\xa0\xe5\xaf\x86"
#define WIFI_TEXT_SCAN_WAIT     "\xe6\xad\xa3\xe5\x9c\xa8\xe6\x89\xab\xe6\x8f\x8f\xe7\xbd\x91\xe7\xbb\x9c..."
#define WIFI_TEXT_SCAN_FAILED   "\xe6\x89\xab\xe6\x8f\x8f\xe5\xa4\xb1\xe8\xb4\xa5, \xe7\xa1\xae\xe8\xae\xa4\xe9\x94\xae\xe9\x87\x8d\xe6\x96\xb0\xe6\x89\xab\xe6\x8f\x8f"
#define WIFI_TEXT_NO_NETWORK_FIXED "\xe6\x9c\xaa\xe6\x89\xbe\xe5\x88\xb0\xe7\xbd\x91\xe7\xbb\x9c, \xe7\xa1\xae\xe8\xae\xa4\xe9\x94\xae\xe9\x87\x8d\xe6\x96\xb0\xe6\x89\xab\xe6\x8f\x8f"
#define WIFI_TEXT_NO_NETWORK    "\xe6\x9c\xaa\xe6\x89¾\xe5\x88\xb0\xe7\xbd\x91\xe7\xbb\x9c, \xe7\xa1\xae\xe8\xae\xa4\xe9\x94\xae\xe9\x87\x8d\xe6\x96\xb0\xe6\x89\xab\xe6\x8f\x8f"
#define WIFI_TEXT_LIST_HINT     ""
#define WIFI_TEXT_LOGIN_TITLE   "\xe6\x97\xa0\xe7\xba\xbf\xe7\xbd\x91\xe7\xbb\x9c\xe7\x99\xbb\xe5\xbd\x95"
#define WIFI_TEXT_PASSWORD      "\xe5\xaf\x86\xe7\xa0\x81: "
#define WIFI_TEXT_CURRENT_CHAR  "\xe5\xbd\x93\xe5\x89\x8d\xe5\xad\x97\xe7\xac\xa6: "
#define WIFI_TEXT_EDIT_HINT     ""
#define WIFI_TEXT_PLEASE_WAIT   "\xe8\xaf\xb7\xe7\xa8\x8d\xe5\x80\x99"
#define WIFI_TEXT_RETRY_HINT    "\xe8\xbf\x94\xe5\x9b\x9e\xe9\x87\x8d\xe8\xaf\x95"
#define WIFI_TEXT_ESC_BACK      ""

/* 六按键密码编辑使用的英文字母、数字和常用符号字符表。 */
static const char wifi_password_charset[] =
    " abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
    "-_.,!?@#$%&*+/:;=";

typedef enum
{
    WIFI_PAGE_NETWORK = 0, /* 联网状态与扫描列表同屏显示。 */
    WIFI_PAGE_PASSWORD     /* 用户选择网络后的密码输入页面。 */
} WifiPageMode_t;

static lv_obj_t *wifi_parent;
static lv_obj_t *wifi_status_label;
static lv_obj_t *wifi_ssid_label;
static lv_obj_t *wifi_ip_label;
static lv_obj_t *wifi_rssi_label;
static lv_timer_t *wifi_refresh_timer;
static WifiPageMode_t wifi_page_mode;
static uint8_t wifi_scan_selected;
static uint8_t wifi_scan_offset;
static uint8_t wifi_scan_started;
static uint8_t wifi_connect_started;
static uint8_t wifi_error_rendered;
static uint8_t wifi_selected_encryption;
static char wifi_selected_ssid[33];
static char wifi_password[WIFI_PASSWORD_MAX_LEN + 1U];
static uint8_t wifi_password_length;
static uint8_t wifi_password_cursor;

/* 将底层连接状态转换为页面使用的中文文本。 */
static const char *wifi_state_text(WifiState_t state)
{
    switch (state)
    {
        case WIFI_STATE_CONNECTED:    return WIFI_TEXT_CONNECTED;
        case WIFI_STATE_CONNECTING:   return WIFI_TEXT_CONNECTING;
        case WIFI_STATE_ERROR:        return WIFI_TEXT_ERROR;
        case WIFI_STATE_CHECKING:     return WIFI_TEXT_CHECKING;
        case WIFI_STATE_DISCONNECTED: return WIFI_TEXT_DISCONNECTED;
        case WIFI_STATE_STARTING:
        default:                      return WIFI_TEXT_STARTING;
    }
}

/* 返回顶部状态区应显示的文本；扫描不会改变已连接状态。 */
static const char *wifi_connection_text(void)
{
    if ((WifiManager_GetSsid()[0] != '\0') && (WifiManager_GetIp()[0] != '\0'))
    {
        return WIFI_TEXT_CONNECTED;
    }

    return wifi_state_text(WifiManager_GetState());
}

/* 离开或重绘页面前清理六按键输入组，防止旧控件残留焦点。 */
static void wifi_page_clear_group(void)
{
    lv_group_t *group = lv_port_indev_get_group();

    if (group != NULL)
    {
        lv_group_remove_all_objs(group);
    }
}

/* 根据 RSSI 将信号强度转换为四格图标的点亮数量。 */
static uint8_t wifi_signal_level(int16_t rssi)
{
    if (rssi >= -55) return 4U;
    if (rssi >= -67) return 3U;
    if (rssi >= -78) return 2U;
    if (rssi >= -90) return 1U;
    return 0U;
}

/* 绘制四格柱状信号图标，不使用字体图标以避免缺字。 */
static void wifi_create_signal_icon(lv_obj_t *parent,
                                    int16_t x,
                                    int16_t y,
                                    int16_t rssi,
                                    lv_color_t color)
{
    uint8_t level = wifi_signal_level(rssi);
    uint8_t i;

    for (i = 0U; i < 4U; i++)
    {
        uint8_t bar_height = (uint8_t)(4U + i * 4U);
        ui_create_shape(parent,
                        (int16_t)(x + i * 5),
                        (int16_t)(y + 16 - bar_height),
                        3,
                        bar_height,
                        (i < level) ? color : UI_COLOR_BORDER);
    }
}

/* 将 ESP8266 返回的加密类型显示为简短中文。 */
static const char *wifi_security_text(uint8_t encryption)
{
    return (encryption == 0U) ? WIFI_TEXT_OPEN : WIFI_TEXT_ENCRYPTED;
}

/* 创建无边框的蓝底白字扫描结果行。 */
static void wifi_create_scan_row(lv_obj_t *parent,
                                 const WifiScanResult_t *result,
                                 uint8_t selected,
                                 int16_t y)
{
    char ssid_text[25];
    uint8_t level;
    uint8_t i;
    lv_color_t text_color;

    if (selected != 0U)
    {
        ui_create_shape(parent, WIFI_CONTENT_X, y, WIFI_CONTENT_W, WIFI_LIST_ROW_H, UI_COLOR_KL);
    }

    strncpy(ssid_text, result->ssid, sizeof(ssid_text) - 1U);
    ssid_text[sizeof(ssid_text) - 1U] = '\0';
    text_color = (selected != 0U) ? UI_COLOR_TEXT : UI_COLOR_TEXT_DIM;
    ui_create_text(parent, ssid_text, 21, (int16_t)(y + 5), &ui_font_10, text_color);
    ui_create_text(parent, wifi_security_text(result->encryption), 232, (int16_t)(y + 5), &ui_font_10, text_color);

    level = wifi_signal_level(result->rssi);
    for (i = 0U; i < 4U; i++)
    {
        uint8_t bar_height = (uint8_t)(4U + i * 3U);
        ui_create_shape(parent,
                        (int16_t)(282 + i * 5),
                        (int16_t)(y + 16 - bar_height),
                        3,
                        bar_height,
                        (i < level) ? UI_COLOR_TEXT : UI_COLOR_BORDER);
    }
}

/* 仅刷新顶部已连接信息，不重建扫描列表控件。 */
static void wifi_page_refresh_status(void)
{
    char text[48];
    const char *ssid;
    const char *ip;
    const char *state_text;

    if (wifi_status_label == NULL)
    {
        return;
    }

    ssid = WifiManager_GetSsid();
    ip = WifiManager_GetIp();
    state_text = wifi_connection_text();
    lv_label_set_text(wifi_status_label, state_text);
    lv_obj_set_style_text_color(wifi_status_label,
                                (strcmp(state_text, WIFI_TEXT_CONNECTED) == 0) ? UI_COLOR_OK : UI_COLOR_LIGHT_BLUE,
                                LV_PART_MAIN);

    (void)snprintf(text, sizeof(text), "%s%s", WIFI_TEXT_NETWORK, (ssid[0] != '\0') ? ssid : "--");
    lv_label_set_text(wifi_ssid_label, text);
    (void)snprintf(text, sizeof(text), "%s%s", WIFI_TEXT_ADDRESS, (ip[0] != '\0') ? ip : "--");
    lv_label_set_text(wifi_ip_label, text);
    if (ssid[0] != '\0')
    {
        (void)snprintf(text, sizeof(text), "%d dBm", (int)WifiManager_GetRssi());
        lv_label_set_text(wifi_rssi_label, text);
    }
    else
    {
        lv_label_set_text(wifi_rssi_label, "--");
    }
}

/* 绘制标题、当前连接信息和分隔线；页面没有外层边框。 */
static void wifi_page_create_status_area(void)
{
    ui_create_text(wifi_parent, WIFI_TEXT_TITLE, WIFI_CONTENT_X, WIFI_TITLE_Y, &ui_font_14, UI_COLOR_TEXT);
    ui_create_shape(wifi_parent, WIFI_CONTENT_X, WIFI_TITLE_LINE_Y, WIFI_CONTENT_W, 1, UI_COLOR_AUX_BLUE);
    wifi_create_signal_icon(wifi_parent, 20, 42, WifiManager_GetRssi(), UI_COLOR_LIGHT_BLUE);
    wifi_status_label = ui_create_text(wifi_parent, "--", 49, WIFI_STATUS_TOP_Y, &ui_font_12, UI_COLOR_OK);
    wifi_ssid_label = ui_create_text(wifi_parent, "--", 49, 57, &ui_font_10, UI_COLOR_TEXT);
    wifi_ip_label = ui_create_text(wifi_parent, "--", 49, 73, &ui_font_10, UI_COLOR_TEXT_DIM);
    wifi_rssi_label = ui_create_text(wifi_parent, "--", 251, 73, &ui_font_10, UI_COLOR_TEXT_DIM);
    ui_create_shape(wifi_parent, WIFI_CONTENT_X, WIFI_STATUS_LINE_Y, WIFI_CONTENT_W, 1, UI_COLOR_AUX_BLUE);
}

/* 绘制扫描中、无结果或当前可见的扫描结果。 */
static void wifi_page_create_scan_list(void)
{
    uint8_t count = WifiManager_GetScanCount();
    uint8_t i;

    if (WifiManager_IsScanBusy() != 0U)
    {
        ui_create_text(wifi_parent, WIFI_TEXT_SCAN_WAIT, WIFI_CONTENT_X, 137, &ui_font_12, UI_COLOR_LIGHT_BLUE);
        ui_create_text(wifi_parent, WIFI_TEXT_PLEASE_WAIT, WIFI_CONTENT_X, 157, &ui_font_10, UI_COLOR_TEXT_DIM);
        return;
    }

    if (count == 0U)
    {
        ui_create_text(wifi_parent,
                       (WifiManager_IsScanFailed() != 0U) ? WIFI_TEXT_SCAN_FAILED : WIFI_TEXT_NO_NETWORK_FIXED,
                       WIFI_CONTENT_X,
                       137,
                       &ui_font_10,
                       UI_COLOR_LIGHT_BLUE);
        return;
    }

    if (wifi_scan_selected >= count)
    {
        wifi_scan_selected = (uint8_t)(count - 1U);
    }

    if (wifi_scan_offset > wifi_scan_selected)
    {
        wifi_scan_offset = wifi_scan_selected;
    }

    for (i = 0U;
         (i < WIFI_SCAN_VISIBLE_ROWS) && ((uint8_t)(wifi_scan_offset + i) < count);
         i++)
    {
        uint8_t result_index = (uint8_t)(wifi_scan_offset + i);
        const WifiScanResult_t *result = WifiManager_GetScanResult(result_index);

        if (result != NULL)
        {
            wifi_create_scan_row(wifi_parent,
                                 result,
                                 (result_index == wifi_scan_selected) ? 1U : 0U,
                                 (int16_t)(WIFI_LIST_TOP_Y + i * WIFI_LIST_ROW_HEIGHT));
        }
    }
}

/* 在密码字符表中查找当前字符的索引。 */
static uint8_t wifi_password_char_index(char value)
{
    uint8_t i;

    for (i = 0U; i < (uint8_t)(sizeof(wifi_password_charset) - 1U); i++)
    {
        if (wifi_password_charset[i] == value)
        {
            return i;
        }
    }

    return 0U;
}

/* 绘制密码编辑页面或连接结果。 */
static void wifi_page_create_password(void)
{
    char masked[WIFI_PASSWORD_MAX_LEN + 1U];
    char current_char[2];
    uint8_t i;

    ui_create_text(wifi_parent, WIFI_TEXT_LOGIN_TITLE, WIFI_CONTENT_X, WIFI_TITLE_Y, &ui_font_14, UI_COLOR_TEXT);
    ui_create_shape(wifi_parent, WIFI_CONTENT_X, WIFI_TITLE_LINE_Y, WIFI_CONTENT_W, 1, UI_COLOR_AUX_BLUE);
    ui_create_text(wifi_parent, WIFI_TEXT_NETWORK, WIFI_CONTENT_X, 50, &ui_font_10, UI_COLOR_TEXT_DIM);
    ui_create_text(wifi_parent, wifi_selected_ssid, 54, 50, &ui_font_10, UI_COLOR_TEXT);

    if (wifi_connect_started != 0U)
    {
        ui_create_text(wifi_parent, wifi_state_text(WifiManager_GetState()), WIFI_CONTENT_X, 92, &ui_font_14, UI_COLOR_LIGHT_BLUE);
        ui_create_text(wifi_parent,
                       (WifiManager_GetState() == WIFI_STATE_ERROR) ? WIFI_TEXT_RETRY_HINT : WIFI_TEXT_PLEASE_WAIT,
                       WIFI_CONTENT_X,
                       122,
                       &ui_font_10,
                       UI_COLOR_TEXT_DIM);
        return;
    }

    for (i = 0U; i < wifi_password_length; i++)
    {
        masked[i] = '*';
    }
    masked[wifi_password_length] = '\0';
    current_char[0] = wifi_password[wifi_password_cursor];
    current_char[1] = '\0';

    ui_create_text(wifi_parent, WIFI_TEXT_PASSWORD, WIFI_CONTENT_X, 86, &ui_font_12, UI_COLOR_TEXT_DIM);
    ui_create_text(wifi_parent, (wifi_password_length > 0U) ? masked : "--", 70, 86, &ui_font_14, UI_COLOR_TEXT);
    ui_create_text(wifi_parent, WIFI_TEXT_CURRENT_CHAR, WIFI_CONTENT_X, 124, &ui_font_12, UI_COLOR_TEXT_DIM);
    ui_create_text(wifi_parent, current_char, 92, 124, &ui_font_14, UI_COLOR_LIGHT_BLUE);
    ui_create_shape(wifi_parent, WIFI_CONTENT_X, 181, WIFI_CONTENT_W, 1, UI_COLOR_AUX_BLUE);
    ui_create_text(wifi_parent, WIFI_TEXT_EDIT_HINT, WIFI_CONTENT_X, 195, &ui_font_10, UI_COLOR_TEXT_DIM);
    ui_create_text(wifi_parent, WIFI_TEXT_ESC_BACK, WIFI_CONTENT_X, 214, &ui_font_10, UI_COLOR_LIGHT_BLUE);
}

/* 根据当前子页面重新创建控件。 */
static void wifi_page_render(void)
{
    wifi_page_clear_group();
    lv_obj_clean(wifi_parent);
    wifi_status_label = NULL;
    wifi_ssid_label = NULL;
    wifi_ip_label = NULL;
    wifi_rssi_label = NULL;

    if (wifi_page_mode == WIFI_PAGE_PASSWORD)
    {
        wifi_page_create_password();
    }
    else
    {
        wifi_page_create_status_area();
        wifi_page_create_scan_list();
        ui_create_shape(wifi_parent, WIFI_CONTENT_X, WIFI_FOOTER_LINE_Y, WIFI_CONTENT_W, 1, UI_COLOR_AUX_BLUE);
        ui_create_text(wifi_parent, WIFI_TEXT_LIST_HINT, WIFI_CONTENT_X, WIFI_HINT_Y, &ui_font_10, UI_COLOR_LIGHT_BLUE);
        wifi_page_refresh_status();
    }
}

/* 定时刷新状态，并处理扫描或连接完成事件。 */
static void wifi_page_refresh_timer_cb(lv_timer_t *timer)
{
    WifiState_t state;

    LV_UNUSED(timer);
    state = WifiManager_GetState();

    if (wifi_page_mode == WIFI_PAGE_NETWORK)
    {
        wifi_page_refresh_status();
        if ((wifi_scan_started != 0U) && (WifiManager_IsScanBusy() == 0U))
        {
            wifi_scan_started = 0U;
            wifi_page_render();
        }
    }
    else if (wifi_connect_started != 0U)
    {
        if (state == WIFI_STATE_CONNECTED)
        {
            wifi_connect_started = 0U;
            wifi_page_mode = WIFI_PAGE_NETWORK;
            wifi_page_render();
        }
        else if ((state == WIFI_STATE_ERROR) && (wifi_error_rendered == 0U))
        {
            wifi_error_rendered = 1U;
            wifi_page_render();
        }
    }
}

/* 按相对位置移动当前选中的网络，并保持选中项位于可见窗口内。 */
static void wifi_page_select_relative(int8_t step)
{
    uint8_t count = WifiManager_GetScanCount();

    if (count == 0U)
    {
        return;                                                            /* 没有扫描结果时不移动选择项。 */
    }

    if (step < 0)
    {
        if (wifi_scan_selected == 0U)
        {
            wifi_scan_selected = (uint8_t)(count - 1U);                   /* 第一条网络继续向左时循环到最后一条。 */
        }
        else
        {
            wifi_scan_selected--;                                         /* 正常选择上一条 WiFi。 */
        }
    }
    else
    {
        if ((uint8_t)(wifi_scan_selected + 1U) >= count)
        {
            wifi_scan_selected = 0U;                                      /* 最后一条网络继续向右时循环到第一条。 */
        }
        else
        {
            wifi_scan_selected++;                                         /* 正常选择下一条 WiFi。 */
        }
    }

    if (wifi_scan_selected < wifi_scan_offset)
    {
        wifi_scan_offset = wifi_scan_selected;
    }
    else if (wifi_scan_selected >= (uint8_t)(wifi_scan_offset + WIFI_SCAN_VISIBLE_ROWS))
    {
        wifi_scan_offset = (uint8_t)(wifi_scan_selected - WIFI_SCAN_VISIBLE_ROWS + 1U);
    }
}

/* 按上下键翻动超过五条记录的扫描结果窗口。 */
static void wifi_page_change_page(int8_t direction)
{
    uint8_t count = WifiManager_GetScanCount();
    uint8_t last_offset;

    if (count <= WIFI_SCAN_VISIBLE_ROWS)
    {
        return;
    }

    last_offset = (uint8_t)(count - WIFI_SCAN_VISIBLE_ROWS);
    if (direction < 0)
    {
        wifi_scan_offset = (wifi_scan_offset >= WIFI_SCAN_VISIBLE_ROWS) ?
                           (uint8_t)(wifi_scan_offset - WIFI_SCAN_VISIBLE_ROWS) : 0U;
    }
    else
    {
        wifi_scan_offset = ((uint8_t)(wifi_scan_offset + WIFI_SCAN_VISIBLE_ROWS) > last_offset) ?
                           last_offset : (uint8_t)(wifi_scan_offset + WIFI_SCAN_VISIBLE_ROWS);
    }
    wifi_scan_selected = wifi_scan_offset;
}

/* 创建无线网络页面，并在进入时自动扫描一次附近网络。 */
void ui_page_wifi_create(lv_obj_t *parent)
{
    wifi_parent = parent;
    wifi_page_mode = WIFI_PAGE_NETWORK;
    wifi_scan_selected = 0U;
    wifi_scan_offset = 0U;
    wifi_scan_started = 1U;
    wifi_connect_started = 0U;
    wifi_error_rendered = 0U;
    wifi_selected_encryption = 0U;
    wifi_selected_ssid[0] = '\0';
    wifi_password[0] = ' ';
    wifi_password[1] = '\0';
    wifi_password_length = 0U;
    wifi_password_cursor = 0U;
    WifiManager_StartScan();
    wifi_page_render();
    wifi_refresh_timer = lv_timer_create(wifi_page_refresh_timer_cb, 500U, NULL);
}

/* 处理扫描列表选择、密码输入、连接和返回。 */
void ui_page_wifi_on_key(BSP_Key_t key)
{
    uint8_t count;
    const WifiScanResult_t *result;
    uint8_t index;
    uint8_t charset_size;

    if (key == BSP_KEY_ESC)
    {
        if (wifi_page_mode == WIFI_PAGE_PASSWORD)
        {
            wifi_connect_started = 0U;
            wifi_error_rendered = 0U;
            wifi_page_mode = WIFI_PAGE_NETWORK;
            wifi_page_render();
        }
        else
        {
            ui_nav_pop();
        }
        return;
    }

    if (wifi_page_mode == WIFI_PAGE_NETWORK)
    {
        count = WifiManager_GetScanCount();

        if ((key == BSP_KEY_LEFT) || (key == BSP_KEY_RIGHT))
        {
            wifi_page_select_relative((key == BSP_KEY_LEFT) ? -1 : 1);
            wifi_page_render();
            return;
        }

        if (WifiManager_IsScanBusy() != 0U)
        {
            return;
        }

        if (key == BSP_KEY_UP)
        {
            wifi_page_change_page(-1);
            wifi_page_render();
        }
        else if (key == BSP_KEY_DOWN)
        {
            wifi_page_change_page(1);
            wifi_page_render();
        }
        else if (key == BSP_KEY_ENTER)
        {
            if (count == 0U)
            {
                wifi_scan_selected = 0U;
                wifi_scan_offset = 0U;
                wifi_scan_started = 1U;
                WifiManager_StartScan();
                wifi_page_render();
                return;
            }

            result = WifiManager_GetScanResult(wifi_scan_selected);
            if (result != NULL)
            {
                strncpy(wifi_selected_ssid, result->ssid, sizeof(wifi_selected_ssid) - 1U);
                wifi_selected_ssid[sizeof(wifi_selected_ssid) - 1U] = '\0';
                wifi_selected_encryption = result->encryption;
                memset(wifi_password, 0, sizeof(wifi_password));
                wifi_password[0] = ' ';
                wifi_password_length = 0U;
                wifi_password_cursor = 0U;
                wifi_connect_started = 0U;
                wifi_error_rendered = 0U;
                wifi_page_mode = WIFI_PAGE_PASSWORD;

                if (wifi_selected_encryption == 0U)
                {
                    wifi_password[0] = '\0';
                    (void)WifiManager_Connect(wifi_selected_ssid, wifi_password);
                    wifi_connect_started = 1U;
                }
                else if (strcmp(wifi_selected_ssid, WIFI_CONFIG_SSID) == 0)
                {
                    strncpy(wifi_password, WIFI_CONFIG_PASSWORD, WIFI_PASSWORD_MAX_LEN); /* 预存网络无需再次逐字输入密码。 */
                    wifi_password[WIFI_PASSWORD_MAX_LEN] = '\0';                         /* 即使配置过长也保证字符串安全结束。 */
                    wifi_password_length = (uint8_t)strlen(wifi_password);                /* 密码页直接显示对应数量的掩码字符。 */
                    wifi_password_cursor = (wifi_password_length > 0U) ?
                                           (uint8_t)(wifi_password_length - 1U) : 0U;      /* 将编辑位置放在末尾，确认键可直接连接。 */
                }
                wifi_page_render();
            }
        }
        return;
    }

    if (wifi_connect_started != 0U)
    {
        return;
    }

    charset_size = (uint8_t)(sizeof(wifi_password_charset) - 1U);
    if ((key == BSP_KEY_UP) || (key == BSP_KEY_DOWN))
    {
        index = wifi_password_char_index(wifi_password[wifi_password_cursor]);
        if (key == BSP_KEY_UP)
        {
            index = (index + 1U >= charset_size) ? 0U : (uint8_t)(index + 1U);
        }
        else
        {
            index = (index == 0U) ? (uint8_t)(charset_size - 1U) : (uint8_t)(index - 1U);
        }
        wifi_password[wifi_password_cursor] = wifi_password_charset[index];
        if (wifi_password_cursor >= wifi_password_length)
        {
            wifi_password_length = (uint8_t)(wifi_password_cursor + 1U);
        }
        wifi_page_render();
    }
    else if ((key == BSP_KEY_LEFT) && (wifi_password_cursor > 0U))
    {
        wifi_password_cursor--;
        wifi_page_render();
    }
    else if ((key == BSP_KEY_RIGHT) && (wifi_password_cursor < (WIFI_PASSWORD_MAX_LEN - 1U)))
    {
        wifi_password_cursor++;
        if (wifi_password_cursor >= wifi_password_length)
        {
            wifi_password[wifi_password_cursor] = 'A';
            wifi_password_length = (uint8_t)(wifi_password_cursor + 1U);
        }
        wifi_page_render();
    }
    else if (key == BSP_KEY_ENTER)
    {
        while ((wifi_password_length > 0U) && (wifi_password[wifi_password_length - 1U] == ' '))
        {
            wifi_password_length--;
        }
        wifi_password[wifi_password_length] = '\0';
        if (WifiManager_Connect(wifi_selected_ssid, wifi_password) != 0U)
        {
            wifi_connect_started = 1U;
            wifi_error_rendered = 0U;
            wifi_page_render();
        }
    }
}

/* 离开无线网络应用时删除定时器并清空页面对象引用。 */
void ui_page_wifi_leave(void)
{
    if (wifi_refresh_timer != NULL)
    {
        lv_timer_delete(wifi_refresh_timer);
        wifi_refresh_timer = NULL;
    }

    wifi_parent = NULL;
    wifi_status_label = NULL;
    wifi_ssid_label = NULL;
    wifi_ip_label = NULL;
    wifi_rssi_label = NULL;
}
