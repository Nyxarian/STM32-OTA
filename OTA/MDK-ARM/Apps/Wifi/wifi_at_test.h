#ifndef __WIFI_AT_TEST_H__
#define __WIFI_AT_TEST_H__

#include <stdint.h>

/* 初始化 AT 测试模块；系统启动时调用一次。 */
void WifiAtTest_Init(void);

/* 进入或离开 AT 测试；进入后测试模块独占 ESP8266 的 USART1。 */
void WifiAtTest_Enter(void);
void WifiAtTest_Leave(void);

/* UI 任务周期调用，接收并保存 ESP8266 的原始返回。 */
void WifiAtTest_Process(void);

/* 左右键选择测试步骤，确认键发送当前步骤，上键清空原始返回。 */
void WifiAtTest_Select(int8_t step);
void WifiAtTest_SendSelected(void);
void WifiAtTest_ClearResponse(void);

/* 以下查询函数只读内部状态，供 LCD 测试页面显示。 */
uint8_t WifiAtTest_IsBusy(void);
uint8_t WifiAtTest_GetSelectedIndex(void);
uint8_t WifiAtTest_GetCommandCount(void);
const char *WifiAtTest_GetStepName(void);
const char *WifiAtTest_GetCommandText(void);
const char *WifiAtTest_GetStatusText(void);
const char *WifiAtTest_GetResponseLine(uint8_t line_index);
uint32_t WifiAtTest_GetRevision(void);

#endif /* __WIFI_AT_TEST_H__ */
