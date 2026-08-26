#ifndef FIRMWARE_LAYOUT_H
#define FIRMWARE_LAYOUT_H

#include <stdint.h>

#define FIRMWARE_BOOT_BASE        0x08000000U
#define FIRMWARE_BOOT_SIZE        0x00004000U
#define FIRMWARE_SLOT_A_BASE      0x08004000U
#define FIRMWARE_SLOT_B_BASE      0x08021000U
#define FIRMWARE_SLOT_SIZE        0x0001D000U
#define FIRMWARE_BOOT_STATE_BASE  0x0803E000U
#define FIRMWARE_CONFIG_BASE      0x0803F000U
#define FIRMWARE_FLASH_END        0x08040000U
#define FIRMWARE_FLASH_PAGE_SIZE  0x00000800U
#define FIRMWARE_SRAM_BASE        0x20000000U
#define FIRMWARE_SRAM_END         0x2000C000U
#define FIRMWARE_MCU_GD32F303CC   0x303CCU

typedef enum {
    FIRMWARE_SLOT_A = 0U,
    FIRMWARE_SLOT_B = 1U
} firmware_slot_t;

static inline uint32_t firmware_slot_base(firmware_slot_t slot)
{
    return slot == FIRMWARE_SLOT_A ? FIRMWARE_SLOT_A_BASE :
                                      FIRMWARE_SLOT_B_BASE;
}

#endif
