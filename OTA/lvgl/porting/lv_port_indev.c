/* 将六个实体按键接入 LVGL 按键输入设备和焦点组。 */
#include "lv_port_indev.h"
#include "BSP_Key.h"

#define KEYPAD_READ_PERIOD_MS   10U
#define KEYPAD_LONG_PRESS_MS   400U
#define KEYPAD_REPEAT_MS       100U

static lv_indev_t *keypad_indev;
static lv_group_t *keypad_group;
static uint8_t keypad_suppress_until_release;

static uint32_t keypad_map_key(BSP_Key_t key)
{
  /* 非编辑状态下，上下键切换焦点；进入编辑状态后作为控件方向键。 */
  switch (key)
  {
    case BSP_KEY_UP:
      return (keypad_group != NULL && lv_group_get_editing(keypad_group)) ? LV_KEY_UP : LV_KEY_PREV;

    case BSP_KEY_DOWN:
      return (keypad_group != NULL && lv_group_get_editing(keypad_group)) ? LV_KEY_DOWN : LV_KEY_NEXT;

    case BSP_KEY_LEFT:
      return LV_KEY_LEFT;

    case BSP_KEY_RIGHT:
      return LV_KEY_RIGHT;

    case BSP_KEY_ENTER:
      return LV_KEY_ENTER;

    case BSP_KEY_ESC:
      return LV_KEY_ESC;

    default:
      return 0U;
  }
}

static void keypad_read(lv_indev_t *indev, lv_indev_data_t *data)
{
  /* LVGL 要求松开时仍保留最后一个有效键值。 */
  static uint32_t last_key = LV_KEY_ENTER;
  BSP_Key_t bsp_key;
  uint32_t key;

  LV_UNUSED(indev);

  bsp_key = BSP_Key_Read();

  /* 页面切换后，当前这次按下不能继续作用于新页面。 */
  if (keypad_suppress_until_release != 0U)
  {
    if (bsp_key == BSP_KEY_NONE)
    {
      keypad_suppress_until_release = 0U;
    }
    else
    {
      data->state = LV_INDEV_STATE_RELEASED;
      data->key = last_key;
      return;
    }
  }

  key = keypad_map_key(bsp_key);
  if (key != 0U)
  {
    last_key = key;
    data->state = LV_INDEV_STATE_PRESSED;
  }
  else
  {
    data->state = LV_INDEV_STATE_RELEASED;
  }

  data->key = last_key;
}

void lv_port_indev_init(void)
{
  /* 所有需要按键操作的控件都加入这个公共焦点组。 */
  keypad_group = lv_group_create();
  keypad_suppress_until_release = 0U;
  lv_group_set_wrap(keypad_group, true);
  lv_group_set_default(keypad_group);

  keypad_indev = lv_indev_create();
  lv_indev_set_type(keypad_indev, LV_INDEV_TYPE_KEYPAD);
  lv_indev_set_read_cb(keypad_indev, keypad_read);
  lv_timer_set_period(lv_indev_get_read_timer(keypad_indev), KEYPAD_READ_PERIOD_MS);
  lv_indev_set_long_press_time(keypad_indev, KEYPAD_LONG_PRESS_MS);
  lv_indev_set_long_press_repeat_time(keypad_indev, KEYPAD_REPEAT_MS);
  lv_indev_set_group(keypad_indev, keypad_group);
}

lv_indev_t *lv_port_indev_get_keypad(void)
{
  return keypad_indev;
}

lv_group_t *lv_port_indev_get_group(void)
{
  return keypad_group;
}

void lv_port_indev_suppress_key_until_release(void)
{
  keypad_suppress_until_release = 1U;
}
