/*
 * 天气应用前端页面。
 *
 * 这一版只负责绘制界面和演示页面切换：
 * 1. 当前天气页显示固定的演示城市、温度、湿度和风速；
 * 2. 三日预报页显示固定的三天预报；
 * 3. 确认键在两个视图之间切换；
 * 4. 预报页左右键移动当前选中的日期；
 * 5. ESC 返回桌面。
 *
 * 页面使用 320 x 240 横屏坐标，并复用 UI 公共组件与主题颜色。
 * 网络、HTTP、JSON 和 ESP8266 后端在后续阶段单独加入。
 */
#include "weather_page.h"

#include "ui_components.h"
#include "ui_nav.h"
#include "ui_theme.h"
#include "lv_port_indev.h"
#include "wifi_manager.h"

#include <stdint.h>
#include <stdio.h>

/* 天气页面的两个显示视图。 */
typedef enum
{
    WEATHER_VIEW_CURRENT = 0U, /* 当前天气视图。 */
    WEATHER_VIEW_FORECAST      /* 三日预报视图。 */
} WeatherView_t;

/* 天气图标类型。 */
typedef enum
{
    WEATHER_ICON_SUNNY = 0U,
    WEATHER_ICON_PARTLY_CLOUDY,
    WEATHER_ICON_OVERCAST,
    WEATHER_ICON_LIGHT_RAIN,
    WEATHER_ICON_MODERATE_RAIN,
    WEATHER_ICON_HEAVY_RAIN
} WeatherIcon_t;

/* 页面布局常量，所有坐标均以页面容器左上角为原点。 */
#define WEATHER_CONTENT_X       16
#define WEATHER_CONTENT_W       288
#define WEATHER_HEADER_LINE_Y   42
#define WEATHER_FOOTER_LINE_Y   204
#define WEATHER_HINT_Y          215

#define WEATHER_CURRENT_PANEL_X 16
#define WEATHER_CURRENT_PANEL_Y 50
#define WEATHER_CURRENT_PANEL_W 288
#define WEATHER_CURRENT_PANEL_H 142

#define WEATHER_FORECAST_TOP_Y  51
#define WEATHER_FORECAST_CARD_W 88
#define WEATHER_FORECAST_CARD_H 143
#define WEATHER_FORECAST_GAP    8
#define WEATHER_STATUS_SHOW_MS  2500U

/* 中文文本统一使用 UTF-8 字节转义，兼容 ARM Compiler 5 的源文件解析。 */
#define WEATHER_TITLE            "\xe5\xa4\xa9\xe6\xb0\x94"
#define WEATHER_SUBTITLE_CURRENT "\xe5\xbd\x93\xe5\x89\x8d\xe5\xa4\xa9\xe6\xb0\x94 / 01"
#define WEATHER_SUBTITLE_FORECAST "\xe4\xb8\x89\xe6\x97\xa5\xe9\xa2\x84\xe6\x8a\xa5 / 02"
#define WEATHER_CITY             "\xe6\x96\xb0\xe4\xb9\xa1"
#define WEATHER_DATE             "2026-08-09"
#define WEATHER_SUNNY            "\xe6\x99\xb4\xe6\x9c\x97"
#define WEATHER_CLOUDY           "\xe5\xa4\x9a\xe4\xba\x91"
#define WEATHER_OVERCAST         "\xe9\x98\xb4\xe5\xa4\xa9"
#define WEATHER_LIGHT_RAIN       "\xe5\xb0\x8f\xe9\x9b\xa8"
#define WEATHER_MODERATE_RAIN    "\xe4\xb8\xad\xe9\x9b\xa8"
#define WEATHER_HEAVY_RAIN       "\xe6\x9a\xb4\xe9\x9b\xa8"
#define WEATHER_TODAY           "\xe4\xbb\x8a\xe5\xa4\xa9"
#define WEATHER_TOMORROW        "\xe6\x98\x8e\xe5\xa4\xa9"
#define WEATHER_AFTER_TOMORROW  "\xe5\x90\x8e\xe5\xa4\xa9"
#define WEATHER_LABEL_FEEL      "\xe4\xbd\x93\xe6\x84\x9f"
#define WEATHER_LABEL_HUMIDITY  "\xe6\xb9\xbf\xe5\xba\xa6"
#define WEATHER_LABEL_WIND      "\xe9\xa3\x8e\xe9\x80\x9f"
#define WEATHER_LABEL_AIR       "\xe7\xa9\xba\xe6\xb0\x94"
#define WEATHER_FETCHING        "\xe8\x8e\xb7\xe5\x8f\x96\xe4\xb8\xad"
#define WEATHER_UPDATED         "\xe5\xb7\xb2\xe6\x9b\xb4\xe6\x96\xb0"
#define WEATHER_REQUEST_FAILED  "\xe8\xaf\xb7\xe6\xb1\x82\xe5\xa4\xb1\xe8\xb4\xa5"
#define WEATHER_NO_TIME         "--"
#define WEATHER_HINT_CURRENT    ""
#define WEATHER_HINT_FORECAST   ""

/* 天气页面当前的父容器和视图状态。 */
static lv_obj_t *weather_parent;
static WeatherView_t weather_view;
static lv_obj_t *weather_feel_label; /* 当前天气页的动态体感温度数值标签。 */
static lv_obj_t *weather_humidity_label; /* 当前天气页的动态湿度数值标签。 */
static lv_obj_t *weather_wind_label; /* 当前天气页的动态风速数值标签。 */
static lv_obj_t *weather_air_label; /* 当前天气页的动态空气质量数值标签。 */
static uint8_t weather_update_notice_shown; /* 本次请求成功后是否已经显示过“已更新”。 */
static uint8_t weather_forecast_data_rendered; /* 三日预报页是否已经按本次真实数据重绘。 */
static uint32_t weather_status_expire_tick; /* “已更新”提示自动隐藏的绝对时间点。 */

static void weather_rebuild(void); /* 在预报数据到达后重绘三张预报卡片。 */
static uint8_t weather_forecast_selected;
static lv_obj_t *weather_temperature_label; /* 当前天气页的动态温度标签。 */
static lv_obj_t *weather_time_label; /* 当前天气页的动态接口时间标签。 */
static lv_obj_t *weather_status_label; /* 当前天气页的动态获取状态标签。 */
static lv_obj_t *weather_condition_label; /* 当前天气页的动态天气描述标签。 */
static lv_obj_t *weather_icon_parent; /* 当前天气页的动态图标容器。 */
static lv_timer_t *weather_refresh_timer; /* 定期将后端状态更新到 LCD 的 LVGL 定时器。 */
static int16_t weather_last_code; /* 上一次已绘制图标的 WMO 天气代码。 */

/* 创建天气页面中的一条细分割线。 */
static void weather_create_divider(lv_obj_t *parent, int16_t y, lv_color_t color)
{
    ui_create_shape(parent, WEATHER_CONTENT_X, y, WEATHER_CONTENT_W, 1, color); // 绘制横向分割线
}

/* 创建天气页面底部的操作提示。 */
static void weather_create_footer(lv_obj_t *parent, const char *hint)
{
    weather_create_divider(parent, WEATHER_FOOTER_LINE_Y, UI_COLOR_AUX_BLUE); // 分隔主体内容与底部提示
    ui_create_text(parent, hint, WEATHER_CONTENT_X, WEATHER_HINT_Y, &ui_font_10, UI_COLOR_TEXT_DIM); // 显示按键提示
}

/* 创建一个带圆角的图形，用于太阳主体和太阳光线。 */
static lv_obj_t *weather_create_round_shape(lv_obj_t *parent,
                                             int16_t x,
                                             int16_t y,
                                             int16_t width,
                                             int16_t height,
                                             int16_t radius,
                                             lv_color_t color)
{
    lv_obj_t *shape; // 保存圆角图形对象

    shape = ui_create_shape(parent, x, y, width, height, color); // 创建基础纯色图形
    lv_obj_set_style_radius(shape, radius, LV_PART_MAIN); // 将普通矩形改为圆角图形

    return shape; // 返回创建好的圆角图形
}

/* 绘制像素风但轮廓规整的晴天太阳图标，避免使用额外图片资源。 */
static void weather_create_sun_icon(lv_obj_t *parent, int16_t x, int16_t y)
{
    /* 中心使用较小的圆形，避免图标显得厚重。 */
    weather_create_round_shape(parent, x + 13, y + 13, 18, 18, 9, UI_COLOR_TEXT); // 绘制白色太阳主体

    /* 使用细长圆角线条绘制四个主方向光线。 */
    weather_create_round_shape(parent, x + 20, y + 1, 4, 7, 2, UI_COLOR_LIGHT_BLUE); // 绘制上方光线
    weather_create_round_shape(parent, x + 20, y + 36, 4, 7, 2, UI_COLOR_LIGHT_BLUE); // 绘制下方光线
    weather_create_round_shape(parent, x + 1, y + 20, 7, 4, 2, UI_COLOR_LIGHT_BLUE); // 绘制左侧光线
    weather_create_round_shape(parent, x + 36, y + 20, 7, 4, 2, UI_COLOR_LIGHT_BLUE); // 绘制右侧光线

    /* 对角线使用小圆点，形成细致的八方向轮廓。 */
    weather_create_round_shape(parent, x + 7, y + 7, 5, 5, 2, UI_COLOR_KL); // 绘制左上光点
    weather_create_round_shape(parent, x + 32, y + 7, 5, 5, 2, UI_COLOR_KL); // 绘制右上光点
    weather_create_round_shape(parent, x + 7, y + 32, 5, 5, 2, UI_COLOR_KL); // 绘制左下光点
    weather_create_round_shape(parent, x + 32, y + 32, 5, 5, 2, UI_COLOR_KL); // 绘制右下光点
}

/* 绘制当前天气页右侧的四项天气指标。 */
/* 绘制阴天云朵图标，由多个圆角几何体叠加形成自然轮廓。 */
static void weather_create_cloud_icon(lv_obj_t *parent, int16_t x, int16_t y, lv_color_t color)
{
    weather_create_round_shape(parent, x + 7, y + 8, 17, 17, 8, color); // 绘制左侧云团
    weather_create_round_shape(parent, x + 17, y + 3, 21, 21, 10, color); // 绘制顶部云团
    weather_create_round_shape(parent, x + 4, y + 17, 36, 13, 6, color); // 绘制云朵底部
}

/* 绘制少云或多云图标：先画太阳，再用前景云朵遮住部分太阳。 */
static void weather_create_partly_cloudy_icon(lv_obj_t *parent, int16_t x, int16_t y, lv_color_t color)
{
    weather_create_sun_icon(parent, x, (int16_t)(y - 3)); // 在左上方绘制太阳，形成从云后露出的效果。
    weather_create_cloud_icon(parent, (int16_t)(x + 8), (int16_t)(y + 13), color); // 叠加前景云朵，表现少云或多云。
}

/* 绘制阴天图标：使用深浅两层云朵体现厚云层。 */
static void weather_create_overcast_icon(lv_obj_t *parent, int16_t x, int16_t y, lv_color_t color)
{
    weather_create_cloud_icon(parent, x, (int16_t)(y + 12), UI_COLOR_TEXT_DIM); // 绘制较暗的后层云朵。
    weather_create_cloud_icon(parent, (int16_t)(x + 8), (int16_t)(y + 4), color); // 绘制较亮的前层云朵。
}

/* 绘制小雨图标：三条短雨线表示轻微降水。 */
static void weather_create_light_rain_icon(lv_obj_t *parent, int16_t x, int16_t y, lv_color_t color)
{
    weather_create_cloud_icon(parent, x, y, color); // 先绘制云朵主体。
    weather_create_round_shape(parent, x + 10, y + 34, 3, 6, 1, UI_COLOR_LIGHT_BLUE); // 绘制第一条短雨线。
    weather_create_round_shape(parent, x + 20, y + 34, 3, 6, 1, UI_COLOR_LIGHT_BLUE); // 绘制第二条短雨线。
    weather_create_round_shape(parent, x + 30, y + 34, 3, 6, 1, UI_COLOR_LIGHT_BLUE); // 绘制第三条短雨线。
}

/* 绘制中雨图标：四条中等长度雨线增加降水密度。 */
static void weather_create_moderate_rain_icon(lv_obj_t *parent, int16_t x, int16_t y, lv_color_t color)
{
    weather_create_cloud_icon(parent, x, y, color); // 先绘制云朵主体。
    weather_create_round_shape(parent, x + 6, y + 33, 3, 9, 1, UI_COLOR_LIGHT_BLUE); // 绘制第一条中等雨线。
    weather_create_round_shape(parent, x + 15, y + 33, 3, 9, 1, UI_COLOR_LIGHT_BLUE); // 绘制第二条中等雨线。
    weather_create_round_shape(parent, x + 24, y + 33, 3, 9, 1, UI_COLOR_LIGHT_BLUE); // 绘制第三条中等雨线。
    weather_create_round_shape(parent, x + 33, y + 33, 3, 9, 1, UI_COLOR_LIGHT_BLUE); // 绘制第四条中等雨线。
}

/* 绘制暴雨图标：五条长雨线和更暗的云层突出强降水。 */
static void weather_create_heavy_rain_icon(lv_obj_t *parent, int16_t x, int16_t y, lv_color_t color)
{
    weather_create_cloud_icon(parent, x, y, UI_COLOR_TEXT_DIM); // 先绘制较暗的雨云底层。
    weather_create_cloud_icon(parent, (int16_t)(x + 3), (int16_t)(y - 2), color); // 叠加较亮前层云朵，形成厚重云层。
    weather_create_round_shape(parent, x + 3, y + 32, 3, 11, 1, UI_COLOR_KL); // 绘制第一条长雨线。
    weather_create_round_shape(parent, x + 11, y + 32, 3, 11, 1, UI_COLOR_KL); // 绘制第二条长雨线。
    weather_create_round_shape(parent, x + 19, y + 32, 3, 11, 1, UI_COLOR_KL); // 绘制第三条长雨线。
    weather_create_round_shape(parent, x + 27, y + 32, 3, 11, 1, UI_COLOR_KL); // 绘制第四条长雨线。
    weather_create_round_shape(parent, x + 35, y + 32, 3, 11, 1, UI_COLOR_KL); // 绘制第五条长雨线。
}

/* 根据天气类型选择对应的图标绘制函数。 */
static void weather_create_condition_icon(lv_obj_t *parent,
                                          int16_t x,
                                          int16_t y,
                                          WeatherIcon_t icon,
                                          lv_color_t color)
{
    if (icon == WEATHER_ICON_PARTLY_CLOUDY) // 判断是否为少云或多云。
    {
        weather_create_partly_cloudy_icon(parent, x, y, color); // 绘制太阳和云朵组合图标。
        return; // 结束图标选择。
    }

    if (icon == WEATHER_ICON_OVERCAST) // 判断是否为阴天或雾天。
    {
        weather_create_overcast_icon(parent, x, y, color); // 绘制双层阴云图标。
        return; // 结束图标选择。
    }

    if (icon == WEATHER_ICON_LIGHT_RAIN) // 判断是否为小雨。
    {
        weather_create_light_rain_icon(parent, x, y, color); // 绘制小雨图标。
        return; // 结束图标选择。
    }

    if (icon == WEATHER_ICON_MODERATE_RAIN) // 判断是否为中雨。
    {
        weather_create_moderate_rain_icon(parent, x, y, color); // 绘制中雨图标。
        return; // 结束图标选择。
    }

    if (icon == WEATHER_ICON_HEAVY_RAIN) // 判断是否为暴雨或雷暴。
    {
        weather_create_heavy_rain_icon(parent, x, y, color); // 绘制暴雨图标。
        return; // 结束图标选择。
    }

    weather_create_sun_icon(parent, x, y); // 默认绘制晴天太阳。
}

/* 将 Open-Meteo 的 WMO 天气代码映射到页面的图标类型。 */
static WeatherIcon_t weather_icon_from_code(int16_t weather_code)
{
    if (weather_code == 0) // 0 表示晴朗。
    {
        return WEATHER_ICON_SUNNY; // 使用太阳图标。
    }

    if ((weather_code == 1) || (weather_code == 2)) // 1、2 分别表示少云和多云。
    {
        return WEATHER_ICON_PARTLY_CLOUDY; // 使用太阳与云朵组合图标。
    }

    if ((weather_code == 3) || (weather_code == 45) || (weather_code == 48) ||
        (weather_code == 71) || (weather_code == 73) || (weather_code == 77)) // 阴天、雾和降雪暂使用厚云图标。
    {
        return WEATHER_ICON_OVERCAST; // 使用双层阴云图标。
    }

    if ((weather_code == 51) || (weather_code == 53) || (weather_code == 55) ||
        (weather_code == 56) || (weather_code == 57) || (weather_code == 61) ||
        (weather_code == 66) || (weather_code == 80)) // 毛毛雨、小雨和轻阵雨属于小雨等级。
    {
        return WEATHER_ICON_LIGHT_RAIN; // 使用短雨线图标。
    }

    if ((weather_code == 63) || (weather_code == 67) || (weather_code == 81)) // 中雨、冻雨和中等阵雨属于中雨等级。
    {
        return WEATHER_ICON_MODERATE_RAIN; // 使用中等密度雨线图标。
    }

    return WEATHER_ICON_HEAVY_RAIN; // 大雨、暴雨、强阵雨和雷暴均使用暴雨图标。
}

/* 将 Open-Meteo 的 WMO 天气代码映射为 LCD 可显示的简短中文描述。 */
static const char *weather_condition_from_code(int16_t weather_code)
{
    if (weather_code == 0) // 0 表示晴朗。
    {
        return WEATHER_SUNNY; // 显示“晴朗”。
    }

    if ((weather_code == 1) || (weather_code == 2)) // 1、2 分别表示少云和多云。
    {
        return WEATHER_CLOUDY; // 显示“多云”。
    }

    if ((weather_code == 3) || (weather_code == 45) || (weather_code == 48) ||
        (weather_code == 71) || (weather_code == 73) || (weather_code == 77)) // 阴天、雾和降雪统一显示阴天。
    {
        return WEATHER_OVERCAST; // 显示“阴天”。
    }

    if ((weather_code == 51) || (weather_code == 53) || (weather_code == 55) ||
        (weather_code == 56) || (weather_code == 57) || (weather_code == 61) ||
        (weather_code == 66) || (weather_code == 80)) // 毛毛雨、小雨和轻阵雨属于小雨等级。
    {
        return WEATHER_LIGHT_RAIN; // 显示“小雨”。
    }

    if ((weather_code == 63) || (weather_code == 67) || (weather_code == 81)) // 中雨、冻雨和中等阵雨属于中雨等级。
    {
        return WEATHER_MODERATE_RAIN; // 显示“中雨”。
    }

    return WEATHER_HEAVY_RAIN; // 大雨、暴雨、强阵雨和雷暴显示“暴雨”。
}

/* 根据最新 WMO 代码刷新当前天气页的描述文字和图标。 */
static void weather_refresh_condition(void)
{
    int16_t weather_code; // 保存后端解析得到的当前天气代码。

    if ((weather_condition_label == NULL) || (weather_icon_parent == NULL))
    {
        return; // 页面不在当前天气视图时不创建或修改图标。
    }

    weather_code = WifiManager_GetWeatherCode(); // 读取刚刚从 HTTP JSON 中解析出的 WMO 代码。
    if ((weather_code < 0) || (weather_code == weather_last_code))
    {
        return; // 尚未得到代码或图标已经是最新状态时不重复重绘。
    }

    lv_label_set_text(weather_condition_label, weather_condition_from_code(weather_code)); // 更新晴、多云或降雨描述。
    lv_obj_clean(weather_icon_parent); // 清除旧图标的全部图形子控件。
    weather_create_condition_icon(weather_icon_parent,
                                  0,
                                  0,
                                  weather_icon_from_code(weather_code),
                                  UI_COLOR_TEXT); // 在固定区域绘制与代码匹配的新图标。
    weather_last_code = weather_code; // 记录已绘制代码，避免半秒定时器重复创建图标。
}

/* 绘制当前天气页右侧的四项天气指标。 */
static lv_obj_t *weather_create_metric(lv_obj_t *parent,
                                       const char *label,
                                       const char *value,
                                       int16_t y)
{
    lv_obj_t *value_label; // 保存指标数值标签指针

    ui_create_text(parent, label, 180, y, &ui_font_10, UI_COLOR_TEXT_DIM); // 创建指标名称
    value_label = ui_create_text(parent, value, 252, y, &ui_font_10, UI_COLOR_TEXT); // 创建指标数值
    lv_obj_align(value_label, LV_ALIGN_TOP_RIGHT, -20, y); // 将数值靠右对齐
    return value_label; // 返回数值标签，供天气刷新函数更新真实数据
}

/* 将浮点摄氏温度四舍五入为 LCD 更适合显示的整数。 */
static int16_t weather_round_temperature(float temperature)
{
    if (temperature >= 0.0f) // 正温度使用加 0.5 的常规四舍五入方式
    {
        return (int16_t)(temperature + 0.5f); // 返回四舍五入后的正整数温度
    }

    return (int16_t)(temperature - 0.5f); // 负温度向远离零方向四舍五入
}

/* 将 WiFi 管理器的实况指标同步到当前天气页右侧的四行数值标签。 */
static void weather_refresh_metric_labels(void)
{
    char feel_text[12]; // 保存体感温度显示文本
    char humidity_text[8]; // 保存湿度显示文本
    char wind_text[12]; // 保存风速显示文本
    char air_text[10]; // 保存空气质量显示文本
    int16_t wind_tenths; // 保存放大十倍后的风速，避免 printf 浮点格式化
    int16_t aqi; // 保存当前 US AQI 数值

    if ((weather_feel_label == NULL) || (weather_humidity_label == NULL) ||
        (weather_wind_label == NULL) || (weather_air_label == NULL)) // 预报页没有这些标签，不执行更新
    {
        return; // 当前不是实况页面时直接退出
    }

    (void)snprintf(feel_text, sizeof(feel_text), "%d C", (int)weather_round_temperature(WifiManager_GetWeatherApparentTemperature())); // 生成体感温度文本
    (void)snprintf(humidity_text, sizeof(humidity_text), "%u%%", (unsigned int)WifiManager_GetWeatherHumidity()); // 生成相对湿度文本
    wind_tenths = (int16_t)(WifiManager_GetWeatherWindSpeed() * 10.0f + 0.5f); // 将风速保留到一位小数
    (void)snprintf(wind_text, sizeof(wind_text), "%d.%d m/s", (int)(wind_tenths / 10), (int)(wind_tenths % 10)); // 生成一位小数的风速文本
    aqi = WifiManager_GetWeatherAqi(); // 读取空气质量接口返回的 US AQI
    if (aqi >= 0) // AQI 非负说明空气质量接口已经返回有效数据
    {
        (void)snprintf(air_text, sizeof(air_text), "AQI %d", (int)aqi); // 显示真实 AQI 数值，不再使用演示文字
    }
    else
    {
        (void)snprintf(air_text, sizeof(air_text), "%s", WEATHER_NO_TIME); // 空气质量尚未返回时显示占位符
    }

    lv_label_set_text(weather_feel_label, feel_text); // 更新体感温度数值
    lv_label_set_text(weather_humidity_label, humidity_text); // 更新湿度数值
    lv_label_set_text(weather_wind_label, wind_text); // 更新风速数值
    lv_label_set_text(weather_air_label, air_text); // 更新空气质量数值
}

/* 将 WiFi 管理器的实时天气状态同步到当前天气页的动态标签。 */
static void weather_refresh_realtime_labels(void)
{
    char temperature_text[12]; // 保存适合 LCD 显示的温度字符串。
    uint32_t now; // 保存当前 LVGL 时钟，用于判断短暂提示是否到期

    if ((weather_temperature_label == NULL) ||
        (weather_time_label == NULL) ||
        (weather_status_label == NULL))
    {
        return; // 预报页或页面已销毁时没有动态标签可更新。
    }

    if (WifiManager_IsWeatherReady() != 0U)
    {
        (void)snprintf(temperature_text, sizeof(temperature_text), "%d C", (int)weather_round_temperature(WifiManager_GetWeatherTemperature())); // 生成温度文本，例如“26 C”。
        lv_label_set_text(weather_temperature_label, temperature_text); // 显示接口返回的实时温度。
        lv_label_set_text(weather_time_label,
                          (WifiManager_GetWeatherTime()[0] != '\0') ?
                          WifiManager_GetWeatherTime() : WEATHER_NO_TIME); // 显示接口的 current.time，缺失时显示占位符。
        weather_refresh_metric_labels(); // 同步体感、湿度、风速和 AQI 的真实数据
        if (weather_update_notice_shown == 0U) // 仅在本次请求首次成功时显示“已更新”
        {
            lv_label_set_text(weather_status_label, WEATHER_UPDATED); // 告知用户本次数据已经成功更新
            lv_obj_set_style_text_color(weather_status_label, UI_COLOR_OK, LV_PART_MAIN); // 成功提示使用绿色
            weather_status_expire_tick = lv_tick_get() + WEATHER_STATUS_SHOW_MS; // 记录 2.5 秒后的自动隐藏时刻
            weather_update_notice_shown = 1U; // 防止半秒刷新定时器反复延长提示时间
        }
        else
        {
            now = lv_tick_get(); // 读取当前 LVGL 时钟
            if ((int32_t)(now - weather_status_expire_tick) >= 0) // 使用有符号差值，兼容毫秒计数回绕
            {
                lv_label_set_text(weather_status_label, ""); // 到期后清空阶段性成功提示，不占用界面空间
            }
        }
        weather_refresh_condition(); // 使用真实 weather_code 更新天气文字和图标。
        return; // 已显示成功数据，无需继续检查其他状态。
    }

    if (WifiManager_IsWeatherBusy() != 0U)
    {
        lv_label_set_text(weather_temperature_label, "-- C"); // 网络请求期间不展示旧温度。
        lv_label_set_text(weather_time_label, WEATHER_FETCHING); // 时间位置用于反馈当前正在读取接口。
        lv_label_set_text(weather_status_label, WEATHER_FETCHING); // 状态标签显示加载中。
        lv_obj_set_style_text_color(weather_status_label, UI_COLOR_LIGHT_BLUE, LV_PART_MAIN); // 加载状态沿用界面主题蓝。
        if (weather_condition_label != NULL) // 天气描述标签仅在得到真实 WMO 代码后才显示内容
        {
            lv_label_set_text(weather_condition_label, ""); // 删除“新乡”下方多余的“获取中”文字
        }
        weather_update_notice_shown = 0U; // 新请求开始后允许下一次成功结果再次显示短暂提示
        weather_status_expire_tick = 0U; // 清除上一次成功提示的到期时间
        return; // 请求尚未结束，等待下一次定时器刷新。
    }

    if (WifiManager_IsWeatherFailed() != 0U)
    {
        lv_label_set_text(weather_temperature_label, "-- C"); // 失败时不显示伪造的演示温度。
        lv_label_set_text(weather_time_label, WEATHER_NO_TIME); // 没有可信接口时间时显示占位符。
        lv_label_set_text(weather_status_label, WEATHER_REQUEST_FAILED); // 明确提示这是请求失败而不是无数据。
        lv_obj_set_style_text_color(weather_status_label, UI_COLOR_RED, LV_PART_MAIN); // 失败状态使用主题警告红色。
        if (weather_condition_label != NULL) // 请求失败时也不保留上一轮的天气描述
        {
            lv_label_set_text(weather_condition_label, ""); // 让“新乡”与温度之间保持干净
        }
    }
}

/* LVGL 定时器回调：页面可见期间每半秒同步一次后台结果。 */
static void weather_refresh_timer_cb(lv_timer_t *timer)
{
    (void)timer; // 本页面不需要通过定时器用户数据区分多个实例。
    if ((weather_view == WEATHER_VIEW_FORECAST) &&
        (WifiManager_IsWeatherReady() != 0U) &&
        (weather_forecast_data_rendered == 0U)) // 用户在请求完成前进入预报页时，需要自动刷新一次卡片
    {
        weather_rebuild(); // 使用刚刚到达的真实三日数据重建预报页
        return; // 本轮已经删除并重建控件，不能继续访问旧标签
    }
    weather_refresh_realtime_labels(); // 只更新已有标签，不重新创建整页控件。
}

/* 创建当前天气视图。 */
static void weather_create_current_view(lv_obj_t *parent)
{
    lv_obj_t *panel; // 保存当前天气面板对象

    ui_create_page_header(parent, WEATHER_TITLE, WEATHER_SUBTITLE_CURRENT); // 创建统一页面标题
    weather_create_divider(parent, WEATHER_HEADER_LINE_Y, UI_COLOR_KL); // 绘制标题下方分割线

    panel = ui_create_shape(parent,
                            WEATHER_CURRENT_PANEL_X,
                            WEATHER_CURRENT_PANEL_Y,
                            WEATHER_CURRENT_PANEL_W,
                            WEATHER_CURRENT_PANEL_H,
                            UI_COLOR_APP_BG); // 创建当前天气主体背景
    lv_obj_set_style_radius(panel, 2, LV_PART_MAIN); // 设置轻微圆角

    ui_create_text(parent, WEATHER_CITY, 28, 58, &ui_font_14, UI_COLOR_TEXT); // 显示当前请求使用的演示坐标城市。
    weather_time_label = ui_create_text(parent, WEATHER_NO_TIME, 216, 61, &ui_font_10, UI_COLOR_TEXT_DIM); // 保存接口时间标签供定时器更新。
    weather_condition_label = ui_create_text(parent, "", 28, 84, &ui_font_12, UI_COLOR_LIGHT_BLUE); // 请求完成后才显示天气描述，避免城市下方出现“获取中”。
    weather_status_label = ui_create_text(parent, WEATHER_FETCHING, 28, 132, &ui_font_10, UI_COLOR_LIGHT_BLUE); // 显示天气请求状态。
    weather_temperature_label = ui_create_text(parent, "-- C", 28, 104, &ui_font_14, UI_COLOR_TEXT); // 保存实时温度标签供后端结果更新。
    weather_icon_parent = lv_obj_create(parent); // 创建固定大小的图标容器，便于后续切换天气图标。
    lv_obj_set_size(weather_icon_parent, 48, 43); // 预留双层云和多云图标的完整横向宽度。
    lv_obj_set_pos(weather_icon_parent, 107, 83); // 将图标容器放在原有天气图标位置。
    lv_obj_set_style_bg_opa(weather_icon_parent, LV_OPA_TRANSP, LV_PART_MAIN); // 图标容器自身完全透明。
    lv_obj_set_style_border_width(weather_icon_parent, 0, LV_PART_MAIN); // 图标容器不显示边框。
    lv_obj_set_style_pad_all(weather_icon_parent, 0, LV_PART_MAIN); // 子图形坐标直接从容器左上角开始。
    lv_obj_remove_flag(weather_icon_parent, LV_OBJ_FLAG_SCROLLABLE); // 图标容器不参与滚动。
    weather_create_sun_icon(weather_icon_parent, 0, 0); // 请求完成前保留默认太阳图形作为占位。

    weather_feel_label = weather_create_metric(parent, WEATHER_LABEL_FEEL, WEATHER_NO_TIME, 83); // 创建体感温度数值标签
    weather_humidity_label = weather_create_metric(parent, WEATHER_LABEL_HUMIDITY, WEATHER_NO_TIME, 106); // 创建空气湿度数值标签
    weather_wind_label = weather_create_metric(parent, WEATHER_LABEL_WIND, WEATHER_NO_TIME, 129); // 创建风速数值标签
    weather_air_label = weather_create_metric(parent, WEATHER_LABEL_AIR, WEATHER_NO_TIME, 152); // 创建空气质量数值标签

    weather_create_footer(parent, WEATHER_HINT_CURRENT); // 显示当前天气页操作提示
}

/* 创建单个预报日期卡片。 */
static void weather_create_forecast_card(lv_obj_t *parent,
                                         int16_t x,
                                         const char *day,
                                         const char *date,
                                         const char *condition,
                                         const char *temperature,
                                         WeatherIcon_t icon,
                                         uint8_t selected)
{
    lv_obj_t *card; // 保存日期卡片对象
    lv_color_t text_color; // 保存当前卡片文字颜色
    lv_color_t dim_color; // 保存当前卡片次要文字颜色

    if (selected != 0U) // 判断当前卡片是否被选中
    {
        card = ui_create_shape(parent, x, WEATHER_FORECAST_TOP_Y, WEATHER_FORECAST_CARD_W, WEATHER_FORECAST_CARD_H, UI_COLOR_KL); // 创建克莱因蓝选中卡片
        text_color = UI_COLOR_TEXT; // 选中卡片使用白色主文字
        dim_color = UI_COLOR_TEXT; // 选中卡片的次要文字同样使用白色
    }
    else
    {
        card = ui_create_outline(parent, x, WEATHER_FORECAST_TOP_Y, WEATHER_FORECAST_CARD_W, WEATHER_FORECAST_CARD_H, UI_COLOR_BORDER); // 创建未选中的暗色轮廓
        text_color = UI_COLOR_LIGHT_BLUE; // 未选中卡片使用浅蓝主文字
        dim_color = UI_COLOR_TEXT_DIM; // 未选中卡片使用灰蓝次要文字
    }

    lv_obj_set_style_radius(card, 2, LV_PART_MAIN); // 设置卡片圆角
    ui_create_text(parent, day, (int16_t)(x + 12), 58, &ui_font_12, text_color); // 显示日期名称
    weather_create_condition_icon(parent, (int16_t)(x + 24), 78, icon, text_color); // 显示对应天气图标
    ui_create_text(parent, condition, (int16_t)(x + 12), 119, &ui_font_10, text_color); // 显示天气状况
    ui_create_text(parent, temperature, (int16_t)(x + 12), 140, &ui_font_12, text_color); // 显示当天温度
    ui_create_text(parent, date, (int16_t)(x + 12), 168, &ui_font_10, dim_color); // 显示接口返回的月日，避免误把当前 AQI 当作未来预报
}

/* 将某一天的最高、最低温度格式化为紧凑的“最低-最高 C”文本。 */
static void weather_format_forecast_temperature(char *text, uint16_t size, const WifiWeatherForecast_t *forecast)
{
    if ((text == NULL) || (size == 0U)) // 输出缓冲区不存在时不能生成显示文本
    {
        return; // 防止空指针或零长度缓冲区写入
    }

    if (forecast == NULL) // 后端尚未完成时不存在可用预报记录
    {
        (void)snprintf(text, size, "-- C"); // 使用明确的占位文本代替演示数据
        return; // 不再访问空的预报记录
    }

    (void)snprintf(text, size, "%d-%d C", (int)weather_round_temperature(forecast->temperature_min), (int)weather_round_temperature(forecast->temperature_max)); // 生成一天的最低与最高温度范围
}

/* 返回预报日期中用于卡片显示的“月-日”部分。 */
static const char *weather_forecast_short_date(const WifiWeatherForecast_t *forecast)
{
    if ((forecast == NULL) || (forecast->date[7] == '\0')) // 日期缺失或格式长度不足时不访问固定偏移
    {
        return WEATHER_NO_TIME; // 使用统一占位文本
    }

    return &forecast->date[5]; // ISO 日期“YYYY-MM-DD”从第六个字符开始正好是“MM-DD”
}

/* 创建三日天气预报视图。 */
static void weather_create_forecast_view(lv_obj_t *parent)
{
    int16_t second_x; // 保存第二张卡片的横坐标
    int16_t third_x; // 保存第三张卡片的横坐标
    const WifiWeatherForecast_t *today; // 保存今天的真实预报记录
    const WifiWeatherForecast_t *tomorrow; // 保存明天的真实预报记录
    const WifiWeatherForecast_t *after_tomorrow; // 保存后天的真实预报记录
    char today_temperature[14]; // 保存今天的最低与最高温文本
    char tomorrow_temperature[14]; // 保存明天的最低与最高温文本
    char after_tomorrow_temperature[14]; // 保存后天的最低与最高温文本

    ui_create_page_header(parent, WEATHER_TITLE, WEATHER_SUBTITLE_FORECAST); // 创建统一页面标题
    weather_create_divider(parent, WEATHER_HEADER_LINE_Y, UI_COLOR_KL); // 绘制标题下方分割线

    second_x = 16 + WEATHER_FORECAST_CARD_W + WEATHER_FORECAST_GAP; // 计算第二张卡片位置
    third_x = second_x + WEATHER_FORECAST_CARD_W + WEATHER_FORECAST_GAP; // 计算第三张卡片位置
    today = WifiManager_GetWeatherForecast(0U); // 读取今天的预报，未完成时会返回空指针
    tomorrow = WifiManager_GetWeatherForecast(1U); // 读取明天的预报，未完成时会返回空指针
    after_tomorrow = WifiManager_GetWeatherForecast(2U); // 读取后天的预报，未完成时会返回空指针
    weather_format_forecast_temperature(today_temperature, sizeof(today_temperature), today); // 格式化今天温度范围
    weather_format_forecast_temperature(tomorrow_temperature, sizeof(tomorrow_temperature), tomorrow); // 格式化明天温度范围
    weather_format_forecast_temperature(after_tomorrow_temperature, sizeof(after_tomorrow_temperature), after_tomorrow); // 格式化后天温度范围

    weather_create_forecast_card(parent, 16, WEATHER_TODAY, weather_forecast_short_date(today), (today != NULL) ? weather_condition_from_code(today->weather_code) : WEATHER_FETCHING, today_temperature, (today != NULL) ? weather_icon_from_code(today->weather_code) : WEATHER_ICON_OVERCAST, weather_forecast_selected == 0U); // 创建今天真实预报卡片
    weather_create_forecast_card(parent, second_x, WEATHER_TOMORROW, weather_forecast_short_date(tomorrow), (tomorrow != NULL) ? weather_condition_from_code(tomorrow->weather_code) : WEATHER_FETCHING, tomorrow_temperature, (tomorrow != NULL) ? weather_icon_from_code(tomorrow->weather_code) : WEATHER_ICON_OVERCAST, weather_forecast_selected == 1U); // 创建明天真实预报卡片
    weather_create_forecast_card(parent, third_x, WEATHER_AFTER_TOMORROW, weather_forecast_short_date(after_tomorrow), (after_tomorrow != NULL) ? weather_condition_from_code(after_tomorrow->weather_code) : WEATHER_FETCHING, after_tomorrow_temperature, (after_tomorrow != NULL) ? weather_icon_from_code(after_tomorrow->weather_code) : WEATHER_ICON_OVERCAST, weather_forecast_selected == 2U); // 创建后天真实预报卡片
    weather_forecast_data_rendered = (WifiManager_IsWeatherReady() != 0U) ? 1U : 0U; // 记录本次视图是否已经使用真实数据绘制

    weather_create_footer(parent, WEATHER_HINT_FORECAST); // 显示预报页操作提示
}

/* 根据当前视图状态重新绘制天气页面。 */
static void weather_rebuild(void)
{
    if (weather_parent == 0) // 判断天气页面父容器是否有效
    {
        return; // 父容器无效时不能创建页面控件
    }

    weather_temperature_label = NULL; // 先清除动态标签指针，避免定时器访问即将删除的对象。
    weather_time_label = NULL; // 清除时间标签指针。
    weather_status_label = NULL; // 清除状态标签指针。
    weather_condition_label = NULL; // 清除天气描述标签指针。
    weather_icon_parent = NULL; // 清除天气图标容器指针。
    weather_feel_label = NULL; // 清除体感温度标签指针。
    weather_humidity_label = NULL; // 清除湿度标签指针。
    weather_wind_label = NULL; // 清除风速标签指针。
    weather_air_label = NULL; // 清除空气质量标签指针。
    weather_last_code = -1; // 让返回当前页后使用最新代码重绘图标。
    lv_obj_clean(weather_parent); // 删除旧视图中的控件

    if (weather_view == WEATHER_VIEW_FORECAST) // 判断当前是否显示三日预报
    {
        weather_create_forecast_view(weather_parent); // 重建三日预报视图
    }
    else
    {
        weather_create_current_view(weather_parent); // 重建当前天气视图
    }
}

/* 创建天气应用页面入口。 */
void ui_page_weather_create(lv_obj_t *parent)
{
    lv_group_t *group; // 保存输入设备分组指针

    weather_parent = parent; // 保存页面父容器供按键切换时重绘
    weather_view = WEATHER_VIEW_CURRENT; // 每次从桌面进入时默认显示当前天气
    weather_forecast_selected = 0U; // 默认选中今天的预报
    weather_temperature_label = NULL; // 初始化动态温度标签指针。
    weather_time_label = NULL; // 初始化动态时间标签指针。
    weather_status_label = NULL; // 初始化动态状态标签指针。
    weather_condition_label = NULL; // 初始化动态天气描述标签指针。
    weather_icon_parent = NULL; // 初始化动态图标容器指针。
    weather_feel_label = NULL; // 初始化动态体感温度标签指针。
    weather_humidity_label = NULL; // 初始化动态湿度标签指针。
    weather_wind_label = NULL; // 初始化动态风速标签指针。
    weather_air_label = NULL; // 初始化动态空气质量标签指针。
    weather_last_code = -1; // 尚未绘制任何来自接口的天气代码。
    weather_update_notice_shown = 0U; // 新页面首次成功时需要显示“已更新”。
    weather_forecast_data_rendered = 0U; // 尚未用本次网络数据绘制三日预报。
    weather_status_expire_tick = 0U; // 清除可能来自上次进入页面的提示计时。

    group = lv_port_indev_get_group(); // 取得按键输入分组
    if (group != NULL) // 判断输入分组是否已经初始化
    {
        lv_group_remove_all_objs(group); // 天气页面没有可聚焦按钮，清空旧页面对象
    }

    weather_create_current_view(parent); // 首次创建当前天气视图
    WifiManager_RequestWeather(); // 进入天气应用时立即请求一次真实当前温度。
    weather_refresh_realtime_labels(); // 先显示“获取中”，避免等待定时器首周期。
    weather_refresh_timer = lv_timer_create(weather_refresh_timer_cb, 500U, NULL); // 半秒刷新一次后端状态。
}

/* 离开天气页面时删除定时器，防止页面控件销毁后仍被回调访问。 */
void ui_page_weather_leave(void)
{
    if (weather_refresh_timer != NULL) // 判断当前页面是否创建过定时器。
    {
        lv_timer_del(weather_refresh_timer); // 删除本页面专用的 LVGL 定时器。
        weather_refresh_timer = NULL; // 清空指针，避免重复删除。
    }

    weather_temperature_label = NULL; // 页面离开后不再持有已销毁控件指针。
    weather_time_label = NULL; // 清除时间标签指针。
    weather_status_label = NULL; // 清除状态标签指针。
    weather_condition_label = NULL; // 清除天气描述标签指针。
    weather_icon_parent = NULL; // 清除天气图标容器指针。
    weather_feel_label = NULL; // 清除体感温度标签指针。
    weather_humidity_label = NULL; // 清除湿度标签指针。
    weather_wind_label = NULL; // 清除风速标签指针。
    weather_air_label = NULL; // 清除空气质量标签指针。
    weather_last_code = -1; // 复位图标代码缓存。
    weather_update_notice_shown = 0U; // 离开页面后不保留成功提示状态。
    weather_forecast_data_rendered = 0U; // 下次进入页面需要等待新的真实预报数据。
    weather_status_expire_tick = 0U; // 清除提示到期时间。
}

/* 处理天气应用页面按键。 */
void ui_page_weather_on_key(BSP_Key_t key)
{
    if (key == BSP_KEY_ESC) // 判断是否按下返回键
    {
        ui_nav_pop(); // 返回桌面
        return; // 结束本次按键处理
    }

    if (key == BSP_KEY_ENTER) // 判断是否按下确认键
    {
        if (weather_view == WEATHER_VIEW_CURRENT) // 当前显示当前天气页
        {
            weather_view = WEATHER_VIEW_FORECAST; // 切换到三日预报页
        }
        else
        {
            weather_view = WEATHER_VIEW_CURRENT; // 从三日预报页切回当前天气页
        }

        weather_rebuild(); // 根据新的视图状态重新绘制页面
        return; // 结束本次按键处理
    }

    if ((weather_view == WEATHER_VIEW_CURRENT) && (key == BSP_KEY_UP)) // 当前天气页用上键手动刷新。
    {
        WifiManager_RequestWeather(); // 重新发起一次 TCP/HTTP 请求。
        weather_forecast_data_rendered = 0U; // 新请求完成后允许预报页按新数据重绘。
        weather_refresh_realtime_labels(); // 立即把界面切换为获取中状态。
        return; // 完成本次按键处理。
    }

    if (weather_view != WEATHER_VIEW_FORECAST) // 当前天气页不需要日期选择
    {
        return; // 左右键在当前天气页暂不执行网络操作
    }

    if (key == BSP_KEY_LEFT) // 判断是否向左选择日期
    {
        if (weather_forecast_selected > 0U) // 防止选中下标小于零
        {
            weather_forecast_selected--; // 移动到前一天
            weather_rebuild(); // 刷新选中卡片的颜色
        }
        return; // 结束本次按键处理
    }

    if (key == BSP_KEY_RIGHT) // 判断是否向右选择日期
    {
        if (weather_forecast_selected < 2U) // 防止选中下标超过三日预报范围
        {
            weather_forecast_selected++; // 移动到后一天
            weather_rebuild(); // 刷新选中卡片的颜色
        }
    }
}
