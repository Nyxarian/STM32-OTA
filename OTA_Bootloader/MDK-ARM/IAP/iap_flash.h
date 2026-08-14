#ifndef __IAP_FLASH_H__
#define __IAP_FLASH_H__

#include <stdbool.h>
#include <stdint.h>

#define IAP_FLASH_APP_START          0x08010000U
#define IAP_FLASH_STORAGE_END        0x08080000U

/*
 * APP 区末尾保留 32 字节给 OTA 元数据：
 * - 0x0807FFE0 ~ 0x0807FFEF：镜像状态区；
 * - 0x0807FFF0 ~ 0x0807FFFF：镜像长度、CRC32 和提交信息。
 *
 * APP 链接区必须在状态区前结束，防止应用代码或常量覆盖 OTA 元数据。
 */
#define IAP_FLASH_IMAGE_STATE_ADDRESS 0x0807FFE0U
#define IAP_FLASH_IMAGE_INFO_ADDRESS 0x0807FFF0U
#define IAP_FLASH_APP_END            IAP_FLASH_IMAGE_STATE_ADDRESS

#define IAP_FLASH_IMAGE_MAGIC        0x49415031U
#define IAP_FLASH_IMAGE_COMMIT       0xC0DEC0DEU

/*
 * STM32 Flash 只能把位从 1 写成 0。
 * 因此状态按 EMPTY -> UPDATING -> COMMITTED 的方向设计，
 * 提交镜像时不需要为了改变状态而再次擦除 APP 区。
 */
#define IAP_FLASH_IMAGE_STATE_EMPTY      0xFFFFFFFFU
#define IAP_FLASH_IMAGE_STATE_UPDATING   0xFFFFFFFEU
#define IAP_FLASH_IMAGE_STATE_COMMITTED  0xFFFFFFFCU

/* 读取镜像状态字；Bootloader 用它区分工厂烧录、升级中和已提交镜像。 */
uint32_t IapFlash_GetImageState(void);
bool IapFlash_IsRangeValid(uint32_t address, uint32_t length);
bool IapFlash_EraseApplication(void);
bool IapFlash_Write(uint32_t address, const uint8_t *data, uint32_t length);
bool IapFlash_CommitImage(uint32_t image_length, uint32_t image_crc32);

#endif
