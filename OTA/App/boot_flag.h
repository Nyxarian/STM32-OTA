#ifndef __BOOT_FLAG_H__
#define __BOOT_FLAG_H__

#include <stdbool.h>
#include <stdint.h>

/*
 * 升级请求标志:存放在 RTC backup register 0(地址 0x40002850)。
 * - 系统复位(按复位键 / 软件复位)后保留;
 * - 整机断电后丢失(backup domain 由 VBAT/主电维持);
 * - App 侧写标志后软件复位,Bootloader 侧读标志决定是否直接进入升级模式。
 */
#define BOOT_FLAG_MAGIC  0x554F5441UL   /* "UOTA" */

void BootFlag_SetUpdateRequest(void);
bool BootFlag_IsUpdateRequested(void);
void BootFlag_Clear(void);

#endif /* __BOOT_FLAG_H__ */
