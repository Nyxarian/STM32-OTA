#include "wifi_manager.h"

#include "wifi_config.h"
#include "wifi_uart.h"
#include "main.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* AT 状态机的时间和缓存限制。 */
#define WIFI_AT_TIMEOUT_MS       1500U
#define WIFI_STARTUP_DELAY_MS    2000U
#define WIFI_SCAN_TIMEOUT_MS     30000U
#define WIFI_JOIN_TIMEOUT_MS     20000U
#define WIFI_REFRESH_PERIOD_MS   3000U
#define WIFI_LINE_BUFFER_SIZE    160U
#define WIFI_RX_BYTES_PER_CYCLE  96U
#define WIFI_SCAN_MAX_COUNT      8U
#define WIFI_HTTP_TIMEOUT_MS     20000U
#define WIFI_HTTP_BUFFER_SIZE    2048U
#define WIFI_UPGRADE_BUFFER_SIZE 512U

/* Open-Meteo 明文 HTTP 请求，长度必须与 AT+CIPSEND 后面的数字一致。 */
static const uint8_t wifi_weather_http_request[] =
    "GET /v1/forecast?latitude=35.30&longitude=113.93&current=temperature_2m,apparent_temperature,relative_humidity_2m,wind_speed_10m,weather_code&daily=weather_code,temperature_2m_max,temperature_2m_min&forecast_days=3&timezone=Asia%2FShanghai HTTP/1.1\r\n"
    "Host: api.open-meteo.com\r\n"
    "Connection: close\r\n"
    "\r\n";

/* 空气质量接口单独请求，避免单个 JSON 过大且便于分别处理失败。 */
static const uint8_t wifi_air_quality_http_request[] =
    "GET /v1/air-quality?latitude=35.30&longitude=113.93&current=us_aqi,pm2_5,pm10&timezone=Asia%2FShanghai HTTP/1.1\r\n"
    "Host: air-quality-api.open-meteo.com\r\n"
    "Connection: close\r\n"
    "\r\n";

static const uint8_t wifi_upgrade_manifest_request[] =
    "GET " WIFI_CONFIG_OTA_MANIFEST_PATH " HTTP/1.0\r\n"
    "Host: " WIFI_CONFIG_OTA_SERVER_HOST "\r\n"
    "Connection: close\r\n"
    "\r\n";

static const uint8_t wifi_upgrade_start_request[] =
    "POST " WIFI_CONFIG_OTA_UPGRADE_PATH " HTTP/1.0\r\n"
    "Host: " WIFI_CONFIG_OTA_SERVER_HOST "\r\n"
    "Content-Length: 0\r\n"
    "Connection: close\r\n"
    "\r\n";

/* 状态机内部使用的 AT 命令编号。 */
typedef enum
{
    WIFI_COMMAND_AT = 0,
    WIFI_COMMAND_MODE,
    WIFI_COMMAND_SERVER_OFF,
    WIFI_COMMAND_NORMAL_MODE,
    WIFI_COMMAND_ACTIVE_RECEIVE,
    WIFI_COMMAND_SINGLE_CONNECTION,
    WIFI_COMMAND_JOIN,
    WIFI_COMMAND_STATUS,
    WIFI_COMMAND_AP,
    WIFI_COMMAND_IP,
    WIFI_COMMAND_SCAN,
    WIFI_COMMAND_HTTP_START,
    WIFI_COMMAND_HTTP_LENGTH,
    WIFI_COMMAND_HTTP_WAIT,
    WIFI_COMMAND_UPGRADE_START,
    WIFI_COMMAND_UPGRADE_LENGTH,
    WIFI_COMMAND_UPGRADE_WAIT,
    WIFI_COMMAND_UPGRADE_TRIGGER_START,
    WIFI_COMMAND_UPGRADE_TRIGGER_LENGTH,
    WIFI_COMMAND_UPGRADE_TRIGGER_WAIT,
    WIFI_COMMAND_CLOSE
} WifiCommand_t;

/* 一次完整天气刷新由天气预报和空气质量两段 HTTP 事务组成。 */
typedef enum
{
    WIFI_WEATHER_HTTP_FORECAST = 0U, /* 当前实况和未来三天预报。 */
    WIFI_WEATHER_HTTP_AIR_QUALITY /* US AQI、PM2.5 和 PM10。 */
} WifiWeatherHttpStage_t;

static WifiState_t wifi_state;                         /* 当前联网状态。 */
static WifiCommand_t wifi_command;                     /* 当前或下一条 AT 命令。 */
static uint8_t wifi_command_pending;                   /* 是否正在等待当前命令的最终响应。 */
static uint8_t wifi_scan_requested;                    /* 是否有等待发送的扫描请求。 */
static uint8_t wifi_scan_busy;                         /* 是否正在排队或执行扫描。 */
static uint8_t wifi_scan_failed;                       /* 最近一次扫描是否失败。 */
static uint32_t wifi_command_deadline;                 /* 当前命令的超时时刻。 */
static uint32_t wifi_next_command_tick;                /* 下一次允许发送命令的时刻。 */
static char wifi_line[WIFI_LINE_BUFFER_SIZE];           /* 当前接收行缓存。 */
static uint8_t wifi_line_length;                        /* 当前接收行长度。 */
static char wifi_ssid[33];                              /* 当前已连接网络名称。 */
static char wifi_ip[16];                                /* 当前 IPv4 地址。 */
static char wifi_join_ssid[33];                         /* 待连接网络名称。 */
static char wifi_join_password[65];                     /* 待连接网络密码。 */
static int16_t wifi_rssi;                               /* 当前信号强度，单位 dBm。 */
static uint8_t wifi_auto_join_attempted;                /* 本次上电是否已经尝试过预存网络，防止失败后循环连接。 */
static WifiScanResult_t wifi_scan_results[WIFI_SCAN_MAX_COUNT]; /* 扫描结果缓存。 */
static uint8_t wifi_scan_count;                         /* 扫描结果数量。 */
static uint8_t wifi_at_test_mode;                        /* AT 测试是否独占 USART1。 */
static uint8_t wifi_weather_requested;                   /* 是否有等待启动的天气请求。 */
static uint8_t wifi_weather_busy;                        /* 天气请求是否正在执行。 */
static uint8_t wifi_weather_ready;                       /* 是否已经得到有效天气数据。 */
static uint8_t wifi_weather_failed;                      /* 最近一次天气请求是否失败。 */
static uint8_t wifi_weather_payload_sent;                /* HTTP 正文是否已经发出。 */
static uint8_t wifi_weather_response_seen;               /* 是否收到 +IPD 数据。 */
static char wifi_weather_buffer[WIFI_HTTP_BUFFER_SIZE];  /* 保存少量 HTTP 响应文本。 */
static uint16_t wifi_weather_length;                     /* 响应缓存当前长度。 */
static float wifi_weather_temperature;                   /* 解析得到的摄氏温度。 */
static int16_t wifi_weather_code;                         /* 解析得到的 WMO 天气代码。 */
static float wifi_weather_apparent_temperature;           /* 解析得到的体感温度。 */
static uint8_t wifi_weather_humidity;                     /* 解析得到的相对湿度。 */
static float wifi_weather_wind_speed;                     /* 解析得到的十米风速。 */
static int16_t wifi_weather_aqi;                          /* 解析得到的美国空气质量指数。 */
static WifiWeatherForecast_t wifi_weather_forecasts[WIFI_WEATHER_FORECAST_DAYS]; /* 三天预报缓存。 */
static WifiWeatherHttpStage_t wifi_weather_http_stage;    /* 当前正在执行的 HTTP 子事务。 */
static uint8_t wifi_weather_current_valid;                /* 当前实况字段是否完整。 */
static uint8_t wifi_weather_forecast_valid;               /* 三天预报字段是否完整。 */
static uint8_t wifi_weather_air_valid;                    /* 空气质量字段是否完整。 */
static char wifi_weather_time[24];                       /* 解析得到的接口时间。 */

/* 将指定长度的字段复制为安全的 C 字符串。 */
static uint8_t wifi_upgrade_requested;
static uint8_t wifi_upgrade_busy;
static uint8_t wifi_upgrade_ready;
static uint8_t wifi_upgrade_failed;
static uint8_t wifi_upgrade_payload_sent;
static uint8_t wifi_upgrade_response_seen;
static uint8_t wifi_upgrade_response_overflow;
static char wifi_upgrade_buffer[WIFI_UPGRADE_BUFFER_SIZE];
static uint16_t wifi_upgrade_length;
static char wifi_upgrade_ipd_header[16];
static uint8_t wifi_upgrade_ipd_header_length;
static uint16_t wifi_upgrade_ipd_payload_remaining;
static WifiUpgradeManifest_t wifi_upgrade_manifest;
static uint8_t wifi_upgrade_start_requested;
static uint8_t wifi_upgrade_start_busy;
static uint8_t wifi_upgrade_start_ready;
static uint8_t wifi_upgrade_start_failed;
static uint8_t wifi_upgrade_start_payload_sent;
static uint8_t wifi_upgrade_start_response_accepted;
static WifiUpgradeFailReason_t wifi_upgrade_fail_reason;
static uint8_t wifi_init_fail_count;

static void wifi_copy_field(char *destination,
                            uint16_t destination_size,
                            const char *source,
                            uint16_t source_length)
{
    uint16_t length = source_length;                    /* 默认使用调用方提供的长度。 */

    if ((destination == NULL) || (source == NULL) || (destination_size == 0U))
    {
        return;                                         /* 参数无效时不访问任何地址。 */
    }

    if (length >= destination_size)
    {
        length = (uint16_t)(destination_size - 1U);     /* 为结束符留出一个字节。 */
    }

    if (length > 0U)
    {
        memcpy(destination, source, length);             /* 复制字段正文。 */
    }

    destination[length] = '\0';                         /* 强制补字符串结束符。 */
}

/* 清空本次 HTTP 响应的临时内容，但保留上一次已经显示的温度数值。 */
static void wifi_weather_clear_response(void)
{
    memset(wifi_weather_buffer, 0, sizeof(wifi_weather_buffer)); // 清空用于查找 JSON 字段的文本缓存。
    wifi_weather_length = 0U; // 将响应长度恢复到起始位置。
    wifi_weather_payload_sent = 0U; // 标记 HTTP 正文尚未发送。
    wifi_weather_response_seen = 0U; // 标记尚未收到服务器数据。
}

/* 在指定 JSON 对象范围内读取一个浮点数值字段。 */
static uint8_t wifi_weather_parse_float_field(const char *object, const char *field, float *result)
{
    const char *key; // 保存找到的 JSON 字段名位置。
    const char *value; // 保存字段值的起始位置。
    char *end_number; // 保存 strtod 结束解析的位置。
    double number; // 暂存标准库解析得到的双精度数值。

    if ((object == NULL) || (field == NULL) || (result == NULL))
    {
        return 0U; // 任一指针无效时不访问字符串。
    }

    key = strstr(object, field); // 在当前对象及其后续数据中查找字段名。
    if (key == NULL)
    {
        return 0U; // 数据尚未完整到达时暂不报告成功。
    }

    value = key + strlen(field); // 跳过字段名和冒号，定位实际数值。
    number = strtod(value, &end_number); // 尝试解析 JSON 中的浮点数。
    if (end_number == value)
    {
        return 0U; // 没有解析到数字时说明该字段还不完整或格式不符。
    }

    *result = (float)number; // 保存转换后的单精度数值。
    return 1U; // 告知调用方该字段已经解析成功。
}

/* 在指定 JSON 对象范围内读取一个整数数值字段。 */
static uint8_t wifi_weather_parse_int_field(const char *object, const char *field, int16_t *result)
{
    const char *key; // 保存找到的 JSON 字段名位置。
    const char *value; // 保存字段值的起始位置。
    char *end_number; // 保存 strtol 结束解析的位置。
    long number; // 暂存标准库解析得到的长整数。

    if ((object == NULL) || (field == NULL) || (result == NULL))
    {
        return 0U; // 任一指针无效时不访问字符串。
    }

    key = strstr(object, field); // 查找字段名。
    if (key == NULL)
    {
        return 0U; // 数据尚未完整到达时暂不报告成功。
    }

    value = key + strlen(field); // 跳过字段名和冒号，定位实际整数。
    number = strtol(value, &end_number, 10); // 解析 JSON 中的整数。
    if (end_number == value)
    {
        return 0U; // 没有解析到数字时说明该字段还不完整或格式不符。
    }

    *result = (int16_t)number; // 保存转换后的 16 位整数。
    return 1U; // 告知调用方该字段已经解析成功。
}

/* 从 JSON 数组字段中读取固定数量的浮点数。 */
static uint8_t wifi_weather_parse_float_array(const char *object,
                                              const char *field,
                                              float *values,
                                              uint8_t count)
{
    const char *key; // 保存数组字段名位置。
    const char *cursor; // 保存当前数组元素解析位置。
    char *end_number; // 保存当前元素解析结束位置。
    uint8_t index; // 保存数组元素索引。
    double number; // 暂存当前元素的双精度数值。

    if ((object == NULL) || (field == NULL) || (values == NULL) || (count == 0U))
    {
        return 0U; // 参数无效时不解析数组。
    }

    key = strstr(object, field); // 查找数组字段名称。
    if (key == NULL)
    {
        return 0U; // 数组字段尚未完整到达。
    }

    cursor = strchr(key, '['); // 定位 JSON 数组左方括号。
    if (cursor == NULL)
    {
        return 0U; // 字段格式不完整时不继续读取。
    }
    cursor++; // 跳过左方括号，进入第一个元素。

    for (index = 0U; index < count; index++)
    {
        while ((*cursor == ' ') || (*cursor == ',') || (*cursor == '\r') || (*cursor == '\n'))
        {
            cursor++; // 跳过元素之间的逗号、空格和换行。
        }

        number = strtod(cursor, &end_number); // 解析当前数组元素。
        if (end_number == cursor)
        {
            return 0U; // 任一元素尚未到达或格式错误时暂不提交数组。
        }

        values[index] = (float)number; // 保存当前元素。
        cursor = end_number; // 将光标移动到下一个元素前。
    }

    return 1U; // 所需数量的元素均已成功读取。
}

/* 从 JSON 数组字段中读取固定数量的整数。 */
static uint8_t wifi_weather_parse_int_array(const char *object,
                                            const char *field,
                                            int16_t *values,
                                            uint8_t count)
{
    const char *key; // 保存数组字段名位置。
    const char *cursor; // 保存当前数组元素解析位置。
    char *end_number; // 保存当前元素解析结束位置。
    uint8_t index; // 保存数组元素索引。
    long number; // 暂存当前元素的长整数数值。

    if ((object == NULL) || (field == NULL) || (values == NULL) || (count == 0U))
    {
        return 0U; // 参数无效时不解析数组。
    }

    key = strstr(object, field); // 查找数组字段名称。
    if (key == NULL)
    {
        return 0U; // 数组字段尚未完整到达。
    }

    cursor = strchr(key, '['); // 定位 JSON 数组左方括号。
    if (cursor == NULL)
    {
        return 0U; // 字段格式不完整时不继续读取。
    }
    cursor++; // 跳过左方括号，进入第一个元素。

    for (index = 0U; index < count; index++)
    {
        while ((*cursor == ' ') || (*cursor == ',') || (*cursor == '\r') || (*cursor == '\n'))
        {
            cursor++; // 跳过元素之间的逗号、空格和换行。
        }

        number = strtol(cursor, &end_number, 10); // 解析当前整数元素。
        if (end_number == cursor)
        {
            return 0U; // 任一元素尚未到达或格式错误时暂不提交数组。
        }

        values[index] = (int16_t)number; // 保存当前元素。
        cursor = end_number; // 将光标移动到下一个元素前。
    }

    return 1U; // 所需数量的元素均已成功读取。
}

/* 从 daily.time 数组中读取三天对应的 ISO 日期。 */
static uint8_t wifi_weather_parse_date_array(const char *object)
{
    const char *key; // 保存 time 数组字段名位置。
    const char *cursor; // 保存当前日期字符串搜索位置。
    const char *end_quote; // 保存当前日期字符串结束引号位置。
    uint8_t index; // 保存日期数组索引。

    if (object == NULL)
    {
        return 0U; // 对象不存在时不能读取日期。
    }

    key = strstr(object, "\"time\":["); // daily 对象中的第一个 time 字段就是日期数组。
    if (key == NULL)
    {
        return 0U; // 日期数组尚未完整到达。
    }

    cursor = key + strlen("\"time\":["); // 定位第一个日期字符串前的位置。
    for (index = 0U; index < WIFI_WEATHER_FORECAST_DAYS; index++)
    {
        cursor = strchr(cursor, '\"'); // 查找当前日期字符串的起始引号。
        if (cursor == NULL)
        {
            return 0U; // 未找到起始引号说明数据仍不完整。
        }
        cursor++; // 跳过起始引号。
        end_quote = strchr(cursor, '\"'); // 查找当前日期字符串结束引号。
        if (end_quote == NULL)
        {
            return 0U; // 未找到结束引号说明数据仍不完整。
        }

        wifi_copy_field(wifi_weather_forecasts[index].date,
                        (uint16_t)sizeof(wifi_weather_forecasts[index].date),
                        cursor,
                        (uint16_t)(end_quote - cursor)); // 有边界地保存 YYYY-MM-DD 日期。
        cursor = end_quote + 1; // 移动到下一日期字符串前。
    }

    return 1U; // 三个日期均已成功解析。
}

/* 解析气象接口中的当前实况和未来三天预报字段。 */
static void wifi_weather_parse_forecast_response(void)
{
    const char *current; // 保存 current JSON 对象位置。
    const char *daily; // 保存 daily JSON 对象位置。
    int16_t humidity; // 暂存相对湿度整数。
    uint8_t current_valid; // 保存当前实况字段是否齐全。
    uint8_t daily_valid; // 保存三天预报字段是否齐全。

    current = strstr(wifi_weather_buffer, "\"current\":{"); // 跳过 current_units，只定位真实当前数据对象。
    if (current != NULL)
    {
        current_valid = wifi_weather_parse_float_field(current, "\"temperature_2m\":", &wifi_weather_temperature); // 读取实时温度。
        current_valid &= wifi_weather_parse_float_field(current, "\"apparent_temperature\":", &wifi_weather_apparent_temperature); // 读取体感温度。
        current_valid &= wifi_weather_parse_int_field(current, "\"relative_humidity_2m\":", &humidity); // 读取相对湿度。
        current_valid &= wifi_weather_parse_float_field(current, "\"wind_speed_10m\":", &wifi_weather_wind_speed); // 读取十米风速。
        current_valid &= wifi_weather_parse_int_field(current, "\"weather_code\":", &wifi_weather_code); // 读取 WMO 天气代码。
        if ((humidity >= 0) && (humidity <= 100))
        {
            wifi_weather_humidity = (uint8_t)humidity; // 将合法湿度保存为百分比。
        }
        else
        {
            current_valid = 0U; // 湿度超出物理范围时不接受整份当前数据。
        }
        if (strstr(current, "\"time\":\"") != NULL)
        {
            const char *time_value = strstr(current, "\"time\":\"") + strlen("\"time\":\""); // 定位 current.time 文本内容。
            const char *end_quote = strchr(time_value, '\"'); // 查找时间字符串结束位置。
            if (end_quote != NULL)
            {
                wifi_copy_field(wifi_weather_time, (uint16_t)sizeof(wifi_weather_time), time_value, (uint16_t)(end_quote - time_value)); // 保存接口时区下的实况时间。
            }
            else
            {
                current_valid = 0U; // 时间字段未接收完整时暂不提交当前数据。
            }
        }
        else
        {
            current_valid = 0U; // 缺少 current.time 时不提交当前数据。
        }
        wifi_weather_current_valid = current_valid; // 保存当前实况完整性。
    }

    daily = strstr(wifi_weather_buffer, "\"daily\":{"); // 定位三天预报对象。
    if (daily != NULL)
    {
        int16_t weather_codes[WIFI_WEATHER_FORECAST_DAYS]; // 暂存三天 WMO 天气代码。
        float temperatures_min[WIFI_WEATHER_FORECAST_DAYS]; // 暂存三天最低温。
        float temperatures_max[WIFI_WEATHER_FORECAST_DAYS]; // 暂存三天最高温。
        uint8_t index; // 保存预报数组写入索引。

        daily_valid = wifi_weather_parse_date_array(daily); // 读取三天日期。
        daily_valid &= wifi_weather_parse_int_array(daily, "\"weather_code\":[", weather_codes, WIFI_WEATHER_FORECAST_DAYS); // 读取三天 WMO 天气代码。
        daily_valid &= wifi_weather_parse_float_array(daily, "\"temperature_2m_min\":[", temperatures_min, WIFI_WEATHER_FORECAST_DAYS); // 读取三天最低温。
        daily_valid &= wifi_weather_parse_float_array(daily, "\"temperature_2m_max\":[", temperatures_max, WIFI_WEATHER_FORECAST_DAYS); // 读取三天最高温。
        if (daily_valid != 0U)
        {
            for (index = 0U; index < WIFI_WEATHER_FORECAST_DAYS; index++)
            {
                wifi_weather_forecasts[index].weather_code = weather_codes[index]; // 保存当天的天气代码。
                wifi_weather_forecasts[index].temperature_min = temperatures_min[index]; // 保存当天最低温。
                wifi_weather_forecasts[index].temperature_max = temperatures_max[index]; // 保存当天最高温。
            }
        }
        wifi_weather_forecast_valid = daily_valid; // 保存三天预报完整性。
    }
}

/* 解析空气质量接口中的当前 US AQI 字段。 */
static void wifi_weather_parse_air_quality_response(void)
{
    const char *current; // 保存空气质量接口 current JSON 对象位置。
    int16_t aqi; // 暂存解析得到的 US AQI 数值。

    current = strstr(wifi_weather_buffer, "\"current\":{"); // 跳过 current_units，只定位真实当前数据对象。
    if (current == NULL)
    {
        return; // 数据尚未完整到达时等待后续 +IPD 分段。
    }

    if (wifi_weather_parse_int_field(current, "\"us_aqi\":", &aqi) != 0U)
    {
        wifi_weather_aqi = aqi; // 保存当前美国空气质量指数。
        wifi_weather_air_valid = 1U; // 标记空气质量字段已完整解析。
    }
}

/* 根据当前 HTTP 子事务选择相应的 JSON 解析逻辑。 */
static void wifi_weather_parse_response(void)
{
    if (wifi_weather_http_stage == WIFI_WEATHER_HTTP_AIR_QUALITY)
    {
        wifi_weather_parse_air_quality_response(); // 当前正在读取空气质量接口。
    }
    else
    {
        wifi_weather_parse_forecast_response(); // 当前正在读取实况和三天预报接口。
    }
}

/* 接收 HTTP 响应的每个原始字节，并持续尝试从中解析所需天气字段。 */
static void wifi_weather_consume_byte(uint8_t byte)
{
    if ((wifi_weather_busy == 0U) || (wifi_command != WIFI_COMMAND_HTTP_WAIT))
    {
        return; // 非天气响应阶段不占用普通 AT 或 WiFi 扫描的数据。
    }

    if (wifi_weather_length >= (WIFI_HTTP_BUFFER_SIZE - 1U))
    {
        return; // 缓存已满时保留前面的 JSON，避免任何越界写入。
    }

    if ((byte >= 32U) && (byte <= 126U))
    {
        wifi_weather_buffer[wifi_weather_length++] = (char)byte; // 保存可打印 ASCII，HTTP 和 JSON 字段均属于此范围。
    }
    else
    {
        wifi_weather_buffer[wifi_weather_length++] = ' '; // 将 CR/LF 等控制字符替换为空格，避免把关键字粘连。
    }
    wifi_weather_buffer[wifi_weather_length] = '\0'; // 让 strstr 和 strtod 始终面对完整 C 字符串。
    if (strstr(wifi_weather_buffer, "+IPD,") != NULL) // ESP8266 用 +IPD 前缀标识来自服务器的数据。
    {
        wifi_weather_response_seen = 1U; // 即使单行缓存溢出，也能在原始字节层确认服务器确实有响应。
    }
    wifi_weather_parse_response(); // 数据可能分多段到达，因此每次追加后都尝试解析。
}

/* 根据命令类型返回对应的等待超时时间。 */
static uint32_t wifi_command_timeout(WifiCommand_t command)
{
    if (command == WIFI_COMMAND_UPGRADE_TRIGGER_WAIT)
    {
        return WIFI_HTTP_TIMEOUT_MS;
    }

    if (command == WIFI_COMMAND_UPGRADE_TRIGGER_START)
    {
        return 10000U;
    }

    if (command == WIFI_COMMAND_UPGRADE_TRIGGER_LENGTH)
    {
        return 5000U;
    }

    if (command == WIFI_COMMAND_UPGRADE_WAIT)
    {
        return WIFI_HTTP_TIMEOUT_MS;
    }

    if (command == WIFI_COMMAND_UPGRADE_START)
    {
        return 10000U;
    }

    if (command == WIFI_COMMAND_UPGRADE_LENGTH)
    {
        return 5000U;
    }

    if (command == WIFI_COMMAND_HTTP_WAIT)
    {
        return WIFI_HTTP_TIMEOUT_MS;                     /* HTTP 响应包含 DNS、TCP 和服务器处理时间，使用更长超时。 */
    }

    if (command == WIFI_COMMAND_HTTP_START)
    {
        return 10000U;                                   /* TCP 建连可能需要等待域名解析。 */
    }

    if (command == WIFI_COMMAND_HTTP_LENGTH)
    {
        return 5000U;                                    /* CIPSEND 等待 '>' 提示的时间不应过长。 */
    }

    if (command == WIFI_COMMAND_SCAN)
    {
        return WIFI_SCAN_TIMEOUT_MS;                    /* 扫描可能返回多行结果，使用长超时。 */
    }

    if (command == WIFI_COMMAND_JOIN)
    {
        return WIFI_JOIN_TIMEOUT_MS;                    /* 连接需要等待认证和 DHCP。 */
    }

    return WIFI_AT_TIMEOUT_MS;                          /* 普通 AT 查询使用短超时。 */
}

/* 清空固定容量的扫描结果缓存。 */
static void wifi_clear_scan_results(void)
{
    memset(wifi_scan_results, 0, sizeof(wifi_scan_results)); /* 清除旧记录。 */
    wifi_scan_count = 0U;                                  /* 有效记录数量归零。 */
}

/* 加入一条扫描结果，并合并相同 SSID 的重复记录。 */
static void wifi_add_scan_result(const char *ssid,
                                 int16_t rssi,
                                 uint8_t encryption)
{
    uint8_t i;                                           /* 遍历已有记录的索引。 */

    if ((ssid == NULL) || (ssid[0] == '\0'))
    {
        return;                                          /* 忽略空 SSID。 */
    }

    for (i = 0U; i < wifi_scan_count; i++)
    {
        if (strcmp(wifi_scan_results[i].ssid, ssid) == 0)
        {
            if (rssi > wifi_scan_results[i].rssi)
            {
                wifi_scan_results[i].rssi = rssi;         /* 同名网络保留更强信号。 */
            }
            wifi_scan_results[i].encryption = encryption; /* 同步最新的加密类型。 */
            return;                                      /* 不重复新增记录。 */
        }
    }

    if (wifi_scan_count >= WIFI_SCAN_MAX_COUNT)
    {
        return;                                          /* 缓存已满时忽略多余网络。 */
    }

    strncpy(wifi_scan_results[wifi_scan_count].ssid,
            ssid,
            sizeof(wifi_scan_results[wifi_scan_count].ssid) - 1U);
    wifi_scan_results[wifi_scan_count].ssid[
        sizeof(wifi_scan_results[wifi_scan_count].ssid) - 1U] = '\0';
    wifi_scan_results[wifi_scan_count].rssi = rssi;
    wifi_scan_results[wifi_scan_count].encryption = encryption;
    wifi_scan_count++;
}

/* 返回当前 HTTP 子事务需要发送的完整请求正文。 */
static const uint8_t *wifi_weather_current_request(void)
{
    if (wifi_weather_http_stage == WIFI_WEATHER_HTTP_AIR_QUALITY)
    {
        return wifi_air_quality_http_request; // 空气质量阶段使用独立主机和路径。
    }

    return wifi_weather_http_request; // 默认使用实况与三天预报请求。
}

/* 返回当前 HTTP 子事务请求正文的精确字节数。 */
static uint16_t wifi_weather_current_request_length(void)
{
    if (wifi_weather_http_stage == WIFI_WEATHER_HTTP_AIR_QUALITY)
    {
        return (uint16_t)(sizeof(wifi_air_quality_http_request) - 1U); // 不发送 C 字符串末尾的 NUL。
    }

    return (uint16_t)(sizeof(wifi_weather_http_request) - 1U); // 不发送 C 字符串末尾的 NUL。
}

/* 发送状态机当前选中的 AT 命令。 */
static uint16_t wifi_upgrade_manifest_request_length(void)
{
    return (uint16_t)(sizeof(wifi_upgrade_manifest_request) - 1U);
}

static uint16_t wifi_upgrade_start_request_length(void)
{
    return (uint16_t)(sizeof(wifi_upgrade_start_request) - 1U);
}

static void wifi_upgrade_clear_response(void)
{
    memset(wifi_upgrade_buffer, 0, sizeof(wifi_upgrade_buffer));
    memset(&wifi_upgrade_manifest, 0, sizeof(wifi_upgrade_manifest));
    wifi_upgrade_length = 0U;
    wifi_upgrade_payload_sent = 0U;
    wifi_upgrade_response_seen = 0U;
    wifi_upgrade_response_overflow = 0U;
    wifi_upgrade_ipd_header_length = 0U;
    wifi_upgrade_ipd_payload_remaining = 0U;
}

static uint8_t wifi_upgrade_parse_version(const char *version)
{
    uint8_t component_count = 0U;
    uint8_t digit_count = 0U;

    while (*version != '\0')
    {
        if ((*version >= '0') && (*version <= '9'))
        {
            digit_count++;
        }
        else if ((*version == '.') && (digit_count > 0U) && (component_count < 2U))
        {
            component_count++;
            digit_count = 0U;
        }
        else
        {
            return 0U;
        }

        version++;
    }

    return ((component_count == 2U) && (digit_count > 0U)) ? 1U : 0U;
}

static uint8_t wifi_upgrade_read_field(const char *body,
                                       const char *name,
                                       char *value,
                                       uint16_t value_size)
{
    const char *line = body;
    size_t name_length = strlen(name);

    while ((line != NULL) && (*line != '\0'))
    {
        const char *line_end = strchr(line, '\n');
        size_t line_length = (line_end != NULL) ? (size_t)(line_end - line) : strlen(line);

        if ((line_length > 0U) && (line[line_length - 1U] == '\r'))
        {
            line_length--;
        }

        if ((line_length > name_length) &&
            (strncmp(line, name, name_length) == 0) &&
            (line[name_length] == '='))
        {
            size_t value_length = line_length - name_length - 1U;
            if ((value_length == 0U) || (value_length >= value_size))
            {
                return 0U;
            }

            memcpy(value, line + name_length + 1U, value_length);
            value[value_length] = '\0';
            return 1U;
        }

        if (line_end == NULL)
        {
            break;
        }
        line = line_end + 1U;
    }

    return 0U;
}

static uint8_t wifi_upgrade_parse_uint32(const char *text,
                                         int base,
                                         uint32_t *value)
{
    char *end;
    unsigned long parsed;

    if ((text == NULL) || (text[0] == '\0') || (value == NULL))
    {
        return 0U;
    }

    parsed = strtoul(text, &end, base);
    if ((*end != '\0') || (parsed > 0xFFFFFFFFUL))
    {
        return 0U;
    }

    *value = (uint32_t)parsed;
    return 1U;
}

static uint8_t wifi_upgrade_parse_manifest(void)
{
    const char *body;
    char format[4];
    char product[24];
    char version[sizeof(wifi_upgrade_manifest.version)];
    char size_text[12];
    char crc_text[12];
    char file[sizeof(WIFI_CONFIG_OTA_FIRMWARE_PATH)];
    uint32_t image_size;
    uint32_t image_crc32;

    if ((strstr(wifi_upgrade_buffer, "HTTP/1.1 200") == NULL) &&
        (strstr(wifi_upgrade_buffer, "HTTP/1.0 200") == NULL))
    {
        return 0U;
    }

    body = strstr(wifi_upgrade_buffer, "\r\n\r\n");
    if (body == NULL)
    {
        return 0U;
    }
    body += 4U;

    if ((wifi_upgrade_read_field(body, "format", format, sizeof(format)) == 0U) ||
        (wifi_upgrade_read_field(body, "product", product, sizeof(product)) == 0U) ||
        (wifi_upgrade_read_field(body, "version", version, sizeof(version)) == 0U) ||
        (wifi_upgrade_read_field(body, "size", size_text, sizeof(size_text)) == 0U) ||
        (wifi_upgrade_read_field(body, "crc32", crc_text, sizeof(crc_text)) == 0U) ||
        (wifi_upgrade_read_field(body, "file", file, sizeof(file)) == 0U))
    {
        return 0U;
    }

    if ((strcmp(format, "1") != 0) ||
        (strcmp(product, WIFI_CONFIG_OTA_PRODUCT) != 0) ||
        (strcmp(file, WIFI_CONFIG_OTA_FIRMWARE_PATH) != 0) ||
        (wifi_upgrade_parse_version(version) == 0U) ||
        (wifi_upgrade_parse_uint32(size_text, 10, &image_size) == 0U) ||
        (wifi_upgrade_parse_uint32(crc_text, 0, &image_crc32) == 0U) ||
        (image_size == 0U) ||
        ((image_size % 4U) != 0U) ||
        (image_size > WIFI_CONFIG_OTA_APP_MAX_SIZE))
    {
        return 0U;
    }

    strncpy(wifi_upgrade_manifest.version,
            version,
            sizeof(wifi_upgrade_manifest.version) - 1U);
    wifi_upgrade_manifest.version[sizeof(wifi_upgrade_manifest.version) - 1U] = '\0';
    wifi_upgrade_manifest.image_size = image_size;
    wifi_upgrade_manifest.image_crc32 = image_crc32;
    return 1U;
}

static void wifi_upgrade_consume_byte(uint8_t byte)
{
    char *payload_text;
    char *payload_end;
    unsigned long payload_length;

    if ((wifi_upgrade_busy == 0U) || (wifi_command != WIFI_COMMAND_UPGRADE_WAIT))
    {
        return;
    }

    if (wifi_upgrade_ipd_payload_remaining != 0U)
    {
        wifi_upgrade_ipd_payload_remaining--;
        if (wifi_upgrade_length >= (WIFI_UPGRADE_BUFFER_SIZE - 1U))
        {
            wifi_upgrade_response_overflow = 1U;
            return;
        }

        wifi_upgrade_buffer[wifi_upgrade_length++] = (char)byte;
        wifi_upgrade_buffer[wifi_upgrade_length] = '\0';
        return;
    }

    if (wifi_upgrade_ipd_header_length == 0U)
    {
        if (byte == (uint8_t)'+')
        {
            wifi_upgrade_ipd_header[0] = '+';
            wifi_upgrade_ipd_header_length = 1U;
        }
        return;
    }

    if (wifi_upgrade_ipd_header_length >= (sizeof(wifi_upgrade_ipd_header) - 1U))
    {
        wifi_upgrade_ipd_header_length = 0U;
        return;
    }

    wifi_upgrade_ipd_header[wifi_upgrade_ipd_header_length++] = (char)byte;
    if (wifi_upgrade_ipd_header_length <= 5U)
    {
        if (memcmp(wifi_upgrade_ipd_header, "+IPD,", wifi_upgrade_ipd_header_length) != 0)
        {
            wifi_upgrade_ipd_header_length = (byte == (uint8_t)'+') ? 1U : 0U;
            if (wifi_upgrade_ipd_header_length != 0U)
            {
                wifi_upgrade_ipd_header[0] = '+';
            }
        }
        return;
    }

    if (((byte >= (uint8_t)'0') && (byte <= (uint8_t)'9')) || (byte == (uint8_t)','))
    {
        return;
    }

    if ((byte == (uint8_t)':') && (wifi_upgrade_ipd_header_length > 6U))
    {
        wifi_upgrade_ipd_header[wifi_upgrade_ipd_header_length - 1U] = '\0';
        payload_text = strrchr(wifi_upgrade_ipd_header, ',');
        if (payload_text != NULL)
        {
            payload_length = strtoul(payload_text + 1, &payload_end, 10);
            if ((*payload_end == '\0') &&
                (payload_length != 0U) &&
                (payload_length <= 0xFFFFUL))
            {
                wifi_upgrade_ipd_payload_remaining = (uint16_t)payload_length;
                wifi_upgrade_response_seen = 1U;
            }
        }
    }

    wifi_upgrade_ipd_header_length = 0U;
}

static void wifi_send_command(WifiCommand_t command)
{
    char command_text[112];                              /* 保存带 SSID 和密码的连接命令。 */
    int length;                                           /* 保存格式化结果长度。 */

    switch (command)
    {
        case WIFI_COMMAND_AT:
            WifiUart_SendText("AT\r\n");
            break;

        case WIFI_COMMAND_MODE:
            WifiUart_SendText("AT+CWMODE=1\r\n");
            break;

        case WIFI_COMMAND_SERVER_OFF:
            WifiUart_SendText("AT+CIPSERVER=0\r\n");
            break;

        case WIFI_COMMAND_NORMAL_MODE:
            WifiUart_SendText("AT+CIPMODE=0\r\n");
            break;

        case WIFI_COMMAND_ACTIVE_RECEIVE:
            WifiUart_SendText("AT+CIPRECVMODE=0\r\n");
            break;

        case WIFI_COMMAND_SINGLE_CONNECTION:
            WifiUart_SendText("AT+CIPMUX=0\r\n");
            break;

        case WIFI_COMMAND_JOIN:
            length = snprintf(command_text,
                              sizeof(command_text),
                              "AT+CWJAP=\"%s\",\"%s\"\r\n",
                              wifi_join_ssid,
                              wifi_join_password);
            if ((length < 0) || ((size_t)length >= sizeof(command_text)))
            {
                wifi_state = WIFI_STATE_ERROR;            /* 命令过长时报告连接失败。 */
                wifi_command_pending = 0U;               /* 清除等待标志。 */
                wifi_next_command_tick = HAL_GetTick() + WIFI_REFRESH_PERIOD_MS;
                return;
            }
            WifiUart_SendText(command_text);
            break;

        case WIFI_COMMAND_STATUS:
            WifiUart_SendText("AT+CIPSTATUS\r\n");
            break;

        case WIFI_COMMAND_AP:
            WifiUart_SendText("AT+CWJAP?\r\n");
            break;

        case WIFI_COMMAND_IP:
            WifiUart_SendText("AT+CIFSR\r\n");
            break;

        case WIFI_COMMAND_SCAN:
            WifiUart_SendText("AT+CWLAP\r\n");
            break;

        case WIFI_COMMAND_HTTP_START:
            if (wifi_weather_http_stage == WIFI_WEATHER_HTTP_AIR_QUALITY)
            {
                WifiUart_SendText("AT+CIPSTART=\"TCP\",\"air-quality-api.open-meteo.com\",80\r\n"); // 连接空气质量服务的明文 HTTP 端口。
            }
            else
            {
                WifiUart_SendText("AT+CIPSTART=\"TCP\",\"api.open-meteo.com\",80\r\n"); // 连接气象预报服务的明文 HTTP 端口。
            }
            break;

        case WIFI_COMMAND_HTTP_LENGTH:
            length = snprintf(command_text,
                               sizeof(command_text),
                               "AT+CIPSEND=%u\r\n",
                               (unsigned int)wifi_weather_current_request_length()); // 请求长度由当前报文数组自动计算，避免手工填写出错。
            if ((length < 0) || ((size_t)length >= sizeof(command_text)))
            {
                wifi_weather_busy = 0U;                  // 命令构造异常时不再等待 HTTP 响应。
                wifi_weather_failed = 1U;                // 通知天气页面本次请求失败。
                wifi_command_pending = 0U;               // 清除等待标志，恢复普通 WiFi 状态机。
                wifi_command = WIFI_COMMAND_STATUS;      // 后续重新检查 WiFi 连接状态。
                wifi_next_command_tick = HAL_GetTick() + WIFI_REFRESH_PERIOD_MS;
                return;
            }
            WifiUart_SendText(command_text);              // 请求 ESP8266 进入指定长度的透传发送阶段。
            break;

        case WIFI_COMMAND_UPGRADE_START:
            length = snprintf(command_text,
                              sizeof(command_text),
                              "AT+CIPSTART=\"TCP\",\"%s\",%u\r\n",
                              WIFI_CONFIG_OTA_SERVER_HOST,
                              (unsigned int)WIFI_CONFIG_OTA_SERVER_PORT);
            if ((length < 0) || ((size_t)length >= sizeof(command_text)))
            {
                wifi_upgrade_busy = 0U;
                wifi_upgrade_failed = 1U;
                wifi_command_pending = 0U;
                wifi_command = WIFI_COMMAND_STATUS;
                wifi_next_command_tick = HAL_GetTick() + WIFI_REFRESH_PERIOD_MS;
                return;
            }
            WifiUart_SendText(command_text);
            break;

        case WIFI_COMMAND_UPGRADE_LENGTH:
            length = snprintf(command_text,
                               sizeof(command_text),
                              "AT+CIPSEND=%u\r\n",
                              (unsigned int)wifi_upgrade_manifest_request_length());
            if ((length < 0) || ((size_t)length >= sizeof(command_text)))
            {
                wifi_upgrade_busy = 0U;
                wifi_upgrade_failed = 1U;
                wifi_command_pending = 0U;
                wifi_command = WIFI_COMMAND_STATUS;
                wifi_next_command_tick = HAL_GetTick() + WIFI_REFRESH_PERIOD_MS;
                return;
            }
            WifiUart_SendText(command_text);
            break;

        case WIFI_COMMAND_UPGRADE_TRIGGER_START:
            length = snprintf(command_text,
                              sizeof(command_text),
                              "AT+CIPSTART=\"TCP\",\"%s\",%u\r\n",
                              WIFI_CONFIG_OTA_SERVER_HOST,
                              (unsigned int)WIFI_CONFIG_OTA_SERVER_PORT);
            if ((length < 0) || ((size_t)length >= sizeof(command_text)))
            {
                wifi_upgrade_start_busy = 0U;
                wifi_upgrade_start_failed = 1U;
                wifi_command_pending = 0U;
                wifi_command = WIFI_COMMAND_STATUS;
                wifi_next_command_tick = HAL_GetTick() + WIFI_REFRESH_PERIOD_MS;
                return;
            }
            WifiUart_SendText(command_text);
            break;

        case WIFI_COMMAND_UPGRADE_TRIGGER_LENGTH:
            length = snprintf(command_text,
                              sizeof(command_text),
                              "AT+CIPSEND=%u\r\n",
                              (unsigned int)wifi_upgrade_start_request_length());
            if ((length < 0) || ((size_t)length >= sizeof(command_text)))
            {
                wifi_upgrade_start_busy = 0U;
                wifi_upgrade_start_failed = 1U;
                wifi_command_pending = 0U;
                wifi_command = WIFI_COMMAND_STATUS;
                wifi_next_command_tick = HAL_GetTick() + WIFI_REFRESH_PERIOD_MS;
                return;
            }
            WifiUart_SendText(command_text);
            break;

        case WIFI_COMMAND_CLOSE:
            WifiUart_SendText("AT+CIPCLOSE\r\n");
            break;

        default:
            return;                                      /* 未知命令不发送数据。 */
    }

    wifi_command = command;                              /* 记录本次发送的命令。 */
    wifi_command_pending = 1U;                           /* 开始等待最终响应。 */
    wifi_command_deadline = HAL_GetTick() + wifi_command_timeout(command);
}

/* 解析 AT+CIPSTATUS 返回的 STATUS:n 行。 */
static void wifi_parse_status_line(const char *line)
{
    long status_code;                                     /* 保存解析出的状态码。 */

    if (strncmp(line, "STATUS:", 7U) != 0)
    {
        return;                                          /* 非状态行交给其他解析函数。 */
    }

    status_code = strtol(line + 7, NULL, 10);
    if ((status_code == 2L) || (status_code == 3L) || (status_code == 4L))
    {
        /* 2=已获得IP，3=TCP连接中，4=TCP刚断开；这些状态下 WiFi 本身仍保持连接。 */
        wifi_state = WIFI_STATE_CONNECTED;
    }
    else if (status_code == 5L)
    {
        /* 只有模块明确报告 WiFi 断开时才清除 IP 并进入未连接状态。 */
        wifi_ip[0] = '\0';
        wifi_state = WIFI_STATE_DISCONNECTED;
    }
}

/* 解析当前接入点名称和信号强度。 */
static void wifi_parse_ap_line(const char *line)
{
    const char *first_quote;                              /* SSID 起始引号。 */
    const char *second_quote;                             /* SSID 结束引号。 */
    const char *last_comma;                               /* RSSI 前的最后一个逗号。 */

    if (strncmp(line, "+CWJAP:", 7U) != 0)
    {
        return;                                          /* 非当前接入点行直接忽略。 */
    }

    first_quote = strchr(line, '"');
    if (first_quote != NULL)
    {
        second_quote = strchr(first_quote + 1, '"');
        if (second_quote != NULL)
        {
            wifi_copy_field(wifi_ssid,
                            (uint16_t)sizeof(wifi_ssid),
                            first_quote + 1,
                            (uint16_t)(second_quote - first_quote - 1));
        }
    }

    last_comma = strrchr(line, ',');
    if (last_comma != NULL)
    {
        wifi_rssi = (int16_t)strtol(last_comma + 1, NULL, 10);
    }
}

/* 解析 +CWLAP:(加密类型,"SSID",RSSI,...) 扫描结果。 */
static void wifi_parse_scan_line(const char *line)
{
    const char *scan_prefix;                              /* +CWLAP: 的实际位置。 */
    const char *cursor;                                   /* 当前解析位置。 */
    const char *first_quote;                              /* SSID 起始引号。 */
    const char *second_quote;                             /* SSID 结束引号。 */
    char *endptr;                                         /* 数字解析结束位置。 */
    char ssid[33];                                        /* 当前扫描结果的 SSID。 */
    long encryption;                                      /* 加密类型编号。 */
    long rssi;                                            /* 信号强度。 */

    scan_prefix = strstr(line, "+CWLAP:");
    if (scan_prefix == NULL)
    {
        return;                                          /* 非扫描结果行直接忽略。 */
    }

    cursor = scan_prefix + 7;
    if (*cursor == '(')
    {
        cursor++;                                        /* 兼容带左括号的固件格式。 */
    }

    encryption = strtol(cursor, &endptr, 10);
    if (endptr == cursor)
    {
        return;                                          /* 加密类型缺失时放弃本行。 */
    }

    first_quote = strchr(endptr, '"');
    if (first_quote == NULL)
    {
        return;                                          /* 找不到 SSID 起始引号。 */
    }

    second_quote = strchr(first_quote + 1, '"');
    if (second_quote == NULL)
    {
        return;                                          /* 找不到 SSID 结束引号。 */
    }

    wifi_copy_field(ssid,
                    (uint16_t)sizeof(ssid),
                    first_quote + 1,
                    (uint16_t)(second_quote - first_quote - 1));
    cursor = second_quote + 1;
    while ((*cursor == ',') || (*cursor == ' '))
    {
        cursor++;                                        /* 跳过字段分隔符和空格。 */
    }

    rssi = strtol(cursor, NULL, 10);
    wifi_add_scan_result(ssid, (int16_t)rssi, (uint8_t)encryption);
}

/* 解析 AT+CIFSR 返回的 STAIP 行。 */
static void wifi_parse_ip_line(const char *line)
{
    const char *first_quote;                              /* IP 起始引号。 */
    const char *second_quote;                             /* IP 结束引号。 */

    if (strstr(line, "STAIP") == NULL)
    {
        return;                                          /* 只处理站点模式地址。 */
    }

    first_quote = strchr(line, '"');
    if (first_quote == NULL)
    {
        return;                                          /* 响应格式不完整。 */
    }

    second_quote = strchr(first_quote + 1, '"');
    if (second_quote != NULL)
    {
        wifi_copy_field(wifi_ip,
                        (uint16_t)sizeof(wifi_ip),
                        first_quote + 1,
                        (uint16_t)(second_quote - first_quote - 1));
        if (wifi_ip[0] != '\0')
        {
            wifi_state = WIFI_STATE_CONNECTED;
        }
    }
}

/* 完成当前 AT 命令并安排下一条状态机命令。 */
static void wifi_finish_command(uint8_t success)
{
    uint32_t now = HAL_GetTick();                        /* 当前系统节拍。 */

    wifi_command_pending = 0U;
    if (success == 0U)
    {
        if ((wifi_command == WIFI_COMMAND_UPGRADE_TRIGGER_START) ||
            (wifi_command == WIFI_COMMAND_UPGRADE_TRIGGER_LENGTH) ||
            (wifi_command == WIFI_COMMAND_UPGRADE_TRIGGER_WAIT))
        {
            wifi_upgrade_start_busy = 0U;
            wifi_upgrade_start_failed = 1U;
            wifi_command = WIFI_COMMAND_CLOSE;            /* 失败后先释放可能残留的 TCP 连接。 */
            wifi_next_command_tick = now;
            return;
        }

        if ((wifi_command == WIFI_COMMAND_UPGRADE_START) ||
            (wifi_command == WIFI_COMMAND_UPGRADE_LENGTH) ||
            (wifi_command == WIFI_COMMAND_UPGRADE_WAIT))
        {
            wifi_upgrade_busy = 0U;
            wifi_upgrade_failed = 1U;
            if ((wifi_command == WIFI_COMMAND_UPGRADE_START) ||
                (wifi_command == WIFI_COMMAND_UPGRADE_LENGTH))
            {
                wifi_upgrade_fail_reason = WIFI_UPGRADE_FAIL_TCP;
            }
            else if (wifi_upgrade_fail_reason == WIFI_UPGRADE_FAIL_NONE)
            {
                wifi_upgrade_fail_reason = WIFI_UPGRADE_FAIL_RESPONSE;
            }
            wifi_command = WIFI_COMMAND_CLOSE;
            wifi_next_command_tick = now;
            return;
        }

        if ((wifi_command == WIFI_COMMAND_HTTP_START) ||
            (wifi_command == WIFI_COMMAND_HTTP_LENGTH) ||
            (wifi_command == WIFI_COMMAND_HTTP_WAIT))
        {
            wifi_weather_busy = 0U;                       /* 结束本次天气事务，防止页面一直显示加载中。 */
            wifi_weather_failed = 1U;                     /* 记录 TCP、发送或响应阶段发生的失败。 */
            wifi_command = WIFI_COMMAND_CLOSE;            /* 释放残留 TCP 连接，避免污染下一次建连。 */
            wifi_next_command_tick = now;
            return;
        }

        if (wifi_command == WIFI_COMMAND_SCAN)
        {
            wifi_scan_busy = 0U;                         /* 扫描已结束。 */
            wifi_scan_failed = 1U;                       /* 记录扫描失败供页面显示。 */
            wifi_command = WIFI_COMMAND_STATUS;
            wifi_next_command_tick = now + WIFI_REFRESH_PERIOD_MS;
            return;                                      /* 保留原来的联网状态。 */
        }

        wifi_state = ((wifi_command == WIFI_COMMAND_AT) || (wifi_command == WIFI_COMMAND_MODE)) ?
                     WIFI_STATE_STARTING : WIFI_STATE_ERROR;
        wifi_command = WIFI_COMMAND_AT;
        wifi_next_command_tick = now + WIFI_STARTUP_DELAY_MS;

        wifi_init_fail_count++;
        if (wifi_init_fail_count >= 3U)
        {
            wifi_init_fail_count = 0U;
            WifiUart_SendText("AT+RST\r\n");             /* 连续失败时复位 ESP8266，避免卡死在配置重试循环。 */
            WifiUart_ClearRx();                          /* 丢弃复位前的残留响应，防止污染下一条命令。 */
        }
        return;
    }

    switch (wifi_command)
    {
        case WIFI_COMMAND_AT:
            wifi_init_fail_count = 0U;                   /* AT 恢复响应说明模块工作正常，清除失败计数。 */
            wifi_state = WIFI_STATE_CHECKING;
            wifi_command = WIFI_COMMAND_MODE;
            wifi_next_command_tick = now + 50U;
            break;

        case WIFI_COMMAND_MODE:
            wifi_command = WIFI_COMMAND_SERVER_OFF;
            wifi_next_command_tick = now + 50U;
            break;

        case WIFI_COMMAND_SERVER_OFF:
            wifi_command = WIFI_COMMAND_NORMAL_MODE;
            wifi_next_command_tick = now + 50U;
            break;

        case WIFI_COMMAND_NORMAL_MODE:
            wifi_command = WIFI_COMMAND_ACTIVE_RECEIVE;
            wifi_next_command_tick = now + 50U;
            break;

        case WIFI_COMMAND_ACTIVE_RECEIVE:
            wifi_command = WIFI_COMMAND_SINGLE_CONNECTION;
            wifi_next_command_tick = now + 50U;
            break;

        case WIFI_COMMAND_SINGLE_CONNECTION:
            wifi_state = WIFI_STATE_DISCONNECTED;
            wifi_command = WIFI_COMMAND_STATUS;
            wifi_next_command_tick = now + 50U;
            break;

        case WIFI_COMMAND_SCAN:
            wifi_scan_busy = 0U;                         /* 扫描完成但不改变联网状态。 */
            wifi_scan_failed = 0U;
            wifi_command = WIFI_COMMAND_STATUS;
            wifi_next_command_tick = now + WIFI_REFRESH_PERIOD_MS;
            break;

        case WIFI_COMMAND_HTTP_START:
            wifi_command = WIFI_COMMAND_HTTP_LENGTH;      /* TCP 建连成功后再请求 ESP 接收 HTTP 正文。 */
            wifi_next_command_tick = now + 50U;
            break;

        case WIFI_COMMAND_UPGRADE_START:
            wifi_command = WIFI_COMMAND_UPGRADE_LENGTH;
            wifi_next_command_tick = now + 50U;
            break;

        case WIFI_COMMAND_UPGRADE_TRIGGER_START:
            wifi_command = WIFI_COMMAND_UPGRADE_TRIGGER_LENGTH;
            wifi_next_command_tick = now + 50U;
            break;

        case WIFI_COMMAND_UPGRADE_TRIGGER_WAIT:
            wifi_upgrade_start_busy = 0U;
            wifi_upgrade_start_ready = wifi_upgrade_start_response_accepted;
            wifi_upgrade_start_failed = (wifi_upgrade_start_ready != 0U) ? 0U : 1U;
            wifi_command = WIFI_COMMAND_STATUS;
            wifi_next_command_tick = now + WIFI_REFRESH_PERIOD_MS;
            break;

        case WIFI_COMMAND_UPGRADE_WAIT:
            wifi_upgrade_busy = 0U;
            wifi_upgrade_ready = (wifi_upgrade_response_overflow == 0U) ?
                                 wifi_upgrade_parse_manifest() : 0U;
            wifi_upgrade_failed = (wifi_upgrade_ready != 0U) ? 0U : 1U;
            if (wifi_upgrade_failed != 0U)
            {
                wifi_upgrade_fail_reason = WIFI_UPGRADE_FAIL_PARSE;
            }
            wifi_command = WIFI_COMMAND_STATUS;
            wifi_next_command_tick = now + WIFI_REFRESH_PERIOD_MS;
            break;

        case WIFI_COMMAND_CLOSE:
            wifi_command = WIFI_COMMAND_STATUS;
            wifi_next_command_tick = now + 50U;
            break;

        case WIFI_COMMAND_HTTP_WAIT:
            if (wifi_weather_http_stage == WIFI_WEATHER_HTTP_FORECAST)
            {
                if ((wifi_weather_current_valid == 0U) || (wifi_weather_forecast_valid == 0U))
                {
                    wifi_weather_busy = 0U;               /* 实况或三天预报不完整时不继续发起空气质量请求。 */
                    wifi_weather_failed = 1U;             /* 页面显示请求失败而不是展示混合旧数据。 */
                    wifi_command = WIFI_COMMAND_STATUS;   /* 回到正常联网轮询。 */
                    wifi_next_command_tick = now + WIFI_REFRESH_PERIOD_MS;
                    break;
                }

                wifi_weather_http_stage = WIFI_WEATHER_HTTP_AIR_QUALITY; // 第一段成功后切换到空气质量接口。
                wifi_weather_clear_response();            // 清空气象 JSON，为第二段响应留出完整缓存。
                wifi_command = WIFI_COMMAND_HTTP_START;  // 从新的 TCP 连接开始空气质量请求。
                wifi_next_command_tick = now + 50U;       // 给 ESP8266 留出关闭前一条连接的短暂间隔。
                break;
            }

            wifi_weather_busy = 0U;                       /* 空气质量接口结束后，完整天气事务才结束。 */
            wifi_weather_ready = ((wifi_weather_current_valid != 0U) &&
                                  (wifi_weather_forecast_valid != 0U) &&
                                  (wifi_weather_air_valid != 0U)) ? 1U : 0U; // 三类数据齐全才对页面提交新数据。
            wifi_weather_failed = (wifi_weather_ready != 0U) ? 0U : 1U; /* 任一类型解析失败都报告失败。 */
            wifi_command = WIFI_COMMAND_STATUS;           /* 回到现有 WiFi 轮询节奏。 */
            wifi_next_command_tick = now + WIFI_REFRESH_PERIOD_MS;
            break;

        case WIFI_COMMAND_JOIN:
            wifi_command = WIFI_COMMAND_STATUS;
            wifi_next_command_tick = now + 50U;
            break;

        case WIFI_COMMAND_STATUS:
            if (wifi_state == WIFI_STATE_CONNECTED)
            {
                wifi_command = WIFI_COMMAND_AP;
                wifi_next_command_tick = now + 50U;
            }
            else if ((wifi_auto_join_attempted == 0U) && (WIFI_CONFIG_SSID[0] != '\0'))
            {
                wifi_auto_join_attempted = 1U;            /* 每次上电只自动尝试一次，密码错误或路由器离线时不反复阻塞串口。 */
                (void)WifiManager_Connect(WIFI_CONFIG_SSID, WIFI_CONFIG_PASSWORD); /* 使用预存凭据启动非阻塞连接。 */
            }
            else
            {
                wifi_command = WIFI_COMMAND_STATUS;
                wifi_next_command_tick = now + WIFI_REFRESH_PERIOD_MS;
            }
            break;

        case WIFI_COMMAND_AP:
            wifi_command = WIFI_COMMAND_IP;
            wifi_next_command_tick = now + 50U;
            break;

        case WIFI_COMMAND_IP:
        default:
            wifi_command = WIFI_COMMAND_STATUS;
            wifi_next_command_tick = now + WIFI_REFRESH_PERIOD_MS;
            break;
    }
}

/* 解析一整行 ESP8266 响应，并处理 OK、ERROR 和联网异步事件。 */
static void wifi_parse_line(const char *line)
{
    if ((line == NULL) || (line[0] == '\0'))
    {
        return;                                          /* 空行没有业务含义。 */
    }

    if (strstr(line, "WIFI GOT IP") != NULL)
    {
        wifi_state = WIFI_STATE_CONNECTED;
    }
    else if (strstr(line, "WIFI DISCONNECT") != NULL)
    {
        wifi_state = WIFI_STATE_DISCONNECTED;
        wifi_auto_join_attempted = 0U;                   /* 断线后允许用预存凭据再自动连接一次。 */
    }
    else if (strncmp(line, "FAIL", 4U) == 0)
    {
        wifi_state = WIFI_STATE_DISCONNECTED;
    }

    wifi_parse_status_line(line);
    wifi_parse_ap_line(line);
    wifi_parse_scan_line(line);
    wifi_parse_ip_line(line);

    if ((wifi_command == WIFI_COMMAND_UPGRADE_TRIGGER_WAIT) &&
        ((strstr(line, "HTTP/1.0 202") != NULL) ||
         (strstr(line, "HTTP/1.1 202") != NULL)))
    {
        wifi_upgrade_start_response_accepted = 1U;
    }

    if ((wifi_command == WIFI_COMMAND_UPGRADE_TRIGGER_WAIT) && (strstr(line, "CLOSED") != NULL))
    {
        wifi_finish_command(wifi_upgrade_start_response_accepted);
        return;
    }

    if ((wifi_command == WIFI_COMMAND_UPGRADE_TRIGGER_START) && (strstr(line, "CLOSED") != NULL))
    {
        wifi_finish_command(0U);
        return;
    }

    if ((wifi_command == WIFI_COMMAND_UPGRADE_TRIGGER_LENGTH) && (strcmp(line, "OK") == 0))
    {
        return;
    }

    if (((wifi_command == WIFI_COMMAND_UPGRADE_START) ||
         (wifi_command == WIFI_COMMAND_HTTP_START) ||
         (wifi_command == WIFI_COMMAND_UPGRADE_TRIGGER_START)) &&
        (strstr(line, "ALREADY CONNECTED") != NULL))
    {
        wifi_finish_command(1U);                         /* 旧连接未释放时 CIPSTART 报已连接，视为建连成功。 */
        return;
    }

    if ((wifi_command == WIFI_COMMAND_UPGRADE_WAIT) && (strstr(line, "CLOSED") != NULL))
    {
        if (wifi_upgrade_response_seen == 0U)
        {
            wifi_upgrade_fail_reason = WIFI_UPGRADE_FAIL_RESPONSE;
        }
        wifi_finish_command(wifi_upgrade_response_seen);
        return;
    }

    if ((wifi_command == WIFI_COMMAND_UPGRADE_START) && (strstr(line, "CLOSED") != NULL))
    {
        wifi_finish_command(0U);
        return;
    }

    if ((wifi_command == WIFI_COMMAND_UPGRADE_LENGTH) && (strcmp(line, "OK") == 0))
    {
        return;
    }

    if ((wifi_command == WIFI_COMMAND_HTTP_WAIT) && (strstr(line, "+IPD,") != NULL))
    {
        wifi_weather_response_seen = 1U;                 /* 已看到服务器响应分段，CLOSED 时可据此判断链路是否通。 */
    }

    if ((wifi_command == WIFI_COMMAND_HTTP_WAIT) && (strstr(line, "CLOSED") != NULL))
    {
        wifi_finish_command(wifi_weather_response_seen); /* TCP 关闭即为 HTTP 请求的自然结束点。 */
        return;
    }

    if ((wifi_command == WIFI_COMMAND_HTTP_START) && (strstr(line, "CLOSED") != NULL))
    {
        wifi_finish_command(0U);                          /* 建连阶段直接 CLOSED，不能把后续 OK 当成连接成功。 */
        return;
    }

    if ((wifi_command == WIFI_COMMAND_HTTP_LENGTH) && (strcmp(line, "OK") == 0))
    {
        return;                                          /* CIPSEND 的 OK 后还必须等待独立到达的 '>' 提示。 */
    }

    if (strncmp(line, "no change", 9U) == 0)
    {
        wifi_finish_command(1U);                         /* 配置值未变化等价于设置成功，旧固件常用此响应。 */
        return;
    }

    if ((strncmp(line, "busy", 4U) == 0) ||
        (strstr(line, "link is not valid") != NULL))
    {
        wifi_finish_command(0U);                         /* 模块繁忙或连接已失效，立即失败进入重试，避免干等超时。 */
        return;
    }

    if ((strncmp(line, "ERROR", 5U) == 0) &&
        ((wifi_command == WIFI_COMMAND_SERVER_OFF) ||
         (wifi_command == WIFI_COMMAND_ACTIVE_RECEIVE) ||
         (wifi_command == WIFI_COMMAND_CLOSE)))
    {
        wifi_finish_command(1U);
    }
    else if (strncmp(line, "ERROR", 5U) == 0)
    {
        wifi_finish_command(0U);
    }
    else if (strcmp(line, "OK") == 0)
    {
        wifi_finish_command(1U);
    }
}

/* 从环形缓冲区按行消费 ESP8266 返回的字节。 */
static void wifi_consume_uart(void)
{
    uint8_t byte;
    uint16_t byte_count = 0U;

    while ((byte_count < WIFI_RX_BYTES_PER_CYCLE) && (WifiUart_ReadByte(&byte) != 0U))
    {
        byte_count++;
        if ((wifi_command == WIFI_COMMAND_UPGRADE_LENGTH) &&
            (wifi_upgrade_busy != 0U) &&
            (wifi_upgrade_payload_sent == 0U) &&
            (byte == (uint8_t)'>'))
        {
            WifiUart_SendBytes(wifi_upgrade_manifest_request,
                               wifi_upgrade_manifest_request_length());
            wifi_upgrade_payload_sent = 1U;
            wifi_command = WIFI_COMMAND_UPGRADE_WAIT;
            wifi_command_pending = 1U;
            wifi_command_deadline = HAL_GetTick() + wifi_command_timeout(wifi_command);
            continue;
        }

        if ((wifi_command == WIFI_COMMAND_UPGRADE_TRIGGER_LENGTH) &&
            (wifi_upgrade_start_busy != 0U) &&
            (wifi_upgrade_start_payload_sent == 0U) &&
            (byte == (uint8_t)'>'))
        {
            WifiUart_SendBytes(wifi_upgrade_start_request,
                               wifi_upgrade_start_request_length());
            wifi_upgrade_start_payload_sent = 1U;
            wifi_command = WIFI_COMMAND_UPGRADE_TRIGGER_WAIT;
            wifi_command_pending = 1U;
            wifi_command_deadline = HAL_GetTick() + wifi_command_timeout(wifi_command);
            continue;
        }

        if ((wifi_command == WIFI_COMMAND_HTTP_LENGTH) &&
            (wifi_weather_busy != 0U) &&
            (wifi_weather_payload_sent == 0U) &&
            (byte == (uint8_t)'>'))
        {
            WifiUart_SendBytes(wifi_weather_current_request(),
                               wifi_weather_current_request_length()); // 只在 ESP 明确给出 '>' 后发送当前阶段的完整 HTTP 报文。
            wifi_weather_payload_sent = 1U;               // 防止后续字符被误判后重复发起 API 请求。
            wifi_command = WIFI_COMMAND_HTTP_WAIT;       // 此后只等待 +IPD 响应和 CLOSED。
            wifi_command_pending = 1U;                   // 继续让超时机制覆盖服务器响应阶段。
            wifi_command_deadline = HAL_GetTick() + wifi_command_timeout(wifi_command);
            continue;                                     // '>' 是协议提示符，不应加入普通 AT 文本行。
        }

        wifi_upgrade_consume_byte(byte);
        wifi_weather_consume_byte(byte);                  // 天气响应阶段在原始字节层累计 HTTP/JSON 内容。
        if (byte == (uint8_t)'\n')
        {
            wifi_line[wifi_line_length] = '\0';
            wifi_parse_line(wifi_line);
            wifi_line_length = 0U;
        }
        else if (byte != (uint8_t)'\r')
        {
            if (wifi_line_length < (WIFI_LINE_BUFFER_SIZE - 1U))
            {
                wifi_line[wifi_line_length++] = (char)byte;
            }
            else
            {
                wifi_line_length = 0U;                   /* 过长响应行直接丢弃。 */
            }
        }
    }
}

/* 初始化串口、状态和所有 WiFi 缓存。 */
void WifiManager_Init(void)
{
    WifiUart_Init();
    wifi_state = WIFI_STATE_STARTING;
    wifi_command = WIFI_COMMAND_AT;
    wifi_command_pending = 0U;
    wifi_scan_requested = 0U;
    wifi_scan_busy = 0U;
    wifi_scan_failed = 0U;
    wifi_command_deadline = 0U;
    wifi_next_command_tick = HAL_GetTick() + WIFI_STARTUP_DELAY_MS;
    wifi_line_length = 0U;
    wifi_ssid[0] = '\0';
    wifi_ip[0] = '\0';
    wifi_join_ssid[0] = '\0';
    wifi_join_password[0] = '\0';
    wifi_rssi = 0;
    wifi_auto_join_attempted = 0U;
    wifi_at_test_mode = 0U;
    wifi_weather_requested = 0U;
    wifi_weather_busy = 0U;
    wifi_weather_ready = 0U;
    wifi_weather_failed = 0U;
    wifi_weather_temperature = 0.0f;
    wifi_weather_code = -1;
    wifi_weather_apparent_temperature = 0.0f;
    wifi_weather_humidity = 0U;
    wifi_weather_wind_speed = 0.0f;
    wifi_weather_aqi = -1;
    wifi_weather_http_stage = WIFI_WEATHER_HTTP_FORECAST;
    wifi_weather_current_valid = 0U;
    wifi_weather_forecast_valid = 0U;
    wifi_weather_air_valid = 0U;
    wifi_weather_time[0] = '\0';
    memset(wifi_weather_forecasts, 0, sizeof(wifi_weather_forecasts));
    wifi_weather_clear_response();
    wifi_upgrade_requested = 0U;
    wifi_upgrade_busy = 0U;
    wifi_upgrade_ready = 0U;
    wifi_upgrade_failed = 0U;
    wifi_upgrade_clear_response();
    wifi_upgrade_start_requested = 0U;
    wifi_upgrade_start_busy = 0U;
    wifi_upgrade_start_ready = 0U;
    wifi_upgrade_start_failed = 0U;
    wifi_upgrade_start_payload_sent = 0U;
    wifi_upgrade_start_response_accepted = 0U;
    wifi_upgrade_fail_reason = WIFI_UPGRADE_FAIL_NONE;
    wifi_init_fail_count = 0U;
    wifi_clear_scan_results();
}

/* 非阻塞推进 WiFi 状态机，必须由系统周期任务反复调用。 */
void WifiManager_Process(void)
{
    uint32_t now = HAL_GetTick();

    if (wifi_at_test_mode != 0U)
    {
        return;                                          /* AT 诊断期间由测试模块读取原始回包。 */
    }

    wifi_consume_uart();
    if ((wifi_command_pending != 0U) && ((int32_t)(now - wifi_command_deadline) >= 0))
    {
        if (wifi_command == WIFI_COMMAND_UPGRADE_WAIT)
        {
            wifi_upgrade_fail_reason = WIFI_UPGRADE_FAIL_TIMEOUT;
        }
        wifi_finish_command(0U);                        /* 当前命令超时。 */
    }

    if ((wifi_weather_requested != 0U) &&
        (wifi_command_pending == 0U) &&
        (wifi_upgrade_busy == 0U) &&
        (wifi_upgrade_start_busy == 0U))
    {
        wifi_weather_requested = 0U;                     /* 本轮要么启动，要么明确报告失败，避免反复占用状态机。 */
        if ((wifi_state != WIFI_STATE_CONNECTED) || (wifi_ip[0] == '\0'))
        {
            wifi_weather_busy = 0U;                      /* 未拿到 IP 时不能建立 TCP 连接。 */
            wifi_weather_failed = 1U;                    /* 页面据此提示先连接 WiFi。 */
        }
        else
        {
            wifi_command = WIFI_COMMAND_HTTP_START;      /* 已联网后，从 TCP 建连步骤开始天气请求。 */
            wifi_next_command_tick = now;
        }
    }

    if ((wifi_upgrade_requested != 0U) &&
        (wifi_command_pending == 0U) &&
        (wifi_weather_busy == 0U) &&
        (wifi_upgrade_start_busy == 0U) &&
        (wifi_state != WIFI_STATE_STARTING) &&
        (wifi_state != WIFI_STATE_CHECKING) &&
        (wifi_state != WIFI_STATE_CONNECTING))
    {
        wifi_upgrade_requested = 0U;
        if ((wifi_state != WIFI_STATE_CONNECTED) || (wifi_ip[0] == '\0'))
        {
            wifi_upgrade_busy = 0U;
            wifi_upgrade_failed = 1U;
            wifi_upgrade_fail_reason = WIFI_UPGRADE_FAIL_NOT_CONNECTED;
        }
        else
        {
            wifi_command = WIFI_COMMAND_UPGRADE_START;
            wifi_next_command_tick = now;
        }
    }

    if ((wifi_upgrade_start_requested != 0U) &&
        (wifi_command_pending == 0U) &&
        (wifi_weather_busy == 0U) &&
        (wifi_upgrade_busy == 0U) &&
        (wifi_state != WIFI_STATE_STARTING) &&
        (wifi_state != WIFI_STATE_CHECKING) &&
        (wifi_state != WIFI_STATE_CONNECTING))
    {
        wifi_upgrade_start_requested = 0U;
        if ((wifi_state != WIFI_STATE_CONNECTED) || (wifi_ip[0] == '\0'))
        {
            wifi_upgrade_start_busy = 0U;
            wifi_upgrade_start_failed = 1U;
        }
        else
        {
            wifi_command = WIFI_COMMAND_UPGRADE_TRIGGER_START;
            wifi_next_command_tick = now;
        }
    }

    /* 初始化阶段先完成 AT 和工作模式设置，再插入页面发起的扫描。 */
    if ((wifi_scan_requested != 0U) &&
        (wifi_command_pending == 0U) &&
        (wifi_weather_busy == 0U) &&
        (wifi_upgrade_busy == 0U) &&
        (wifi_upgrade_start_busy == 0U) &&
        (wifi_state != WIFI_STATE_STARTING) &&
        (wifi_state != WIFI_STATE_CHECKING) &&
        (wifi_state != WIFI_STATE_CONNECTING))
    {
        wifi_command = WIFI_COMMAND_SCAN;
        wifi_scan_requested = 0U;
        wifi_next_command_tick = now;
    }

    if ((wifi_command_pending == 0U) && ((int32_t)(now - wifi_next_command_tick) >= 0))
    {
        wifi_send_command(wifi_command);
    }
}

/* 立即从 AT 基础检测重新开始普通联网状态查询。 */
uint8_t WifiManager_RequestUpgradeManifest(void)
{
    if ((wifi_at_test_mode != 0U) ||
        (wifi_upgrade_busy != 0U) ||
        (wifi_upgrade_start_busy != 0U) ||
        (wifi_weather_busy != 0U) ||
        (wifi_scan_busy != 0U))
    {
        return 0U;
    }

    wifi_upgrade_clear_response();
    wifi_upgrade_ready = 0U;
    wifi_upgrade_failed = 0U;
    wifi_upgrade_fail_reason = WIFI_UPGRADE_FAIL_NONE;
    wifi_upgrade_busy = 1U;
    wifi_upgrade_requested = 1U;
    return 1U;
}

uint8_t WifiManager_IsUpgradeManifestBusy(void)
{
    return wifi_upgrade_busy;
}

uint8_t WifiManager_IsUpgradeManifestReady(void)
{
    return wifi_upgrade_ready;
}

uint8_t WifiManager_IsUpgradeManifestFailed(void)
{
    return wifi_upgrade_failed;
}

WifiUpgradeFailReason_t WifiManager_GetUpgradeFailReason(void)
{
    return wifi_upgrade_fail_reason;
}

const WifiUpgradeManifest_t *WifiManager_GetUpgradeManifest(void)
{
    return (wifi_upgrade_ready != 0U) ? &wifi_upgrade_manifest : NULL;
}

uint8_t WifiManager_RequestUpgradeStart(void)
{
    if ((wifi_at_test_mode != 0U) ||
        (wifi_upgrade_busy != 0U) ||
        (wifi_upgrade_start_busy != 0U) ||
        (wifi_weather_busy != 0U) ||
        (wifi_scan_busy != 0U))
    {
        return 0U;
    }

    wifi_upgrade_start_ready = 0U;
    wifi_upgrade_start_failed = 0U;
    wifi_upgrade_start_payload_sent = 0U;
    wifi_upgrade_start_response_accepted = 0U;
    wifi_upgrade_start_busy = 1U;
    wifi_upgrade_start_requested = 1U;
    return 1U;
}

uint8_t WifiManager_IsUpgradeStartBusy(void)
{
    return wifi_upgrade_start_busy;
}

uint8_t WifiManager_IsUpgradeStartReady(void)
{
    return wifi_upgrade_start_ready;
}

uint8_t WifiManager_IsUpgradeStartFailed(void)
{
    return wifi_upgrade_start_failed;
}

void WifiManager_RequestRefresh(void)
{
    wifi_state = WIFI_STATE_CHECKING;
    wifi_command = WIFI_COMMAND_AT;
    wifi_command_pending = 0U;
    wifi_next_command_tick = 0U;
}

/* 清空旧结果并排队一次附近 WiFi 扫描。 */
void WifiManager_StartScan(void)
{
    wifi_clear_scan_results();
    wifi_scan_busy = 1U;
    wifi_scan_failed = 0U;
    wifi_scan_requested = 1U;

    if ((wifi_command_pending == 0U) &&
        (wifi_weather_busy == 0U) &&
        (wifi_upgrade_busy == 0U) &&
        (wifi_state != WIFI_STATE_STARTING) &&
        (wifi_state != WIFI_STATE_CHECKING) &&
        (wifi_state != WIFI_STATE_CONNECTING))
    {
        wifi_command = WIFI_COMMAND_SCAN;
        wifi_scan_requested = 0U;
        wifi_next_command_tick = 0U;
    }
}

/* 返回扫描是否仍在排队或等待响应。 */
uint8_t WifiManager_IsScanBusy(void)
{
    return wifi_scan_busy;
}

/* 返回最近一次扫描是否失败。 */
uint8_t WifiManager_IsScanFailed(void)
{
    return wifi_scan_failed;
}

/* 保存凭据并启动异步 WiFi 连接。 */
uint8_t WifiManager_Connect(const char *ssid, const char *password)
{
    if ((ssid == NULL) || (password == NULL) || (ssid[0] == '\0'))
    {
        return 0U;
    }

    strncpy(wifi_join_ssid, ssid, sizeof(wifi_join_ssid) - 1U);
    wifi_join_ssid[sizeof(wifi_join_ssid) - 1U] = '\0';
    strncpy(wifi_join_password, password, sizeof(wifi_join_password) - 1U);
    wifi_join_password[sizeof(wifi_join_password) - 1U] = '\0';
    wifi_auto_join_attempted = 1U;                      /* 手动或自动连接均算作本次上电已经选择过网络。 */
    wifi_ip[0] = '\0';
    wifi_state = WIFI_STATE_CONNECTING;
    wifi_command = WIFI_COMMAND_JOIN;
    wifi_command_pending = 0U;
    wifi_next_command_tick = 0U;
    return 1U;
}

/* 返回当前连接状态。 */
WifiState_t WifiManager_GetState(void)
{
    return wifi_state;
}

/* 返回当前 SSID。 */
const char *WifiManager_GetSsid(void)
{
    return wifi_ssid;
}

/* 返回当前 IPv4 地址。 */
const char *WifiManager_GetIp(void)
{
    return wifi_ip;
}

/* 返回当前信号强度。 */
int16_t WifiManager_GetRssi(void)
{
    return wifi_rssi;
}

/* 返回扫描结果数量。 */
uint8_t WifiManager_GetScanCount(void)
{
    return wifi_scan_count;
}

/* 返回指定扫描结果。 */
const WifiScanResult_t *WifiManager_GetScanResult(uint8_t index)
{
    if (index >= wifi_scan_count)
    {
        return NULL;
    }

    return &wifi_scan_results[index];
}

/* 请求一次天气数据；真正的 AT 命令会在 WifiManager_Process 中异步发送。 */
void WifiManager_RequestWeather(void)
{
    if ((wifi_at_test_mode != 0U) || (wifi_weather_busy != 0U))
    {
        return;                                          /* AT 诊断或上一请求尚未结束时不抢占 USART1。 */
    }

    wifi_weather_clear_response();                       /* 清除旧响应，保证本次解析不会读到旧 JSON。 */
    wifi_weather_ready = 0U;                             /* 本次尚未得到新的有效温度。 */
    wifi_weather_failed = 0U;                            /* 清除上一次失败提示。 */
    wifi_weather_code = -1;                              /* 清除上一次图标使用的天气代码。 */
    wifi_weather_apparent_temperature = 0.0f;             /* 清除上一次体感温度。 */
    wifi_weather_humidity = 0U;                           /* 清除上一次湿度。 */
    wifi_weather_wind_speed = 0.0f;                       /* 清除上一次风速。 */
    wifi_weather_aqi = -1;                                /* 清除上一次空气质量指数。 */
    wifi_weather_http_stage = WIFI_WEATHER_HTTP_FORECAST; /* 新事务固定先请求实况与三天预报。 */
    wifi_weather_current_valid = 0U;                      /* 清除当前实况完整性标志。 */
    wifi_weather_forecast_valid = 0U;                     /* 清除三天预报完整性标志。 */
    wifi_weather_air_valid = 0U;                          /* 清除空气质量完整性标志。 */
    wifi_weather_time[0] = '\0';                         /* 清除上一份接口时间。 */
    memset(wifi_weather_forecasts, 0, sizeof(wifi_weather_forecasts)); /* 清除三天预报缓存。 */
    wifi_weather_busy = 1U;                              /* 页面立即可以显示“获取中”。 */
    wifi_weather_requested = 1U;                         /* 交给周期状态机在合适时机启动 TCP。 */
}

/* 返回天气请求是否仍在执行。 */
uint8_t WifiManager_IsWeatherBusy(void)
{
    return wifi_weather_busy; // 直接提供天气事务的执行标志给页面轮询。
}

/* 返回天气请求是否已完整取得实况、三天预报和空气质量。 */
uint8_t WifiManager_IsWeatherReady(void)
{
    return wifi_weather_ready; // 两次 HTTP 都完成且全部所需字段有效时此标志才为真。
}

/* 返回最近一次天气请求是否失败。 */
uint8_t WifiManager_IsWeatherFailed(void)
{
    return wifi_weather_failed; // TCP、发送、响应超时或解析缺失都会置位。
}

/* 返回最近一次成功解析的摄氏温度。 */
float WifiManager_GetWeatherTemperature(void)
{
    return wifi_weather_temperature; // 返回最近一次解析到的摄氏温度数值。
}

/* 返回最近一次成功解析的体感温度。 */
float WifiManager_GetWeatherApparentTemperature(void)
{
    return wifi_weather_apparent_temperature; // 返回实时体感温度，单位为摄氏度。
}

/* 返回最近一次成功解析的相对湿度。 */
uint8_t WifiManager_GetWeatherHumidity(void)
{
    return wifi_weather_humidity; // 返回相对湿度百分比。
}

/* 返回最近一次成功解析的十米风速。 */
float WifiManager_GetWeatherWindSpeed(void)
{
    return wifi_weather_wind_speed; // 返回米每秒单位的风速。
}

/* 返回最近一次成功解析的美国空气质量指数。 */
int16_t WifiManager_GetWeatherAqi(void)
{
    return wifi_weather_aqi; // 返回 US AQI，-1 表示尚未得到空气质量数据。
}

/* 返回最近一次成功解析的 WMO 天气代码。 */
int16_t WifiManager_GetWeatherCode(void)
{
    return wifi_weather_code; // 返回 -1 表示当前响应中尚未解析到天气代码。
}

/* 返回指定日期的三天预报记录。 */
const WifiWeatherForecast_t *WifiManager_GetWeatherForecast(uint8_t index)
{
    if ((wifi_weather_ready == 0U) || (index >= WIFI_WEATHER_FORECAST_DAYS))
    {
        return NULL; // 数据未完成或索引越界时不给页面返回无效地址。
    }

    return &wifi_weather_forecasts[index]; // 返回只读的固定缓存记录。
}

/* 返回最近一次成功解析的接口时间。 */
const char *WifiManager_GetWeatherTime(void)
{
    return wifi_weather_time; // 返回内部固定缓存，调用方只读使用。
}

/* 切换 AT 测试模式，并在退出时恢复普通网络状态查询。 */
void WifiManager_SetAtTestMode(uint8_t enabled)
{
    if (enabled != 0U)
    {
        wifi_upgrade_requested = 0U;
        wifi_upgrade_busy = 0U;
        wifi_upgrade_ready = 0U;
        wifi_upgrade_failed = 1U;
        wifi_upgrade_clear_response();
    }

    if (enabled != 0U)
    {
        wifi_at_test_mode = 1U;                          /* 先设置标志，阻止下一轮自动发送。 */
        wifi_command_pending = 0U;                       /* 放弃普通状态机等待中的命令。 */
        wifi_weather_requested = 0U;                     /* 取消尚未开始的天气请求，避免诊断页被自动命令打断。 */
        wifi_weather_busy = 0U;                          /* 天气请求若未完成，离开后可由用户重新发起。 */
        wifi_scan_requested = 0U;                        /* 取消尚未发送的扫描请求。 */
        wifi_scan_busy = 0U;                             /* 不再把普通扫描显示为进行中。 */
        wifi_line_length = 0U;                           /* 丢弃普通状态机未完成的半行。 */
        WifiUart_ClearRx();                              /* 清掉切换前遗留的旧回包。 */
    }
    else
    {
        wifi_at_test_mode = 0U;                          /* 允许普通状态机重新处理串口。 */
        WifiManager_RequestRefresh();                    /* 从 AT 基础检测重新恢复。 */
    }
}
