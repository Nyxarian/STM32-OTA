#ifndef ESP8266_TCP_H
#define ESP8266_TCP_H

#include <stdbool.h>
#include <stdint.h>

#define ESP8266_TCP_MAX_IPD_LENGTH 2048U // 单条 +IPD 数据允许的最大长度。
#define ESP8266_TCP_MAX_LINK_ID    4U    // ESP8266 多连接模式允许的最大连接编号。

typedef void (*Esp8266Tcp_DataHandler_t)(uint8_t link_id,
                                         uint8_t byte);

typedef void (*Esp8266Tcp_ConnectHandler_t)(uint8_t link_id);

/* 初始化 +IPD 解析状态机，并保存 TCP 数据回调函数。 */
void Esp8266Tcp_Init(Esp8266Tcp_DataHandler_t data_handler);

/* 保存 TCP 客户端连接建立后的通知回调。 */
void Esp8266Tcp_SetConnectHandler(Esp8266Tcp_ConnectHandler_t connect_handler);

/*
 * 分发一条已经解析完成的 TCP 连接建立事件。
 *
 * +IPD 解析器只负责识别 ESP8266 输出的 "n,CONNECT" 并记录事件，
 * 不会在逐字节解析过程中直接执行 AT+CIPSEND。调用方应在主循环中
 * 调用本函数，使欢迎包发送与串口解析彻底分离。
 *
 * 返回 true 表示本次实际分发了一条连接事件。
 */
bool Esp8266Tcp_DispatchConnectEvent(void);

/*
 * 分发一条已经完整接收的 TCP 数据包。
 *
 * 函数会根据当前模式执行原样回显，或逐字节调用已注册的数据处理函数。
 * 解析器完成 +IPD 包后只将数据放入内部缓存；调用方必须在主循环中调用
 * 本函数，才能执行后续的回显、协议处理或 TCP 回包。
 *
 * 返回 true 表示本次实际分发了一条数据包。
 */
bool Esp8266Tcp_DispatchReceivedPacket(void);

/*
 * 开启或关闭临时的 TCP 原样回显测试。
 * 开启后收到的完整 TCP 数据包会直接原样返回，不进入 IAP 协议层。
 */
void Esp8266Tcp_EnableRawEcho(bool enable);

/* 输入一个来自 ESP8266 USART 的字节，并判断它是否属于 +IPD 的有效 TCP 数据。 */
bool Esp8266Tcp_ProcessByte(uint8_t byte);

/* 通过指定 TCP 连接发送一段数据，并等待 ESP8266 返回发送结果。 */
bool Esp8266Tcp_Send(uint8_t link_id, const uint8_t *data, uint16_t length, uint32_t timeout_ms);

#endif
