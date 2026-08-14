#ifndef __WIFI_CONFIG_H__
#define __WIFI_CONFIG_H__

/*
 * STM32 侧自动联网参数。
 *
 * 这两个宏就是以后不使用电脑串口工具时的唯一配置入口。
 * 把字符串替换成你自己的 2.4 GHz WiFi 名称和密码即可。
 * ESP8266 常见固件不支持 5 GHz WiFi，请确保路由器提供 2.4 GHz 网络。
 */
/* 仓库中已脱敏：请把下面两个宏替换为你自己的 2.4 GHz WiFi 名称和密码。
 * 留空字符串表示不自动联网，可在设备 WiFi 页面手动输入。 */
#define WIFI_CONFIG_SSID     ""
#define WIFI_CONFIG_PASSWORD ""

/* Computer that passively publishes OTA files on the local network. */
#define WIFI_CONFIG_OTA_SERVER_HOST    "192.168.0.101"
#define WIFI_CONFIG_OTA_SERVER_PORT    8080U
#define WIFI_CONFIG_OTA_MANIFEST_PATH  "/ota/manifest.txt"
#define WIFI_CONFIG_OTA_FIRMWARE_PATH  "/ota/firmware.bin"
#define WIFI_CONFIG_OTA_UPGRADE_PATH   "/ota/upgrade"
#define WIFI_CONFIG_OTA_PRODUCT        "550W_F407"
#define WIFI_CONFIG_OTA_APP_MAX_SIZE   458720UL

#endif
