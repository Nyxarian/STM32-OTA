/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : FreeRTOS 应用任务配置
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "lvgl.h"
#include "lv_port_indev.h"                                                                                    
#include "lv_port_lcd_stm32.h"
#include "ui_app.h"
#include "ui_nav.h"
#include "BSP_Key.h"
#include "settings_page.h"
#include "wifi_manager.h"
#include "wifi_at_test.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define UI_TASK_PERIOD_MS 5U

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

/* USER CODE END Variables */
/* Definitions for uiTask */
osThreadId_t uiTaskHandle;
const osThreadAttr_t uiTask_attributes = {
  .name = "uiTask",
  .stack_size = 1024 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* 根据设置页返回的连发模式，转换为按键重复触发的时间间隔。 */
static uint32_t ui_key_repeat_interval_ms(uint8_t mode);

/* USER CODE END FunctionPrototypes */

void StartUiTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of uiTask */
  uiTaskHandle = osThreadNew(StartUiTask, NULL, &uiTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartUiTask */
/**
  * @brief  LVGL 界面任务入口。
  * @param  argument: 未使用。
  * @retval 无返回值。
  */
/* USER CODE END Header_StartUiTask */
void StartUiTask(void *argument)
{
  /* USER CODE BEGIN StartUiTask */
  BSP_Key_t key = BSP_KEY_NONE;                 // 保存本轮读取到的稳定按键
  static BSP_Key_t last_key = BSP_KEY_NONE;     // 保存上一轮按键，用于识别新按下动作
  static uint32_t key_hold_ms = 0U;             // 累计当前按键已经保持按下的时间
  static uint8_t key_input_armed = 1U;          // 默认允许按键输入，避免启动时低电平导致永久锁死
  uint32_t repeat_interval_ms = 0U;             // 保存当前设置对应的连发周期

  (void)argument;

  lv_init();
  lv_tick_set_cb(HAL_GetTick);
  if (!lv_port_disp_init())
  {
    Error_Handler();
  }
  lv_port_indev_init();
  ui_app_init();
  WifiManager_Init(); // 初始化 ESP8266 的 USART1 接收和 WiFi 状态机。
  WifiAtTest_Init(); // 初始化 AT 诊断状态；默认不接管普通 WiFi 串口。

  /* LVGL 只能由该任务调用，其他任务后续通过消息队列通知界面。 */
  for(;;)
  {
    WifiManager_Process(); // 持续处理 ESP8266 返回数据、扫描结果和命令超时。
    WifiAtTest_Process(); // AT 测试页面打开时，显示 ESP8266 的原始回包和 API 结果。
    lv_timer_handler();
    osDelay(UI_TASK_PERIOD_MS);

    /* 每轮只在这里扫描一次实体按键，其他模块只读取消抖后的缓存结果。 */
    BSP_Key_Update();

    /*
     * 按键轮询策略：
     * 1. 首次按下立即分发一次；
     * 2. 按住不放时，按照“按键连发”设置周期重复分发；
     * 3. 设置为关闭时保持原来的单次触发行为。
     */
    key = BSP_Key_Read(); // 读取已经完成消抖的稳定按键状态

    if (key == BSP_KEY_NONE) // 判断当前是否已经释放全部按键
    {
      key_input_armed = 1U; // 上电后首次确认全部释放，才允许接收按键事件
      key_hold_ms = 0U; // 释放按键后清空本次长按计时
      last_key = BSP_KEY_NONE; // 释放按键后允许下一次按下立即触发
      continue; // 当前没有按键，无需再进行连发计算
    }

    if (key_input_armed == 0U) // 防止上电瞬间的低电平误触发解锁
    {
      continue; // 等待按键全部释放后再开始处理按键
    }

    if (key != last_key) // 判断当前按键是否是一次新的按下动作
    {
      ui_nav_handle_key(key); // 新按下的按键立即分发给当前页面
      key_hold_ms = 0U; // 新按键从零开始统计长按时间
    }
    else
    {
      key_hold_ms += UI_TASK_PERIOD_MS; // 同一个按键持续按下时累加按住时间
      repeat_interval_ms = ui_key_repeat_interval_ms(Settings_GetKeyRepeatMode()); // 读取全局设置对应的连发周期

      if ((repeat_interval_ms > 0U) && (key_hold_ms >= repeat_interval_ms)) // 判断是否开启连发且已到达下一次重复时间
      {
        ui_nav_handle_key(key); // 将长按产生的重复按键分发给当前页面
        key_hold_ms = 0U; // 重新开始计算下一次连发间隔
      }
    }

    last_key = key; // 保存本轮按键，为下一轮的按下沿判断做准备
  }
  /* USER CODE END StartUiTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/*
 * 根据设置页中的“按键连发”选项返回重复触发周期。
 * 模式 0 表示关闭连发；模式 1、2、3 分别表示慢、正常、快。
 */
static uint32_t ui_key_repeat_interval_ms(uint8_t mode)
{
  switch (mode)
  {
    case 1U:
      return 500U;  // 慢速连发：每 500 ms 触发一次

    case 2U:
      return 250U;  // 正常连发：每 250 ms 触发一次

    case 3U:
      return 100U;  // 快速连发：每 100 ms 触发一次

    default:
      return 0U;    // 关闭连发：只响应第一次按下
  }
}

/* USER CODE END Application */

