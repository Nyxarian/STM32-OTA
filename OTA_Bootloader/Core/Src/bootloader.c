/* STM32F407 最小 Bootloader 跳转实现。 */
#include "bootloader.h"
#include "main.h"
#include "iap_flash.h"

#define BOOTLOADER_SRAM_START        0x20000000UL
#define BOOTLOADER_SRAM_END          0x20020000UL
#define BOOTLOADER_NVIC_REG_COUNT    8U

typedef void (*Bootloader_AppEntry_t)(void);

static uint32_t Bootloader_GetAppStack(void)
{
  return *(volatile const uint32_t *)BOOTLOADER_APP_ADDRESS;
}

static uint32_t Bootloader_GetAppResetHandler(void)
{
  return *(volatile const uint32_t *)(BOOTLOADER_APP_ADDRESS + sizeof(uint32_t));
}

/**
 * @brief 计算指定地址开始的指定长度字节的CRC32校验和.
 * 
 * @param address 要计算CRC验和的起始地址.
 * @param length 要计算CRC验和的字节数.
 * @return 计算得到的CRC32校验和.
 */
static uint32_t Bootloader_CalculateCrc32(uint32_t address, uint32_t length)
{
  volatile const uint8_t *data;
  uint32_t crc = 0xFFFFFFFFU;
  uint32_t index;
  uint8_t bit;

  data = (volatile const uint8_t *)address;

  for (index = 0U; index < length; index++)
  {
    crc ^= (uint32_t)data[index];

    for (bit = 0U; bit < 8U; bit++)
    {
      if ((crc & 1U) != 0U)
      {
        crc = (crc >> 1U) ^ 0xEDB88320U;
      }
      else
      {
        crc >>= 1U;
      }
    }
  }

  return crc ^ 0xFFFFFFFFU;
}

/**
 * @brief 检查主应用图像信息是否有效
 * 
 * @return true 有效
 * @return false 无效
 */
static bool Bootloader_IsImageInfoValid(void)
{
  uint32_t image_state;
  uint32_t magic;
  uint32_t image_length;
  uint32_t image_crc32;
  uint32_t commit;
  uint32_t calculated_crc32;

  image_state = IapFlash_GetImageState();

  /*
   * 直接烧录(SWD/JTAG)的工厂固件不会写入 OTA 状态区，
   * 因此状态字保持擦除态 0xFFFFFFFF。
   * 此时视为"工厂烧录",信任向量表检查(Bootloader_AreAppVectorsValid),
   * 跳过整包 CRC32 校验,否则直接烧录的 App 永远不会被跳转。
   *
   * OTA 擦除完成后会先写入 UPDATING。只要状态不是 COMMITTED，
   * 就算 APP 向量表已经被部分写入，也绝不能跳转到这个残缺镜像。
   */
  if (image_state == IAP_FLASH_IMAGE_STATE_EMPTY)
  {
    return true;
  }

  if (image_state != IAP_FLASH_IMAGE_STATE_COMMITTED)
  {
    return false;
  }

  magic = *(volatile const uint32_t *)IAP_FLASH_IMAGE_INFO_ADDRESS;

  image_length = *(volatile const uint32_t *)
      (IAP_FLASH_IMAGE_INFO_ADDRESS + 4U);
  image_crc32 = *(volatile const uint32_t *)
      (IAP_FLASH_IMAGE_INFO_ADDRESS + 8U);
  commit = *(volatile const uint32_t *)
      (IAP_FLASH_IMAGE_INFO_ADDRESS + 12U);

  if ((magic != IAP_FLASH_IMAGE_MAGIC) ||
      (commit != IAP_FLASH_IMAGE_COMMIT))
  {
    return false;
  }

  if ((image_length == 0U) ||
      ((image_length % 4U) != 0U) ||
      (image_length > (IAP_FLASH_APP_END - IAP_FLASH_APP_START)))
  {
    return false;
  }

  calculated_crc32 = Bootloader_CalculateCrc32(
      BOOTLOADER_APP_ADDRESS,
      image_length);

  return calculated_crc32 == image_crc32;
}

/**
 * @brief 检查主应用是否有效
 * 
 * @return true 有效
 * @return false 无效
 */
bool Bootloader_AreAppVectorsValid(void)
{
  uint32_t app_stack = Bootloader_GetAppStack();
  uint32_t app_reset = Bootloader_GetAppResetHandler();
  uint32_t app_reset_address = app_reset & ~1UL;
  bool stack_valid;
  bool reset_valid;
  bool thumb_valid;

  /* 初始 MSP 允许指向 SRAM 顶部的边界地址。 */
  stack_valid = (app_stack >= BOOTLOADER_SRAM_START) &&
                (app_stack <= BOOTLOADER_SRAM_END) &&
                ((app_stack & 0x3UL) == 0UL);

  /* 复位入口必须位于为主应用保留的 Flash 区域。 */
  reset_valid = (app_reset_address >= BOOTLOADER_APP_ADDRESS) &&
                (app_reset_address < BOOTLOADER_APP_FLASH_END);

  /* Cortex-M 的函数入口最低位必须为 1，表示 Thumb 状态。 */
  thumb_valid = (app_reset & 0x1UL) != 0UL;

  return stack_valid && reset_valid && thumb_valid;
}

bool Bootloader_IsAppValid(void)
{
  if (!Bootloader_AreAppVectorsValid())
  {
    return false;
  }

  return Bootloader_IsImageInfoValid();
}

/**
 * @brief 跳转到主应用
 * 
 * 如果主应用无效，不会跳转。
 */
void Bootloader_JumpToApp(void)
{
  uint32_t app_stack;
  uint32_t app_reset;
  Bootloader_AppEntry_t app_entry;
  uint32_t index;

  if (!Bootloader_IsAppValid())
  {
    return;
  }

  app_stack = Bootloader_GetAppStack();
  app_reset = Bootloader_GetAppResetHandler();
  app_entry = (Bootloader_AppEntry_t)app_reset;

  /* 恢复时钟和 HAL 外设状态，主应用随后会重新初始化。 */
  (void)HAL_RCC_DeInit();
  (void)HAL_DeInit();

  __disable_irq();

  /* 关闭 SysTick，避免跳转过程中产生 Bootloader 的时基中断。 */
  SysTick->CTRL = 0UL;
  SysTick->LOAD = 0UL;
  SysTick->VAL = 0UL;

  /* 禁用并清除所有可屏蔽中断，防止旧中断进入主应用。 */
  for (index = 0U; index < BOOTLOADER_NVIC_REG_COUNT; index++)
  {
    NVIC->ICER[index] = 0xFFFFFFFFUL;
    NVIC->ICPR[index] = 0xFFFFFFFFUL;
  }

  SCB->VTOR = BOOTLOADER_APP_ADDRESS;
  __set_CONTROL(0UL);
  __set_MSP(app_stack);
  __DSB();
  __ISB();

  /* 已清除全部中断状态，可以让主应用以正常中断状态启动。 */
  __enable_irq();
  app_entry();

  /* 正常情况下不会返回。 */
  while (1)
  {
  }
}
