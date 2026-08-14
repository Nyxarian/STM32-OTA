#ifndef __WIFI_MANAGER_H__
#define __WIFI_MANAGER_H__

#include <stdint.h>

/* ESP8266 当前的联网状态；扫描状态单独通过 IsScanBusy() 查询。 */
typedef enum
{
    WIFI_STATE_STARTING = 0,
    WIFI_STATE_CHECKING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_DISCONNECTED,
    WIFI_STATE_ERROR,
    WIFI_STATE_CONNECTING
} WifiState_t;

/* 一条由 AT+CWLAP 返回的无线网络记录。 */
typedef struct
{
    char ssid[33];
    int16_t rssi;
    uint8_t encryption;
} WifiScanResult_t;

/* 天气接口返回的一天预报数据，温度单位均为摄氏度。 */
typedef struct
{
    char date[11];
    int16_t weather_code;
    float temperature_min;
    float temperature_max;
} WifiWeatherForecast_t;

typedef struct
{
    char version[16];
    uint32_t image_size;
    uint32_t image_crc32;
} WifiUpgradeManifest_t;

/* 升级清单请求失败的具体原因，供界面显示准确的错误提示。 */
typedef enum
{
    WIFI_UPGRADE_FAIL_NONE = 0,
    WIFI_UPGRADE_FAIL_NOT_CONNECTED,
    WIFI_UPGRADE_FAIL_TCP,
    WIFI_UPGRADE_FAIL_TIMEOUT,
    WIFI_UPGRADE_FAIL_RESPONSE,
    WIFI_UPGRADE_FAIL_PARSE
} WifiUpgradeFailReason_t;

#define WIFI_WEATHER_FORECAST_DAYS 3U

/* 初始化 ESP8266 串口和非阻塞 AT 状态机。 */
void WifiManager_Init(void);

/* 周期性消费串口数据并推进 AT 状态机。 */
void WifiManager_Process(void);

/* 请求重新执行基础 AT 检测和联网状态查询。 */
void WifiManager_RequestRefresh(void);

/* 启动一次附近 WiFi 扫描；扫描期间不会覆盖顶部联网状态。 */
void WifiManager_StartScan(void);

/* 返回扫描命令是否仍在排队或等待 ESP8266 响应。 */
uint8_t WifiManager_IsScanBusy(void);

/* 返回最近一次扫描是否因 ERROR 或超时失败。 */
uint8_t WifiManager_IsScanFailed(void);

/* 使用指定 SSID 和密码启动一次异步连接。 */
uint8_t WifiManager_Connect(const char *ssid, const char *password);

/* 返回当前连接状态。 */
WifiState_t WifiManager_GetState(void);

/* 返回当前已连接网络的 SSID。 */
const char *WifiManager_GetSsid(void);

/* 返回 ESP8266 当前获得的 IPv4 地址。 */
const char *WifiManager_GetIp(void);

/* 返回当前连接信号强度，单位为 dBm。 */
int16_t WifiManager_GetRssi(void);

/* 返回扫描结果数量。 */
uint8_t WifiManager_GetScanCount(void);

/* 按索引返回一条扫描结果，越界时返回 NULL。 */
const WifiScanResult_t *WifiManager_GetScanResult(uint8_t index);

/* 进入或退出 AT 诊断独占模式，避免普通状态机抢占 USART1。 */
void WifiManager_SetAtTestMode(uint8_t enabled);

/* 请求通过当前已连接的 WiFi 获取一次 Open-Meteo 当前温度。 */
void WifiManager_RequestWeather(void);

/* 返回天气请求是否仍在进行。 */
uint8_t WifiManager_IsWeatherBusy(void);

/* 返回天气请求是否成功得到并解析了温度。 */
uint8_t WifiManager_IsWeatherReady(void);

/* 返回最近一次天气请求是否失败。 */
uint8_t WifiManager_IsWeatherFailed(void);

/* 返回解析得到的摄氏温度，单位为摄氏度。 */
float WifiManager_GetWeatherTemperature(void);

/* 返回当前体感温度，单位为摄氏度。 */
float WifiManager_GetWeatherApparentTemperature(void);

/* 返回当前相对湿度，单位为百分比。 */
uint8_t WifiManager_GetWeatherHumidity(void);

/* 返回当前十米风速，单位为米每秒。 */
float WifiManager_GetWeatherWindSpeed(void);

/* 返回当前美国空气质量指数 US AQI。 */
int16_t WifiManager_GetWeatherAqi(void);

/* 返回天气接口返回的 WMO 天气代码。 */
int16_t WifiManager_GetWeatherCode(void);

/* 返回指定日期的预报记录，索引为 0 至 WIFI_WEATHER_FORECAST_DAYS - 1。 */
const WifiWeatherForecast_t *WifiManager_GetWeatherForecast(uint8_t index);

/* 返回天气接口返回的 ISO 时间文本。 */
const char *WifiManager_GetWeatherTime(void);

/* Request and validate the local OTA server manifest asynchronously. */
uint8_t WifiManager_RequestUpgradeManifest(void);
uint8_t WifiManager_IsUpgradeManifestBusy(void);
uint8_t WifiManager_IsUpgradeManifestReady(void);
uint8_t WifiManager_IsUpgradeManifestFailed(void);
WifiUpgradeFailReason_t WifiManager_GetUpgradeFailReason(void);
const WifiUpgradeManifest_t *WifiManager_GetUpgradeManifest(void);

/* Request the local OTA server to start the Bootloader transfer asynchronously. */
uint8_t WifiManager_RequestUpgradeStart(void);
uint8_t WifiManager_IsUpgradeStartBusy(void);
uint8_t WifiManager_IsUpgradeStartReady(void);
uint8_t WifiManager_IsUpgradeStartFailed(void);

#endif /* __WIFI_MANAGER_H__ */
