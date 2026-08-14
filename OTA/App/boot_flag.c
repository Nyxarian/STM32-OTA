#include "boot_flag.h"
#include "main.h"

/* RTC backup register 0 的绝对地址(STM32F407: RTC 基址 0x40002800 + 偏移 0x50) */
#define BOOT_FLAG_REGISTER_ADDR 0x40002850U

static void BootFlag_EnableBackupAccess(void)
{
    __HAL_RCC_PWR_CLK_ENABLE();
    PWR->CR |= PWR_CR_DBP;   /* 使能备份域访问(Disable Backup Domain write protection) */
}

void BootFlag_SetUpdateRequest(void)
{
    BootFlag_EnableBackupAccess();
    *(volatile uint32_t *)BOOT_FLAG_REGISTER_ADDR = BOOT_FLAG_MAGIC;
    __DSB();
}

bool BootFlag_IsUpdateRequested(void)
{
    return *(volatile const uint32_t *)BOOT_FLAG_REGISTER_ADDR == BOOT_FLAG_MAGIC;
}

void BootFlag_Clear(void)
{
    BootFlag_EnableBackupAccess();
    *(volatile uint32_t *)BOOT_FLAG_REGISTER_ADDR = 0U;
}
