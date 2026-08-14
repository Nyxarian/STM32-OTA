/* LVGL 六按键输入设备接口。 */
#ifndef LV_PORT_INDEV_H
#define LV_PORT_INDEV_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

void lv_port_indev_init(void);
lv_indev_t *lv_port_indev_get_keypad(void);
lv_group_t *lv_port_indev_get_group(void);

/* 页面切换后屏蔽当前仍处于按下状态的按键，直到硬件报告释放。 */
void lv_port_indev_suppress_key_until_release(void);

#ifdef __cplusplus
}
#endif

#endif /* LV_PORT_INDEV_H */
