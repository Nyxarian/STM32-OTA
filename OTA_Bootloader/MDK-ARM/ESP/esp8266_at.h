#ifndef ESP8266_AT_H
#define ESP8266_AT_H

#include <stdbool.h>
#include <stdint.h>

/* 发送一条 AT 命令，并在指定时间内等待期望响应文本。 */
bool Esp8266At_SendCommand(const char *command, const char *expected, uint32_t timeout_ms);

/* 发送基础 AT 命令，检查 ESP8266 是否在线。 */
bool Esp8266At_Test(uint32_t timeout_ms);

/* 关闭 ESP8266 的命令回显，避免解析时收到重复命令文本。 */
bool Esp8266At_DisableEcho(uint32_t timeout_ms);

/* 设置 ESP8266 为普通传输模式。 */
bool Esp8266At_SetNormalMode(uint32_t timeout_ms);

/* 设置 TCP 主动接收模式，让 ESP8266 通过 +IPD 直接把数据送到 STM32。 */
bool Esp8266At_SetActiveReceiveMode(uint32_t timeout_ms);

/* 开启 ESP8266 的多连接模式。 */
bool Esp8266At_EnableMultipleConnections(uint32_t timeout_ms);

/* 设置是否在接收数据通知中附加远端 IP 和端口信息。 */
bool Esp8266At_SetIpdInfo(uint8_t enable, uint32_t timeout_ms);

/* 设置 TCP 服务器允许的最大连接数量。 */
bool Esp8266At_SetServerMaxConnections(uint8_t max_connections, uint32_t timeout_ms);

/* 在指定端口启动 TCP 服务器。 */
bool Esp8266At_StartTcpServer(uint16_t port, uint32_t timeout_ms);

/* 停止当前已经启动的 TCP 服务器。 */
bool Esp8266At_StopTcpServer(uint32_t timeout_ms);

/*
 * 使用 esp8266_config.h 中的预存凭据确保模块已联网，并启动 TCP Server。
 * station_ip 可传入 NULL；非 NULL 时会返回模块当前获取到的 IPv4 地址。
 */
bool Esp8266At_PrepareWifiTcpServer(uint16_t port,
                                    char *station_ip,
                                    uint16_t station_ip_size,
                                    uint32_t timeout_ms);

/*
 * 兼容旧调用接口：只启动 TCP Server，不向调用方返回 IP 地址。
 * 新代码应优先使用 Esp8266At_PrepareWifiTcpServer()。
 */
bool Esp8266At_PrepareTcpServer(uint16_t port, uint32_t timeout_ms);

#endif /* ESP8266_AT_H */
