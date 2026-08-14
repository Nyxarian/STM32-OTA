/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
#include "main.h"
#include "spi.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "bootloader.h"
#include "iap_protocol.h"
#include "iap_uart.h"
#include "iap_frame.h"
#include "esp8266_at.h"
#include "boot_flag.h"
#include "boot_display.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART1_UART_Init();
  MX_SPI1_Init();
  /* USER CODE BEGIN 2 */

    bool tcp_upgrade_ready; /* 记录 ESP8266 是否已联网并启动 TCP 升级服务。 */
    bool update_requested; /* 记录 APP 是否通过备份寄存器明确请求进入升级模式。 */
    char station_ip[16]; /* 保存 ESP8266 获得的 IPv4；当前用于联网成功校验和后续扩展。 */

    /*
     * USART1 已物理连接 ESP8266，因此在联网初始化前不能发送 BOOT_WAIT、
     * UPDATE_REQUESTED 等普通文本；否则模块会把文本当作未知 AT 指令。
     */
    update_requested = BootFlag_IsUpdateRequested(); /* 先读取一次升级请求标志。 */
    if (update_requested)
    {
      BootFlag_Clear(); /* 读取后立即清除，防止后续意外复位永久停留 Bootloader。 */
    }
    else if (Bootloader_IsAppValid() && !IapUart_WaitForEnter(3000U))
    {
      Bootloader_JumpToApp();
    }

    (void)BootDisplay_Init();
    BootDisplay_ShowConnecting();

    /*
     * 进入升级模式后，Bootloader 自己恢复 ESP8266 的网络连接并启动
     * 5000 端口 TCP Server。APP 的 WiFi 状态机在软件复位后已不存在，
     * 所以不能由 APP 代替此处的联网过程。
     */
    tcp_upgrade_ready = Esp8266At_PrepareWifiTcpServer(5000U,
                                                        station_ip,
                                                        (uint16_t)sizeof(station_ip),
                                                        500U); /* 完成 AT 检测、连接 WiFi、获取 IP 和启动 Server。 */

    if (tcp_upgrade_ready)
    {
      BootDisplay_ShowWaiting();
      /*
       * USART1 此时连接 ESP8266，不能继续把普通 IAP 文本发往 UART，
       * 否则 ESP 会把它误认为 AT 命令。专用循环只处理 +IPD 中的真实
       * TCP 负载，ACK/NACK 也经原 TCP 连接返回电脑端发送工具。
       */
      IapProtocol_RunEsp8266();
    }

    BootDisplay_ShowFailed();

    /*
     * ESP8266 初始化失败时保留原有串口升级入口，便于将 ESP 模块暂时
     * 断开后通过 USB 转串口工具恢复应用。此分支才允许输出文本提示。
     */
    if (update_requested)
    {
      IapUart_SendText("\r\nUPDATE_REQUESTED\r\n"); /* 仅在 ESP 初始化失败的串口恢复场景下输出升级请求状态。 */
    }
    IapUart_SendText("\r\nIAP_READY\r\n");
    IapProtocol_Run();

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    /* 没有检测到合法主应用时停留在 Bootloader，等待后续升级功能。 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 168;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
