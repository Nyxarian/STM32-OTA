#ifndef ESP8266_CONFIG_H
#define ESP8266_CONFIG_H

/*
 * Bootloader 的最小 WiFi 配置。
 *
 * APP 与 Bootloader 是两个独立工程：软件复位进入 Bootloader 后，
 * APP 的 FreeRTOS 任务、WiFi 状态机和内存都不再运行。因此这里必须
 * 保留一份独立的联网凭据，供 Bootloader 自己恢复 ESP8266 的联网状态。
 *
 * 注意：ESP8266 仅支持 2.4 GHz WiFi；SSID 区分大小写。
 * 修改家庭网络时，请同步修改 APP 工程 Apps/Wifi/wifi_config.h 中的
 * WIFI_CONFIG_SSID 和 WIFI_CONFIG_PASSWORD，保持两层使用同一网络。
 */
/* 仓库中已脱敏：请替换为你自己的 2.4 GHz WiFi 名称和密码，
 * 并保持与 App 工程 Apps/Wifi/wifi_config.h 中的配置一致。 */
#define ESP8266_CONFIG_WIFI_SSID      ""
#define ESP8266_CONFIG_WIFI_PASSWORD  ""

#endif /* ESP8266_CONFIG_H */
