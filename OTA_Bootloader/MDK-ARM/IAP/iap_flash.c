#include "iap_flash.h"
#include <stdbool.h>
#include <string.h>
#include "main.h"

/**
 * @brief 向指定地址写入32位字.
 *
 * @param address 要写入的地址.
 * @param word 要写入的32位字.
 * @return 成功则返回true, 否则返回false.
 */
static bool IapFlash_ProgramWord(uint32_t address, uint32_t word)
{
    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                          address,
                          word) != HAL_OK)
    {
        return false;
    }

    if (*(volatile uint32_t *)address != word)
    {
        return false;
    }

    return true;
}

/**
 * @brief 读取 APP 镜像的 OTA 状态字。
 *
 * @return 当前状态区中保存的原始 32 位状态值。
 */
uint32_t IapFlash_GetImageState(void)
{
    return *(volatile const uint32_t *)IAP_FLASH_IMAGE_STATE_ADDRESS; /* 从保留 Flash 地址读取当前升级状态。 */
}

/**
 * @brief 检查地址范围是否有效用于执行IAP.
 *
 * @param address 要检查的地址范围的起始地址.
 * @param length 要检查的地址范围的长度.
 * @return 有效则返回true, 否则返回false.
 */
bool IapFlash_IsRangeValid(uint32_t address, uint32_t length)
{
    if (length == 0U)
    {
        return false;
    }

    if (address < IAP_FLASH_APP_START)
    {
        return false;
    }

    if (address >= IAP_FLASH_APP_END)
    {
        return false;
    }

    if (length > (IAP_FLASH_APP_END - address))
    {
        return false;
    }

    return true;
}

/**
 * @brief 擦除应用程序区域，并立即标记为“升级中”.
 *
 * @return 成功则返回true, 否则返回false.
 */
bool IapFlash_EraseApplication(void)
{
    FLASH_EraseInitTypeDef erase;
    uint32_t sector_error = 0U;
    uint32_t address;

    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    erase.Sector = FLASH_SECTOR_4;
    erase.NbSectors = 4U;

    if (HAL_FLASH_Unlock() != HAL_OK)
    {
        return false;
    }

    if (HAL_FLASHEx_Erase(&erase, &sector_error) != HAL_OK)
    {
        HAL_FLASH_Lock();
        return false;
    }

    for (address = IAP_FLASH_APP_START;
         address < IAP_FLASH_STORAGE_END;
         address += 4U)
    {
        if (*(volatile uint32_t *)address != 0xFFFFFFFFU)
        {
            HAL_FLASH_Lock();
            return false;
        }
    }

    /*
     * 擦除后先写入 UPDATING 状态。
     * 从此刻开始即使突然掉电，Bootloader 下次上电也会拒绝跳入尚未
     * 提交的应用区，而是继续停留在升级模式。
     */
    if (!IapFlash_ProgramWord(IAP_FLASH_IMAGE_STATE_ADDRESS,
                              IAP_FLASH_IMAGE_STATE_UPDATING))
    {
        HAL_FLASH_Lock();
        return false;
    }

    HAL_FLASH_Lock();
    return true;
}

/**
 * @brief 向指定地址写入数据.
 *
 * @param address 要写入数据的起始地址.
 * @param data 要写入的数据指针.
 * @param length 要写入的数据长度.
 * @return 成功则返回true, 否则返回false.
 */
bool IapFlash_Write(uint32_t address,
                    const uint8_t *data,
                    uint32_t length)
{
    uint32_t index;
    uint32_t word;

    if ((data == 0) ||
        !IapFlash_IsRangeValid(address, length))
    {
        return false;
    }

    if (((address % 4U) != 0U) ||
        ((length % 4U) != 0U))
    {
        return false;
    }

    if (HAL_FLASH_Unlock() != HAL_OK)
    {
        return false;
    }

    for (index = 0U; index < length; index += 4U)
    {
        word =  ((uint32_t)data[index]);
        word |= ((uint32_t)data[index + 1U] << 8U);
        word |= ((uint32_t)data[index + 2U] << 16U);
        word |= ((uint32_t)data[index + 3U] << 24U);

        if (!IapFlash_ProgramWord(address + index, word))
        {
            HAL_FLASH_Lock();
            return false;
        }
    }

    HAL_FLASH_Lock();
    return true;
}

/**
 * @brief 提交应用程序更新.
 *
 * @param image_length 应用程序图像的长度.
 * @param image_crc32 应用程序图像的CRC32校验和.
 * @return 成功则返回true, 否则返回false.
 */
bool IapFlash_CommitImage(uint32_t image_length,
                          uint32_t image_crc32)
{
    if ((image_length == 0U) ||
        ((image_length % 4U) != 0U) ||
        (image_length > (IAP_FLASH_APP_END - IAP_FLASH_APP_START)))
    {
        return false;
    }

    /* 只有已经成功擦除并标记为升级中的镜像，才允许执行最终提交。 */
    if (IapFlash_GetImageState() != IAP_FLASH_IMAGE_STATE_UPDATING)
    {
        return false;
    }

    if (HAL_FLASH_Unlock() != HAL_OK)
    {
        return false;
    }

    if (!IapFlash_ProgramWord(IAP_FLASH_IMAGE_INFO_ADDRESS,
                              IAP_FLASH_IMAGE_MAGIC) ||
        !IapFlash_ProgramWord(IAP_FLASH_IMAGE_INFO_ADDRESS + 4U,
                              image_length) ||
         !IapFlash_ProgramWord(IAP_FLASH_IMAGE_INFO_ADDRESS + 8U,
                               image_crc32) ||
         !IapFlash_ProgramWord(IAP_FLASH_IMAGE_INFO_ADDRESS + 12U,
                               IAP_FLASH_IMAGE_COMMIT) ||
         !IapFlash_ProgramWord(IAP_FLASH_IMAGE_STATE_ADDRESS,
                               IAP_FLASH_IMAGE_STATE_COMMITTED))
    {
        HAL_FLASH_Lock();
        return false;
    }

    HAL_FLASH_Lock();
    return true;
}
