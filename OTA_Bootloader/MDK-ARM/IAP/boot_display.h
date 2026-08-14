#ifndef __BOOT_DISPLAY_H__
#define __BOOT_DISPLAY_H__

#include <stdbool.h>
#include <stdint.h>

bool BootDisplay_Init(void);
void BootDisplay_ShowConnecting(void);
void BootDisplay_ShowWaiting(void);
void BootDisplay_ShowProgress(uint32_t received_length, uint32_t image_length);
void BootDisplay_ShowVerifying(void);
void BootDisplay_ShowRestarting(void);
void BootDisplay_ShowFailed(void);

#endif
