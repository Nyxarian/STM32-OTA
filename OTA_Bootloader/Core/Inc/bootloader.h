/* 最小 Bootloader 接口：检查主应用并跳转到主应用。 */
#ifndef BOOTLOADER_H
#define BOOTLOADER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#define BOOTLOADER_APP_ADDRESS       0x08010000UL
#define BOOTLOADER_APP_FLASH_END     0x08080000UL

bool Bootloader_AreAppVectorsValid(void);

/* 检查主应用的初始栈地址和复位向量是否合法。 */
bool Bootloader_IsAppValid(void);

/* 关闭 Bootloader 使用的系统资源并跳转到主应用。 */
void Bootloader_JumpToApp(void);

#ifdef __cplusplus
}
#endif

#endif /* BOOTLOADER_H */
