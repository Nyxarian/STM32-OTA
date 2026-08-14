/*
 * 天气应用前端页面接口。
 *
 * 当前文件只描述页面创建和按键入口，不包含网络请求、ESP8266 控制、
 * JSON 解析或天气数据存储。后续接入真实天气数据时，只需要把静态演示
 * 数据替换成天气管理模块提供的数据，不需要改变导航器接口。
 */
#ifndef __WEATHER_PAGE_H__
#define __WEATHER_PAGE_H__

#include "lvgl.h"
#include "BSP_Key.h"

/* 创建天气应用页面。 */
void ui_page_weather_create(lv_obj_t *parent);

/* 处理天气应用页面收到的按键。 */
void ui_page_weather_on_key(BSP_Key_t key);

/* 离开天气页面时释放页面定时器和动态标签引用。 */
void ui_page_weather_leave(void);

#endif /* __WEATHER_PAGE_H__ */
