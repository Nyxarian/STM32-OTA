#include "system_upgrade_page.h"

#include "app_version.h"
#include "boot_flag.h"
#include "main.h"
#include "ui_components.h"
#include "ui_nav.h"
#include "ui_theme.h"
#include "wifi_manager.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef enum
{
    SYSTEM_UPGRADE_IDLE = 0U,
    SYSTEM_UPGRADE_CHECKING,
    SYSTEM_UPGRADE_AVAILABLE,
    SYSTEM_UPGRADE_UP_TO_DATE,
    SYSTEM_UPGRADE_FAILED,
    SYSTEM_UPGRADE_STARTING,
    SYSTEM_UPGRADE_START_FAILED
} SystemUpgradeState_t;

#define SYSTEM_UPGRADE_CONTENT_X       16
#define SYSTEM_UPGRADE_CONTENT_W       288
#define SYSTEM_UPGRADE_HEADER_LINE_Y   42
#define SYSTEM_UPGRADE_PANEL_X         16
#define SYSTEM_UPGRADE_PANEL_Y         50
#define SYSTEM_UPGRADE_PANEL_W         288
#define SYSTEM_UPGRADE_PANEL_H         143
#define SYSTEM_UPGRADE_BUTTON_Y        160
#define SYSTEM_UPGRADE_BUTTON_W        126
#define SYSTEM_UPGRADE_BUTTON_H        30
#define SYSTEM_UPGRADE_BUTTON_GAP      20
#define SYSTEM_UPGRADE_TIMER_MS        150U

#define SYSTEM_UPGRADE_TITLE             "\xe7\xb3\xbb\xe7\xbb\x9f\xe5\x8d\x87\xe7\xba\xa7"
#define SYSTEM_UPGRADE_SUBTITLE          "\xe5\x9b\xba\xe4\xbb\xb6\xe7\xae\xa1\xe7\x90\x86 / 06"
#define SYSTEM_UPGRADE_CURRENT           "\xe5\xbd\x93\xe5\x89\x8d\xe7\x89\x88\xe6\x9c\xac"
#define SYSTEM_UPGRADE_NETWORK           "WiFi: CONNECTED"
#define SYSTEM_UPGRADE_CHECK             "\xe6\xa3\x80\xe6\x9f\xa5\xe6\x9b\xb4\xe6\x96\xb0"
#define SYSTEM_UPGRADE_START             "\xe5\xbc\x80\xe5\xa7\x8b\xe5\x8d\x87\xe7\xba\xa7"
#define SYSTEM_UPGRADE_STATUS_READY      "\xe8\xaf\xb7\xe6\xa3\x80\xe6\x9f\xa5\xe6\x9b\xb4\xe6\x96\xb0"
#define SYSTEM_UPGRADE_STATUS_CHECKING   "\xe6\xad\xa3\xe5\x9c\xa8\xe6\xa3\x80\xe6\x9f\xa5\xe6\x9b\xb4\xe6\x96\xb0"
#define SYSTEM_UPGRADE_STATUS_FOUND      "\xe5\x8f\x91\xe7\x8e\xb0\xe6\x96\xb0\xe7\x89\x88\xe6\x9c\xac"
#define SYSTEM_UPGRADE_STATUS_CURRENT    "\xe5\xb7\xb2\xe6\x98\xaf\xe6\x9c\x80\xe6\x96\xb0\xe7\x89\x88\xe6\x9c\xac"
#define SYSTEM_UPGRADE_STATUS_FAILED     "\xe6\xa3\x80\xe6\x9f\xa5\xe5\xa4\xb1\xe8\xb4\xa5"
#define SYSTEM_UPGRADE_STATUS_STARTING   "\xe6\xad\xa3\xe5\x9c\xa8\xe8\xaf\xb7\xe6\xb1\x82\xe5\x8d\x87\xe7\xba\xa7\xe6\x9c\x8d\xe5\x8a\xa1"
#define SYSTEM_UPGRADE_STATUS_START_FAILED "\xe5\x8d\x87\xe7\xba\xa7\xe5\xa4\xb1\xe8\xb4\xa5"
#define SYSTEM_UPGRADE_DETAIL_IDLE       "\xe6\x9c\xaa\xe6\xa3\x80\xe6\x9f\xa5\xe6\x96\xb0\xe7\x89\x88\xe6\x9c\xac"
#define SYSTEM_UPGRADE_DETAIL_CHECKING   "\xe6\xad\xa3\xe5\x9c\xa8\xe8\xaf\xbb\xe5\x8f\x96\xe7\x89\x88\xe6\x9c\xac\xe4\xbf\xa1\xe6\x81\xaf"
#define SYSTEM_UPGRADE_DETAIL_CURRENT    "\xe6\x9c\x8d\xe5\x8a\xa1\xe5\x99\xa8\xe6\x97\xa0\xe6\x9b\xb4\xe6\x96\xb0"
#define SYSTEM_UPGRADE_DETAIL_FAILED     "WiFi / OTA ERROR"
#define SYSTEM_UPGRADE_DETAIL_BUSY       "WiFi BUSY"
#define SYSTEM_UPGRADE_DETAIL_STARTING   "\xe7\xad\x89\xe5\xbe\x85\xe6\x9c\x8d\xe5\x8a\xa1\xe5\x99\xa8\xe7\xa1\xae\xe8\xae\xa4"
#define SYSTEM_UPGRADE_DETAIL_NOT_CONNECTED "WiFi not connected"
#define SYSTEM_UPGRADE_DETAIL_TCP_FAILED     "Server connect failed"
#define SYSTEM_UPGRADE_DETAIL_TIMEOUT        "Server response timeout"
#define SYSTEM_UPGRADE_DETAIL_BAD_RESPONSE   "Server closed early"
#define SYSTEM_UPGRADE_DETAIL_PARSE_FAILED   "Manifest parse error"
#define SYSTEM_UPGRADE_NETWORK_OFFLINE       "WiFi: OFFLINE"

static lv_obj_t *system_upgrade_status_label;
static lv_obj_t *system_upgrade_detail_label;
static lv_obj_t *system_upgrade_size_label;
static lv_obj_t *system_upgrade_network_label;
static lv_obj_t *system_upgrade_action[2];
static lv_obj_t *system_upgrade_action_label[2];
static lv_timer_t *system_upgrade_timer;
static SystemUpgradeState_t system_upgrade_state;
static uint8_t system_upgrade_selected_action;
static char system_upgrade_remote_version[16];
static uint32_t system_upgrade_remote_size;
static WifiState_t system_upgrade_last_wifi_state;
static uint8_t system_upgrade_wifi_label_valid;
static const char *system_upgrade_failure_detail;

static void system_upgrade_create_divider(lv_obj_t *parent, int16_t y, lv_color_t color)
{
    ui_create_shape(parent, SYSTEM_UPGRADE_CONTENT_X, y, SYSTEM_UPGRADE_CONTENT_W, 1, color);
}

static void system_upgrade_create_action(lv_obj_t *parent, uint8_t index, const char *text)
{
    int16_t x = (index == 0U) ? 24 : (24 + SYSTEM_UPGRADE_BUTTON_W + SYSTEM_UPGRADE_BUTTON_GAP);

    system_upgrade_action[index] = ui_create_outline(parent,
                                                      x,
                                                      SYSTEM_UPGRADE_BUTTON_Y,
                                                      SYSTEM_UPGRADE_BUTTON_W,
                                                      SYSTEM_UPGRADE_BUTTON_H,
                                                      UI_COLOR_BORDER);
    lv_obj_set_style_radius(system_upgrade_action[index], 2, LV_PART_MAIN);
    system_upgrade_action_label[index] = ui_create_text(parent,
                                                         text,
                                                         (int16_t)(x + 12),
                                                         (int16_t)(SYSTEM_UPGRADE_BUTTON_Y + 8),
                                                         &ui_font_10,
                                                         UI_COLOR_TEXT_DIM);
}

static uint8_t system_upgrade_compare_versions(const char *left, const char *right)
{
    uint8_t index;

    for (index = 0U; index < 3U; index++)
    {
        char *left_end;
        char *right_end;
        unsigned long left_value = strtoul(left, &left_end, 10);
        unsigned long right_value = strtoul(right, &right_end, 10);

        if (left_value != right_value)
        {
            return (left_value > right_value) ? 1U : 0U;
        }

        if (index == 2U)
        {
            break;
        }

        if ((*left_end != '.') || (*right_end != '.'))
        {
            return 0U;
        }

        left = left_end + 1;
        right = right_end + 1;
    }

    return 0U;
}

static void system_upgrade_refresh_actions(void)
{
    uint8_t index;
    uint8_t start_disabled = (system_upgrade_state != SYSTEM_UPGRADE_AVAILABLE) ? 1U : 0U;

    for (index = 0U; index < 2U; index++)
    {
        if ((index == 1U) && (start_disabled != 0U))
        {
            lv_obj_set_style_bg_opa(system_upgrade_action[index], LV_OPA_20, LV_PART_MAIN);
            lv_obj_set_style_border_color(system_upgrade_action[index], UI_COLOR_BORDER, LV_PART_MAIN);
            lv_obj_set_style_text_color(system_upgrade_action_label[index], UI_COLOR_TEXT_DIM, LV_PART_MAIN);
        }
        else if (index == system_upgrade_selected_action)
        {
            lv_obj_set_style_bg_color(system_upgrade_action[index], UI_COLOR_KL, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(system_upgrade_action[index], LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_border_color(system_upgrade_action[index], UI_COLOR_LIGHT_BLUE, LV_PART_MAIN);
            lv_obj_set_style_text_color(system_upgrade_action_label[index], UI_COLOR_TEXT, LV_PART_MAIN);
        }
        else
        {
            lv_obj_set_style_bg_opa(system_upgrade_action[index], LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_border_color(system_upgrade_action[index], UI_COLOR_BORDER, LV_PART_MAIN);
            lv_obj_set_style_text_color(system_upgrade_action_label[index], UI_COLOR_LIGHT_BLUE, LV_PART_MAIN);
        }
    }
}

static void system_upgrade_refresh_state_text(void)
{
    const char *status = SYSTEM_UPGRADE_STATUS_READY;
    const char *detail = SYSTEM_UPGRADE_DETAIL_IDLE;
    lv_color_t color = UI_COLOR_TEXT;
    char remote_detail[32];
    char size_text[20];

    if (system_upgrade_state == SYSTEM_UPGRADE_CHECKING)
    {
        status = SYSTEM_UPGRADE_STATUS_CHECKING;
        detail = SYSTEM_UPGRADE_DETAIL_CHECKING;
        color = UI_COLOR_LIGHT_BLUE;
    }
    else if (system_upgrade_state == SYSTEM_UPGRADE_AVAILABLE)
    {
        (void)snprintf(remote_detail, sizeof(remote_detail), "V%s", system_upgrade_remote_version);
        status = SYSTEM_UPGRADE_STATUS_FOUND;
        detail = remote_detail;
        color = UI_COLOR_OK;
    }
    else if (system_upgrade_state == SYSTEM_UPGRADE_UP_TO_DATE)
    {
        status = SYSTEM_UPGRADE_STATUS_CURRENT;
        detail = SYSTEM_UPGRADE_DETAIL_CURRENT;
        color = UI_COLOR_OK;
    }
    else if (system_upgrade_state == SYSTEM_UPGRADE_FAILED)
    {
        status = SYSTEM_UPGRADE_STATUS_FAILED;
        detail = system_upgrade_failure_detail;
        color = UI_COLOR_RED;
    }
    else if (system_upgrade_state == SYSTEM_UPGRADE_STARTING)
    {
        status = SYSTEM_UPGRADE_STATUS_STARTING;
        detail = SYSTEM_UPGRADE_DETAIL_STARTING;
        color = UI_COLOR_LIGHT_BLUE;
    }
    else if (system_upgrade_state == SYSTEM_UPGRADE_START_FAILED)
    {
        status = SYSTEM_UPGRADE_STATUS_START_FAILED;
        detail = SYSTEM_UPGRADE_DETAIL_FAILED;
        color = UI_COLOR_RED;
    }

    lv_label_set_text(system_upgrade_status_label, status);
    lv_label_set_text(system_upgrade_detail_label, detail);
    lv_obj_set_style_text_color(system_upgrade_status_label, color, LV_PART_MAIN);
    lv_obj_set_style_text_color(system_upgrade_detail_label, color, LV_PART_MAIN);

    if (system_upgrade_state == SYSTEM_UPGRADE_AVAILABLE)
    {
        (void)snprintf(size_text,
                       sizeof(size_text),
                       "%lu KB",
                       (unsigned long)((system_upgrade_remote_size + 1023U) / 1024U));
        lv_label_set_text(system_upgrade_size_label, size_text);
    }
    else
    {
        lv_label_set_text(system_upgrade_size_label, "");
    }
}

/* 根据 WiFi 状态机实时刷新右上角网络标签，替换原先写死的 CONNECTED。 */
static void system_upgrade_refresh_network_label(void)
{
    WifiState_t wifi_state = WifiManager_GetState();

    if ((system_upgrade_wifi_label_valid != 0U) &&
        (wifi_state == system_upgrade_last_wifi_state))
    {
        return;
    }

    system_upgrade_wifi_label_valid = 1U;
    system_upgrade_last_wifi_state = wifi_state;

    if (wifi_state == WIFI_STATE_CONNECTED)
    {
        lv_label_set_text(system_upgrade_network_label, SYSTEM_UPGRADE_NETWORK);
        lv_obj_set_style_text_color(system_upgrade_network_label, UI_COLOR_OK, LV_PART_MAIN);
    }
    else
    {
        lv_label_set_text(system_upgrade_network_label, SYSTEM_UPGRADE_NETWORK_OFFLINE);
        lv_obj_set_style_text_color(system_upgrade_network_label, UI_COLOR_RED, LV_PART_MAIN);
    }
}

/* 按 WifiManager 报告的失败原因选择具体提示文本。 */
static void system_upgrade_show_failure(WifiUpgradeFailReason_t reason)
{
    switch (reason)
    {
        case WIFI_UPGRADE_FAIL_NOT_CONNECTED:
            system_upgrade_failure_detail = SYSTEM_UPGRADE_DETAIL_NOT_CONNECTED;
            break;

        case WIFI_UPGRADE_FAIL_TCP:
            system_upgrade_failure_detail = SYSTEM_UPGRADE_DETAIL_TCP_FAILED;
            break;

        case WIFI_UPGRADE_FAIL_TIMEOUT:
            system_upgrade_failure_detail = SYSTEM_UPGRADE_DETAIL_TIMEOUT;
            break;

        case WIFI_UPGRADE_FAIL_RESPONSE:
            system_upgrade_failure_detail = SYSTEM_UPGRADE_DETAIL_BAD_RESPONSE;
            break;

        case WIFI_UPGRADE_FAIL_PARSE:
            system_upgrade_failure_detail = SYSTEM_UPGRADE_DETAIL_PARSE_FAILED;
            break;

        default:
            system_upgrade_failure_detail = SYSTEM_UPGRADE_DETAIL_FAILED;
            break;
    }

    system_upgrade_state = SYSTEM_UPGRADE_FAILED;
    system_upgrade_refresh_state_text();
    system_upgrade_refresh_actions();
}

static void system_upgrade_finish_check(void)
{
    const WifiUpgradeManifest_t *manifest = WifiManager_GetUpgradeManifest();

    if (manifest == NULL)
    {
        system_upgrade_state = SYSTEM_UPGRADE_FAILED;
    }
    else
    {
        (void)snprintf(system_upgrade_remote_version,
                       sizeof(system_upgrade_remote_version),
                       "%s",
                       manifest->version);
        system_upgrade_remote_size = manifest->image_size;
        if (system_upgrade_compare_versions(manifest->version, APP_VERSION_STRING) != 0U)
        {
            system_upgrade_state = SYSTEM_UPGRADE_AVAILABLE;
            system_upgrade_selected_action = 1U;
        }
        else
        {
            system_upgrade_state = SYSTEM_UPGRADE_UP_TO_DATE;
            system_upgrade_selected_action = 0U;
        }
    }

    system_upgrade_refresh_state_text();
    system_upgrade_refresh_actions();
}

static void system_upgrade_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    system_upgrade_refresh_network_label();

    if (system_upgrade_state == SYSTEM_UPGRADE_CHECKING)
    {
        if (WifiManager_IsUpgradeManifestReady() != 0U)
        {
            system_upgrade_finish_check();
        }
        else if (WifiManager_IsUpgradeManifestFailed() != 0U)
        {
            system_upgrade_show_failure(WifiManager_GetUpgradeFailReason());
        }
        return;
    }

    if (system_upgrade_state != SYSTEM_UPGRADE_STARTING)
    {
        return;
    }

    if (WifiManager_IsUpgradeStartReady() != 0U)
    {
        /* 电脑已接受发送任务，复位后由 Bootloader 等待并接收固件。 */
        BootFlag_SetUpdateRequest();
        NVIC_SystemReset();
    }
    else if (WifiManager_IsUpgradeStartFailed() != 0U)
    {
        system_upgrade_state = SYSTEM_UPGRADE_START_FAILED;
        system_upgrade_refresh_state_text();
        system_upgrade_refresh_actions();
    }
}

void ui_page_system_upgrade_create(lv_obj_t *parent)
{
    lv_obj_t *panel;
    char current_version[20];

    system_upgrade_state = SYSTEM_UPGRADE_IDLE;
    system_upgrade_selected_action = 0U;
    system_upgrade_remote_version[0] = '\0';
    system_upgrade_remote_size = 0U;
    system_upgrade_timer = NULL;
    system_upgrade_wifi_label_valid = 0U;
    system_upgrade_failure_detail = SYSTEM_UPGRADE_DETAIL_FAILED;

    ui_create_page_header(parent, SYSTEM_UPGRADE_TITLE, SYSTEM_UPGRADE_SUBTITLE);
    system_upgrade_create_divider(parent, SYSTEM_UPGRADE_HEADER_LINE_Y, UI_COLOR_KL);
    panel = ui_create_shape(parent,
                            SYSTEM_UPGRADE_PANEL_X,
                            SYSTEM_UPGRADE_PANEL_Y,
                            SYSTEM_UPGRADE_PANEL_W,
                            SYSTEM_UPGRADE_PANEL_H,
                            UI_COLOR_APP_BG);
    lv_obj_set_style_radius(panel, 2, LV_PART_MAIN);

    ui_create_text(parent, SYSTEM_UPGRADE_CURRENT, 24, 57, &ui_font_10, UI_COLOR_TEXT_DIM);
    (void)snprintf(current_version, sizeof(current_version), "V%s", APP_VERSION_STRING);
    ui_create_text(parent, current_version, 24, 72, &ui_font_14, UI_COLOR_TEXT);
    system_upgrade_network_label = ui_create_text(parent,
                                                  SYSTEM_UPGRADE_NETWORK,
                                                  190,
                                                  76,
                                                  &ui_font_10,
                                                  UI_COLOR_OK);
    system_upgrade_status_label = ui_create_text(parent,
                                                  SYSTEM_UPGRADE_STATUS_READY,
                                                  24,
                                                  103,
                                                  &ui_font_12,
                                                  UI_COLOR_TEXT);
    system_upgrade_detail_label = ui_create_text(parent,
                                                  SYSTEM_UPGRADE_DETAIL_IDLE,
                                                  24,
                                                  122,
                                                  &ui_font_10,
                                                  UI_COLOR_TEXT_DIM);
    system_upgrade_size_label = ui_create_text(parent, "", 230, 122, &ui_font_10, UI_COLOR_TEXT_DIM);
    system_upgrade_create_action(parent, 0U, SYSTEM_UPGRADE_CHECK);
    system_upgrade_create_action(parent, 1U, SYSTEM_UPGRADE_START);
    system_upgrade_refresh_actions();
    system_upgrade_timer = lv_timer_create(system_upgrade_timer_cb, SYSTEM_UPGRADE_TIMER_MS, NULL);
}

void ui_page_system_upgrade_leave(void)
{
    if (system_upgrade_timer != NULL)
    {
        lv_timer_del(system_upgrade_timer);
        system_upgrade_timer = NULL;
    }
}

void ui_page_system_upgrade_on_key(BSP_Key_t key)
{
    if (key == BSP_KEY_ESC)
    {
        if ((system_upgrade_state != SYSTEM_UPGRADE_CHECKING) &&
            (system_upgrade_state != SYSTEM_UPGRADE_STARTING))
        {
            ui_nav_pop();
        }
        return;
    }

    if ((system_upgrade_state == SYSTEM_UPGRADE_CHECKING) ||
        (system_upgrade_state == SYSTEM_UPGRADE_STARTING))
    {
        return;
    }

    if ((key == BSP_KEY_LEFT) || (key == BSP_KEY_RIGHT))
    {
        if (system_upgrade_state == SYSTEM_UPGRADE_AVAILABLE)
        {
            system_upgrade_selected_action = (system_upgrade_selected_action == 0U) ? 1U : 0U;
            system_upgrade_refresh_actions();
        }
        return;
    }

    if (key != BSP_KEY_ENTER)
    {
        return;
    }

    if (system_upgrade_selected_action == 0U)
    {
        if (WifiManager_RequestUpgradeManifest() != 0U)
        {
            system_upgrade_state = SYSTEM_UPGRADE_CHECKING;
            system_upgrade_refresh_state_text();
            system_upgrade_refresh_actions();
        }
        else
        {
            system_upgrade_state = SYSTEM_UPGRADE_FAILED;
            lv_label_set_text(system_upgrade_status_label, SYSTEM_UPGRADE_STATUS_FAILED);
            lv_label_set_text(system_upgrade_detail_label, SYSTEM_UPGRADE_DETAIL_BUSY);
            lv_obj_set_style_text_color(system_upgrade_status_label, UI_COLOR_RED, LV_PART_MAIN);
            lv_obj_set_style_text_color(system_upgrade_detail_label, UI_COLOR_RED, LV_PART_MAIN);
            lv_label_set_text(system_upgrade_size_label, "");
            system_upgrade_refresh_actions();
        }
        return;
    }

    if (system_upgrade_state == SYSTEM_UPGRADE_AVAILABLE)
    {
        if (WifiManager_RequestUpgradeStart() != 0U)
        {
            system_upgrade_state = SYSTEM_UPGRADE_STARTING;
            system_upgrade_selected_action = 0U;
            system_upgrade_refresh_state_text();
            system_upgrade_refresh_actions();
        }
        else
        {
            system_upgrade_state = SYSTEM_UPGRADE_START_FAILED;
            lv_label_set_text(system_upgrade_status_label, SYSTEM_UPGRADE_STATUS_START_FAILED);
            lv_label_set_text(system_upgrade_detail_label, SYSTEM_UPGRADE_DETAIL_BUSY);
            lv_obj_set_style_text_color(system_upgrade_status_label, UI_COLOR_RED, LV_PART_MAIN);
            lv_obj_set_style_text_color(system_upgrade_detail_label, UI_COLOR_RED, LV_PART_MAIN);
            lv_label_set_text(system_upgrade_size_label, "");
            system_upgrade_refresh_actions();
        }
    }
}
