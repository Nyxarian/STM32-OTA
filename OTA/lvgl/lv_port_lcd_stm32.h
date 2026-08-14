/* STM32F407 + ST7789 的 LVGL 显示接口。 */
#ifndef LV_PORT_LCD_STM32_H
#define LV_PORT_LCD_STM32_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

/* 初始化 LCD、显示对象和局部双缓冲。 */
bool lv_port_disp_init(void);

/*
 * 设置 LCD 背光亮度，取值范围为 0～100。
 *
 * 当前硬件的 LCD_BLK 位于普通 GPIO，
 * 驱动内部使用 TIM7 中断产生软件 PWM。
 */
void lv_port_lcd_set_brightness(uint8_t percent);

/* 由 TIM7_IRQHandler() 调用的软件 PWM 中断处理入口。 */
void lv_port_lcd_backlight_irq_handler(void);

#ifdef __cplusplus
}
#endif

#endif /* LV_PORT_LCD_STM32_H */
