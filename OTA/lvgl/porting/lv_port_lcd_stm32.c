/* ST7789 显示移植：SPI1 发送命令，16 位 DMA 发送 RGB565 像素。 */
#include "lv_port_lcd_stm32.h"
#include "src/drivers/display/st7789/lv_st7789.h"
#include "main.h"
#include "spi.h"

/* LCD 物理方向为 240x320，LVGL 旋转后使用 320x240 横屏。 */
#define LCD_PHYSICAL_HOR_RES     240U
#define LCD_PHYSICAL_VER_RES     320U
#define LCD_LOGICAL_HOR_RES      320U
#define LCD_DRAW_BUF_LINES        20U
#define LCD_BYTES_PER_PIXEL        2U
#define LCD_DRAW_BUF_SIZE        (LCD_LOGICAL_HOR_RES * LCD_DRAW_BUF_LINES * LCD_BYTES_PER_PIXEL)
#define LCD_SPI_TIMEOUT_MS       1000U

/*
 * 背光使用 10 个软件 PWM 相位。
 *
 * TIM7 以 2 kHz 进入一次中断，
 * 10 个相位组成一个完整周期，因此最终 PWM 频率为 200 Hz。
 */
#define LCD_BACKLIGHT_PWM_STEPS       10U
#define LCD_BACKLIGHT_TIMER_PSC       83U
#define LCD_BACKLIGHT_TIMER_ARR       499U

/* 两块 20 行局部缓冲交替绘制，避免占用全屏帧缓冲。 */
__ALIGNED(4) static uint8_t draw_buf_1[LCD_DRAW_BUF_SIZE];
__ALIGNED(4) static uint8_t draw_buf_2[LCD_DRAW_BUF_SIZE];

static lv_display_t *lcd_display;
static volatile uint8_t lcd_bus_busy;
static volatile uint8_t lcd_backlight_duty_steps = LCD_BACKLIGHT_PWM_STEPS; /* 当前背光占空比档位 */
static volatile uint8_t lcd_backlight_phase; /* 当前软件 PWM 相位 */

/*
 * 初始化用于背光软件 PWM 的 TIM7。
 *
 * 系统当前 APB1 定时器时钟为 84 MHz：
 * 84 MHz / (83 + 1) / (499 + 1) = 2 kHz。
 */
static void lcd_backlight_pwm_init(void)
{
  __HAL_RCC_TIM7_CLK_ENABLE(); // 开启 TIM7 外设时钟

  TIM7->CR1 = 0U; // 配置前先停止定时器并清空控制寄存器
  TIM7->PSC = LCD_BACKLIGHT_TIMER_PSC; // 将计数时钟分频到 1 MHz
  TIM7->ARR = LCD_BACKLIGHT_TIMER_ARR; // 每 500 个计数产生一次更新中断
  TIM7->CNT = 0U; // 从第零个计数开始运行
  TIM7->EGR = TIM_EGR_UG; // 立即把预分频和自动重装值装入工作寄存器
  TIM7->SR = 0U; // 清除初始化过程中产生的更新标志
  TIM7->DIER = TIM_DIER_UIE; // 只开启更新中断

  HAL_NVIC_SetPriority(TIM7_IRQn, 15U, 0U); // 使用最低优先级，避免影响刷屏和 RTOS
  HAL_NVIC_EnableIRQ(TIM7_IRQn); // 在 NVIC 中开启 TIM7 中断

  lcd_backlight_phase = 0U; // 软件 PWM 从第一个相位开始
  TIM7->CR1 = TIM_CR1_CEN; // 启动 TIM7 连续计数
}

/*
 * 设置背光亮度百分比。
 *
 * 百分比会转换为 0～10 个点亮相位；
 * 设置页当前提供 20%、40%、60%、80%、100% 五档，
 * 因此转换结果不会产生额外量化误差。
 */
void lv_port_lcd_set_brightness(uint8_t percent)
{
  uint8_t duty_steps; // 保存百分比转换后的软件 PWM 点亮相位数

  if (percent > 100U) // 防止调用者传入超过百分比上限的数值
  {
    percent = 100U; // 将非法大值限制为满亮度
  }

  duty_steps = (uint8_t)((percent + 9U) / 10U); // 将百分比向上换算为 0～10 档占空比
  lcd_backlight_duty_steps = duty_steps; // 原子更新中断读取的八位占空比

  if (duty_steps == 0U) // 判断是否需要完全关闭背光
  {
    LCD_BLK_GPIO_Port->BSRR = ((uint32_t)LCD_BLK_Pin << 16U); // 立即拉低背光引脚
  }
  else if (duty_steps >= LCD_BACKLIGHT_PWM_STEPS) // 判断是否设置为 100% 常亮
  {
    LCD_BLK_GPIO_Port->BSRR = LCD_BLK_Pin; // 立即拉高背光引脚
  }
}

/*
 * TIM7 软件 PWM 中断处理。
 *
 * 本函数只清标志、更新相位并写一次 GPIO BSRR，
 * 不调用 HAL 和 RTOS 接口，保持中断执行时间稳定。
 */
void lv_port_lcd_backlight_irq_handler(void)
{
  uint8_t duty_steps; // 保存本次中断使用的占空比快照

  if ((TIM7->SR & TIM_SR_UIF) == 0U) // 判断是否确实发生了更新中断
  {
    return; // 没有更新标志时不执行 PWM
  }

  TIM7->SR &= ~TIM_SR_UIF; // 清除 TIM7 更新中断标志
  duty_steps = lcd_backlight_duty_steps; // 读取当前占空比档位

  if (lcd_backlight_phase < duty_steps) // 判断当前相位是否位于点亮区间
  {
    LCD_BLK_GPIO_Port->BSRR = LCD_BLK_Pin; // 原子拉高背光 GPIO
  }
  else
  {
    LCD_BLK_GPIO_Port->BSRR = ((uint32_t)LCD_BLK_Pin << 16U); // 原子拉低背光 GPIO
  }

  lcd_backlight_phase++; // 进入下一个软件 PWM 相位
  if (lcd_backlight_phase >= LCD_BACKLIGHT_PWM_STEPS) // 判断是否完成一个 PWM 周期
  {
    lcd_backlight_phase = 0U; // 完成一周期后回到第一个相位
  }
}

static bool lcd_spi_set_data_size(uint32_t data_size)
{
  /* 命令阶段使用 8 位，像素阶段使用 16 位以保持 RGB565 字节顺序。 */
  if (hspi1.Init.DataSize == data_size)
  {
    return true;
  }

  hspi1.Init.DataSize = data_size;
  return HAL_SPI_Init(&hspi1) == HAL_OK;
}

static void lcd_release_bus(void)
{
  HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_SET);
  lcd_bus_busy = 0U;
}

static void lcd_wait_bus_idle(void)
{
  /* 等待上一笔 DMA 刷屏完成后再操作 SPI。 */
  while (lcd_bus_busy != 0U)
  {
    __NOP();
  }
}

static void lcd_send_cmd(lv_display_t *display, const uint8_t *cmd, size_t cmd_size,
                         const uint8_t *param, size_t param_size)
{
  /* ST7789 命令和短参数采用轮询发送。 */
  LV_UNUSED(display);

  lcd_wait_bus_idle();
  if (!lcd_spi_set_data_size(SPI_DATASIZE_8BIT))
  {
    return;
  }

  HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_RESET);

  if (HAL_SPI_Transmit(&hspi1, (uint8_t *)cmd, (uint16_t)cmd_size, LCD_SPI_TIMEOUT_MS) == HAL_OK)
  {
    HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, GPIO_PIN_SET);
    if (param_size > 0U)
    {
      (void)HAL_SPI_Transmit(&hspi1, (uint8_t *)param, (uint16_t)param_size, LCD_SPI_TIMEOUT_MS);
    }
  }

  HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_SET);
}

static void lcd_send_color(lv_display_t *display, const uint8_t *cmd, size_t cmd_size,
                           uint8_t *param, size_t param_size)
{
  /* 大块像素数据采用 DMA 发送，完成后在中断回调中通知 LVGL。 */
  LV_UNUSED(display);

  lcd_wait_bus_idle();
  if (!lcd_spi_set_data_size(SPI_DATASIZE_8BIT))
  {
    lv_display_flush_ready(lcd_display);
    return;
  }

  HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_RESET);

  if (HAL_SPI_Transmit(&hspi1, (uint8_t *)cmd, (uint16_t)cmd_size, LCD_SPI_TIMEOUT_MS) != HAL_OK)
  {
    lcd_release_bus();
    lv_display_flush_ready(lcd_display);
    return;
  }

  HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, GPIO_PIN_SET);
  if (!lcd_spi_set_data_size(SPI_DATASIZE_16BIT))
  {
    lcd_release_bus();
    lv_display_flush_ready(lcd_display);
    return;
  }

  lcd_bus_busy = 1U;
  if (HAL_SPI_Transmit_DMA(&hspi1, param, (uint16_t)(param_size / 2U)) != HAL_OK)
  {
    lcd_release_bus();
    lv_display_flush_ready(lcd_display);
  }
}

bool lv_port_disp_init(void)
{
  /* 初始化期间关闭背光，避免屏幕显示未刷新显存。 */
  HAL_GPIO_WritePin(LCD_BLK_GPIO_Port, LCD_BLK_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, GPIO_PIN_SET);

  HAL_GPIO_WritePin(LCD_RST_GPIO_Port, LCD_RST_Pin, GPIO_PIN_RESET);
  HAL_Delay(100U);
  HAL_GPIO_WritePin(LCD_RST_GPIO_Port, LCD_RST_Pin, GPIO_PIN_SET);
  HAL_Delay(100U);

  lcd_display = lv_st7789_create(LCD_PHYSICAL_HOR_RES, LCD_PHYSICAL_VER_RES,
                                 LV_LCD_FLAG_NONE, lcd_send_cmd, lcd_send_color);
  if (lcd_display == NULL)
  {
    return false;
  }

  lv_display_set_rotation(lcd_display, LV_DISPLAY_ROTATION_270);
  lv_display_set_buffers(lcd_display, draw_buf_1, draw_buf_2, sizeof(draw_buf_1),
                         LV_DISPLAY_RENDER_MODE_PARTIAL);

  lcd_backlight_pwm_init(); // 启动 TIM7 软件 PWM
  lv_port_lcd_set_brightness(100U); // 设置模块初始化前先以满亮度显示
  return true;
}

void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi)
{
  /* DMA 发送完成：释放片选并通知 LVGL 当前区域已经刷新。 */
  if (hspi->Instance == SPI1 && lcd_bus_busy != 0U)
  {
    lcd_release_bus();
    lv_display_flush_ready(lcd_display);
  }
}

void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi)
{
  /* DMA 异常时同样释放总线，防止 LVGL 永久等待。 */
  if (hspi->Instance == SPI1 && lcd_bus_busy != 0U)
  {
    lcd_release_bus();
    lv_display_flush_ready(lcd_display);
  }
}
