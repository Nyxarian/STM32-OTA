/* 六按键板级驱动：负责按键定义和消抖后的按键状态读取。 */
#ifndef BSP_KEY_H
#define BSP_KEY_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
  BSP_KEY_NONE = 0,
  BSP_KEY_UP,
  BSP_KEY_DOWN,
  BSP_KEY_LEFT,
  BSP_KEY_RIGHT,
  BSP_KEY_ENTER,
  BSP_KEY_ESC
} BSP_Key_t;

/* 每个 UI 任务周期调用一次，推进按键消抖状态机。 */
void BSP_Key_Update(void);

/* 读取最近一次消抖后的稳定按键状态，不会再次采样硬件。 */
BSP_Key_t BSP_Key_Read(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_KEY_H */
