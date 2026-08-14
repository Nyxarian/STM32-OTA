/* 六按键扫描与消抖实现。 */
#include "BSP_Key.h"
#include "main.h"

#define KEY_PRESS_SAMPLES    2U
#define KEY_RELEASE_SAMPLES  2U

/* 最近一次完成消抖的稳定按键，供所有读取者共享。 */
static BSP_Key_t bsp_key_stable = BSP_KEY_NONE;

static BSP_Key_t BSP_Key_ReadRaw(void)
{
  /* 引脚编号与参考遥控器工程保持一致：1上、2下、3左、4右、5确认、6返回。 */
  if (HAL_GPIO_ReadPin(KEY_1_GPIO_Port, KEY_1_Pin) == GPIO_PIN_RESET)
  {
    return BSP_KEY_UP;
  }
  if (HAL_GPIO_ReadPin(KEY_2_GPIO_Port, KEY_2_Pin) == GPIO_PIN_RESET)
  {
    return BSP_KEY_DOWN;
  }
  if (HAL_GPIO_ReadPin(KEY_3_GPIO_Port, KEY_3_Pin) == GPIO_PIN_RESET)
  {
    return BSP_KEY_LEFT;
  }
  if (HAL_GPIO_ReadPin(KEY_4_GPIO_Port, KEY_4_Pin) == GPIO_PIN_RESET)
  {
    return BSP_KEY_RIGHT;
  }
  if (HAL_GPIO_ReadPin(KEY_5_GPIO_Port, KEY_5_Pin) == GPIO_PIN_RESET)
  {
    return BSP_KEY_ENTER;
  }
  if (HAL_GPIO_ReadPin(KEY_6_GPIO_Port, KEY_6_Pin) == GPIO_PIN_RESET)
  {
    return BSP_KEY_ESC;
  }

  return BSP_KEY_NONE;
}

void BSP_Key_Update(void)
{
  /* 连续多次采样一致后才更新稳定状态，避免机械按键抖动。 */
  static BSP_Key_t candidate = BSP_KEY_NONE;
  static uint8_t sample_count = 0U;
  BSP_Key_t raw = BSP_Key_ReadRaw();
  uint8_t threshold;

  if (raw != candidate)
  {
    candidate = raw;
    sample_count = 1U;
  }
  else
  {
    threshold = (candidate == BSP_KEY_NONE) ? KEY_RELEASE_SAMPLES : KEY_PRESS_SAMPLES;
    if (sample_count < threshold)
    {
      sample_count++;
    }
    if (sample_count >= threshold)
    {
      bsp_key_stable = candidate;
    }
  }

}

BSP_Key_t BSP_Key_Read(void)
{
  /* 读取缓存结果，避免 LVGL、导航器和定时器重复推进消抖状态机。 */
  return bsp_key_stable;
}
