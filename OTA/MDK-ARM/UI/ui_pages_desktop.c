#include "ui_nav.h"
#include "ui_theme.h"
#include "lv_port_indev.h"
#include "lvgl.h"

typedef enum
{
    UI_DESKTOP_ICON_TETRIS = 0,
    UI_DESKTOP_ICON_WIFI,
    UI_DESKTOP_ICON_SETTINGS,
    UI_DESKTOP_ICON_UPDATE,
    UI_DESKTOP_ICON_WEATHER,
    UI_DESKTOP_ICON_MONITOR
} UiDesktop_Icon_t;

typedef struct
{
    const char *name;
    const char *subtitle;
    const char *number;
    UiDesktop_Icon_t icon;
} UiDesktop_App_t;

static const UiDesktop_App_t desktop_apps[] =
{
    { "\xe4\xbf\x84\xe7\xbd\x97\xe6\x96\xaf\xe6\x96\xb9\xe5\x9d\x97", "TETRIS", "01", UI_DESKTOP_ICON_TETRIS },
    { "\xe7\xbd\x91\xe7\xbb\x9c",             "WIFI",       "02", UI_DESKTOP_ICON_WIFI     },
    { "\xe8\xae\xbe\xe7\xbd\xae",             "SETTINGS",   "03", UI_DESKTOP_ICON_SETTINGS },
    { "\xe7\xb3\xbb\xe7\xbb\x9f\xe5\x8d\x87\xe7\xba\xa7", "OTA UPDATE", "04", UI_DESKTOP_ICON_UPDATE },
    { "\xe5\xa4\xa9\xe6\xb0\x94",             "WEATHER",    "05", UI_DESKTOP_ICON_WEATHER  },
    { "\xe7\xb3\xbb\xe7\xbb\x9f\xe7\x9b\x91\xe6\x8e\xa7", "MONITOR", "06", UI_DESKTOP_ICON_MONITOR },
};

#define DESKTOP_APP_COUNT  (sizeof(desktop_apps) / sizeof(desktop_apps[0]))
#define DESKTOP_COLS       3U
#define DESKTOP_CARD_W     92
#define DESKTOP_CARD_H     72
#define DESKTOP_GAP_X      8
#define DESKTOP_GAP_Y      9
#define DESKTOP_TOP_Y      43
#define DESKTOP_LEFT_X     12

static lv_obj_t *desktop_shape(lv_obj_t *parent,
                               int16_t x,
                               int16_t y,
                               int16_t width,
                               int16_t height,
                               lv_color_t color,
                               int16_t radius)
{
    lv_obj_t *shape = lv_obj_create(parent);

    lv_obj_set_size(shape, width, height);
    lv_obj_set_pos(shape, x, y);
    lv_obj_set_style_bg_color(shape, color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(shape, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(shape, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(shape, radius, LV_PART_MAIN);
    lv_obj_set_style_pad_all(shape, 0, LV_PART_MAIN);
    lv_obj_remove_flag(shape, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(shape, LV_OBJ_FLAG_CLICKABLE);

    return shape;
}

static lv_obj_t *desktop_outline(lv_obj_t *parent,
                                 int16_t x,
                                 int16_t y,
                                 int16_t width,
                                 int16_t height,
                                 lv_color_t color,
                                 int16_t radius)
{
    lv_obj_t *shape = desktop_shape(parent, x, y, width, height,
                                    UI_COLOR_APP_BG, radius);

    lv_obj_set_style_bg_opa(shape, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(shape, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(shape, color, LV_PART_MAIN);

    return shape;
}

static void desktop_create_tetris_icon(lv_obj_t *parent, int16_t x, int16_t y)
{
    desktop_shape(parent, x + 7,  y + 1,  6, 6, UI_COLOR_TEXT,       1);
    desktop_shape(parent, x,      y + 8,  6, 6, UI_COLOR_KL,         1);
    desktop_shape(parent, x + 7,  y + 8,  6, 6, UI_COLOR_LIGHT_BLUE, 1);
    desktop_shape(parent, x + 14, y + 8,  6, 6, UI_COLOR_RED,        1);
}

static void desktop_create_wifi_icon(lv_obj_t *parent, int16_t x, int16_t y)
{
    desktop_shape(parent, x,      y + 15, 4, 5,  UI_COLOR_KL,         0);
    desktop_shape(parent, x + 6,  y + 11, 4, 9,  UI_COLOR_AUX_BLUE,   0);
    desktop_shape(parent, x + 12, y + 6,  4, 14, UI_COLOR_LIGHT_BLUE, 0);
    desktop_shape(parent, x + 18, y,      4, 20, UI_COLOR_TEXT,       0);
}

static void desktop_create_settings_icon(lv_obj_t *parent, int16_t x, int16_t y)
{
    desktop_shape(parent, x,     y + 2,  23, 2, UI_COLOR_TEXT_DIM,   0);
    desktop_shape(parent, x,     y + 9,  23, 2, UI_COLOR_TEXT_DIM,   0);
    desktop_shape(parent, x,     y + 16, 23, 2, UI_COLOR_TEXT_DIM,   0);
    desktop_shape(parent, x + 4, y,      4, 6, UI_COLOR_LIGHT_BLUE, 1);
    desktop_shape(parent, x + 14,y + 7,  4, 6, UI_COLOR_TEXT,       1);
    desktop_shape(parent, x + 8, y + 14, 4, 6, UI_COLOR_KL,         1);
}

static void desktop_create_update_icon(lv_obj_t *parent, int16_t x, int16_t y)
{
    desktop_shape(parent, x + 10, y + 2,  4, 14, UI_COLOR_TEXT,       0);
    desktop_shape(parent, x + 6,  y + 6,  4, 4,  UI_COLOR_LIGHT_BLUE, 0);
    desktop_shape(parent, x + 14, y + 6,  4, 4,  UI_COLOR_LIGHT_BLUE, 0);
    desktop_shape(parent, x + 3,  y + 18, 22, 3,  UI_COLOR_KL,         0);
    desktop_shape(parent, x + 3,  y + 15, 3, 6,   UI_COLOR_KL,         0);
    desktop_shape(parent, x + 22, y + 15, 3, 6,   UI_COLOR_KL,         0);
}

static void desktop_create_weather_icon(lv_obj_t *parent, int16_t x, int16_t y)
{
    desktop_shape(parent, x + 1,  y + 1,  8, 8,  UI_COLOR_LIGHT_BLUE, 4);
    desktop_shape(parent, x + 5,  y + 11, 18, 9, UI_COLOR_TEXT,       4);
    desktop_shape(parent, x + 10, y + 7,  11, 12,UI_COLOR_TEXT,       6);
}

static void desktop_create_monitor_icon(lv_obj_t *parent, int16_t x, int16_t y)
{
    desktop_outline(parent, x,      y,      25, 18, UI_COLOR_LIGHT_BLUE, 1);
    desktop_shape(parent,   x + 4,  y + 10, 3, 5,  UI_COLOR_KL,         0);
    desktop_shape(parent,   x + 9,  y + 7,  3, 8,  UI_COLOR_AUX_BLUE,   0);
    desktop_shape(parent,   x + 14, y + 4,  3, 11, UI_COLOR_TEXT,       0);
    desktop_shape(parent,   x + 20, y + 4,  2, 2,  UI_COLOR_RED,        1);
    desktop_shape(parent,   x + 10, y + 18, 5, 3,  UI_COLOR_TEXT_DIM,   0);
}

static void desktop_create_icon(lv_obj_t *parent, UiDesktop_Icon_t icon)
{
    const int16_t x = 10;
    const int16_t y = 7;

    switch (icon)
    {
        case UI_DESKTOP_ICON_TETRIS:
            desktop_create_tetris_icon(parent, x, y);
            break;

        case UI_DESKTOP_ICON_WIFI:
            desktop_create_wifi_icon(parent, x, y);
            break;

        case UI_DESKTOP_ICON_SETTINGS:
            desktop_create_settings_icon(parent, x, y);
            break;

        case UI_DESKTOP_ICON_UPDATE:
            desktop_create_update_icon(parent, x, y);
            break;

        case UI_DESKTOP_ICON_WEATHER:
            desktop_create_weather_icon(parent, x, y);
            break;

        case UI_DESKTOP_ICON_MONITOR:
            desktop_create_monitor_icon(parent, x, y);
            break;

        default:
            break;
    }
}

/*
 * 桌面应用卡片的点击事件。
 *
 * 用户按下确认键或点击某张应用卡片后，
 * LVGL 会调用这个函数。
 */
static void app_click_event_cb(lv_event_t *event)
{
    lv_obj_t *button;
    uint32_t index;

    /*
     * 取得本次被点击的应用卡片对象。
     */
    button = lv_event_get_target(event);

    /*
     * 每个桌面卡片在创建时都保存了自己的数组下标：
     *
     * lv_obj_set_user_data(button, (void *)(uintptr_t)i);
     *
     * 这里将该下标重新读取出来，用于判断用户打开了哪个应用。
     */
    index = (uint32_t)(uintptr_t)lv_obj_get_user_data(button);

    /*
     * 防止下标超出 desktop_apps[] 数组范围。
     *
     * 正常情况下不会出现，但保留检查可以避免错误访问内存。
     */
    if (index >= DESKTOP_APP_COUNT)
    {
        return;
    }

    /*
     * 根据应用类型进入对应页面。
     *
     * 当前俄罗斯方块和设置拥有独立页面，
     * 其他四个应用仍然进入通用占位页面。
     *
     * 后续开发 WiFi、设置、系统升级等应用时，
     * 继续在这个 switch 中增加对应 case 即可。
     */
    switch (desktop_apps[index].icon)
    {
        /*
         * 用户点击俄罗斯方块卡片：
         * 将 UI_PAGE_TETRIS 压入导航栈，
         * 导航器随后调用 ui_page_tetris_create() 创建游戏页面。
         */
        case UI_DESKTOP_ICON_TETRIS:
            ui_nav_push(UI_PAGE_TETRIS);
            break;

        /* 用户点击 SETTINGS 卡片时进入独立设置页。 */
        case UI_DESKTOP_ICON_WIFI:
            ui_nav_push(UI_PAGE_WIFI);
            break;

        case UI_DESKTOP_ICON_WEATHER:
            ui_nav_push(UI_PAGE_WEATHER);
            break;

        case UI_DESKTOP_ICON_UPDATE:
            ui_nav_push(UI_PAGE_SYSTEM_UPGRADE);
            break;

        case UI_DESKTOP_ICON_SETTINGS: // 匹配桌面设置应用的图标类型
            ui_nav_push(UI_PAGE_SETTINGS); // 将设置页压入导航栈并触发页面创建
            break; // 结束当前分支，避免继续执行默认占位页逻辑

        /* 用户点击系统监控卡片时进入实时监控页面。 */
        case UI_DESKTOP_ICON_MONITOR: // 匹配桌面系统监控应用的图标类型
            ui_nav_push(UI_PAGE_MONITOR); // 将系统监控页压入导航栈并创建页面
            break; // 结束当前分支，避免落入通用占位页

        /*
         * 其余尚未开发的应用：
         * 先把应用中文名称传给通用占位页面，
         * 然后打开 UI_PAGE_PLACEHOLDER。
         */
        default:
            ui_page_placeholder_set_title(desktop_apps[index].name);
            ui_nav_push(UI_PAGE_PLACEHOLDER);
            break;
    }
}

void ui_page_desktop_create(lv_obj_t *parent)
{
    lv_group_t *group = lv_port_indev_get_group();
    uint8_t i;

    if (group != NULL)
    {
        lv_group_remove_all_objs(group);
    }

    {
        lv_obj_t *title = lv_label_create(parent);
        lv_label_set_text(title, "TERMINAL / APPS");
        lv_obj_set_style_text_color(title, UI_COLOR_TEXT, LV_PART_MAIN);
        lv_obj_set_style_text_font(title, &ui_font_12, LV_PART_MAIN);
        lv_obj_set_pos(title, 12, 14);
    }

    {
        lv_obj_t *page_no = lv_label_create(parent);
        lv_label_set_text(page_no, "06");
        lv_obj_set_style_text_color(page_no, UI_COLOR_RED, LV_PART_MAIN);
        lv_obj_set_style_text_font(page_no, &ui_font_12, LV_PART_MAIN);
        lv_obj_align(page_no, LV_ALIGN_TOP_RIGHT, -12, 14);
    }

    desktop_shape(parent, 12, 34, 296, 1, UI_COLOR_KL, 0);
    desktop_shape(parent, 12, 34, 64,  2, UI_COLOR_LIGHT_BLUE, 0);

    for (i = 0U; i < DESKTOP_APP_COUNT; i++)
    {
        uint8_t row = i / DESKTOP_COLS;
        uint8_t col = i % DESKTOP_COLS;
        lv_obj_t *button = lv_button_create(parent);
        lv_obj_t *label;
        lv_obj_t *subtitle;
        lv_obj_t *number;

        lv_obj_set_size(button, DESKTOP_CARD_W, DESKTOP_CARD_H);
        lv_obj_set_pos(button,
                       DESKTOP_LEFT_X + col * (DESKTOP_CARD_W + DESKTOP_GAP_X),
                       DESKTOP_TOP_Y + row * (DESKTOP_CARD_H + DESKTOP_GAP_Y));
        lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(button, 2, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_color(button, UI_COLOR_APP_BG, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(button, UI_COLOR_BORDER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(button, UI_COLOR_DEEP_BLUE, LV_STATE_FOCUSED);
        lv_obj_set_style_border_width(button, 2, LV_STATE_FOCUSED);
        lv_obj_set_style_border_color(button, UI_COLOR_LIGHT_BLUE, LV_STATE_FOCUSED);
        lv_obj_set_style_bg_color(button, UI_COLOR_SELECT, LV_STATE_PRESSED);
        lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);

        desktop_shape(button, 0, 0, DESKTOP_CARD_W, 2, UI_COLOR_KL, 0);
        desktop_create_icon(button, desktop_apps[i].icon);

        number = lv_label_create(button);
        lv_label_set_text(number, desktop_apps[i].number);
        lv_obj_set_style_text_color(number, UI_COLOR_TEXT_DIM, LV_PART_MAIN);
        lv_obj_set_style_text_font(number, &ui_font_10, LV_PART_MAIN);
        lv_obj_align(number, LV_ALIGN_TOP_RIGHT, -7, 7);

        label = lv_label_create(button);
        lv_label_set_text(label, desktop_apps[i].name);
        lv_obj_set_style_text_color(label, UI_COLOR_TEXT, LV_PART_MAIN);
        lv_obj_set_style_text_font(label, &ui_font_12, LV_PART_MAIN);
        lv_obj_set_pos(label, 9, 38);

        subtitle = lv_label_create(button);
        lv_label_set_text(subtitle, desktop_apps[i].subtitle);
        lv_obj_set_style_text_color(subtitle, UI_COLOR_TEXT_DIM, LV_PART_MAIN);
        lv_obj_set_style_text_font(subtitle, &ui_font_10, LV_PART_MAIN);
        lv_obj_set_pos(subtitle, 9, 56);

        lv_obj_set_user_data(button, (void *)(uintptr_t)i);
        lv_obj_add_event_cb(button, app_click_event_cb, LV_EVENT_CLICKED, NULL);

        if (group != NULL)
        {
            lv_group_add_obj(group, button);
        }
    }

    desktop_shape(parent, 12, 207, 296, 1, UI_COLOR_AUX_BLUE, 0);

    {
        lv_obj_t *node_info = lv_label_create(parent);
        lv_label_set_text(node_info, "NODE 550W // READY");
        lv_obj_set_style_text_color(node_info, UI_COLOR_TEXT_DIM, LV_PART_MAIN);
        lv_obj_set_style_text_font(node_info, &ui_font_10, LV_PART_MAIN);
        lv_obj_set_pos(node_info, 12, 214);
    }

    if (group != NULL)
    {
        lv_obj_t *first = lv_group_get_obj_by_index(group, 0);

        if (first != NULL)
        {
            lv_group_focus_obj(first);
        }
    }
}

void ui_page_desktop_on_key(BSP_Key_t key)
{
    if (key == BSP_KEY_ESC)
    {
        ui_nav_pop();
    }
}
