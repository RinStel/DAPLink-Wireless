#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "boot_state.h"
#include "dfu_flash.h"
#include "firmware_image.h"

#define FLASH_BASE 0x08000000U
#define FLASH_SIZE (256U * 1024U)
#define PAGE_SIZE 2048U

static uint8_t s_flash[FLASH_SIZE];

static bool range_ok(uint32_t address, size_t length, size_t *offset)
{
    if ((address < FLASH_BASE) ||
        ((uint64_t)address + length > (uint64_t)FLASH_BASE + FLASH_SIZE)) {
        return false;
    }
    *offset = address - FLASH_BASE;
    return true;
}

bool boot_state_test_read(uint32_t address, void *data, size_t length)
{
    size_t offset;
    return (data != NULL) && range_ok(address, length, &offset) &&
           (memcpy(data, &s_flash[offset], length) != NULL);
}

bool boot_state_test_erase(uint32_t address)
{
    size_t offset;
    if (((address % PAGE_SIZE) != 0U) || !range_ok(address, PAGE_SIZE,
                                                   &offset)) {
        return false;
    }
    memset(&s_flash[offset], 0xFF, PAGE_SIZE);
    return true;
}

bool boot_state_test_program(uint32_t address, uint32_t value)
{
    size_t offset;
    uint32_t current;
    if (((address & 3U) != 0U) || !range_ok(address, sizeof(value), &offset)) {
        return false;
    }
    memcpy(&current, &s_flash[offset], sizeof(current));
    if ((current & value) != value) {
        return false;
    }
    current &= value;
    memcpy(&s_flash[offset], &current, sizeof(current));
    return true;
}

static bool dfu_read(uint32_t address, void *data, size_t length)
{
    return boot_state_test_read(address, data, length);
}

static bool dfu_erase(uint32_t address)
{
    return boot_state_test_erase(address);
}

static bool dfu_program(uint32_t address, uint32_t value)
{
    return boot_state_test_program(address, value);
}

int main(void)
{
    static const uint8_t payload[16] = {
        0x00U, 0x20U, 0x00U, 0x20U,
        0x05U, 0x10U, 0x02U, 0x08U,
        0xAAU, 0x55U, 0x11U, 0x22U,
        0x33U, 0x44U, 0x66U, 0x77U
    };
    firmware_image_header_t header = {
        .magic = FIRMWARE_IMAGE_MAGIC,
        .format_version = FIRMWARE_IMAGE_FORMAT_VERSION,
        .mcu_id = FIRMWARE_MCU_GD32F303CC,
        .slot = FIRMWARE_SLOT_B,
        .load_address = FIRMWARE_SLOT_B_BASE,
        .image_length = sizeof(payload),
        .firmware_version_code = 800U,
        .image_crc32 = 0U
    };
    dfu_flash_ops_t ops = {
        .read = dfu_read,
        .erase_page = dfu_erase,
        .program_word = dfu_program
    };
    dfu_flash_session_t session;

    memset(s_flash, 0xFF, sizeof(s_flash));
    boot_state_flash_set_ops(NULL);
    assert(boot_state_factory_init(FIRMWARE_SLOT_A, 799U,
                                   47512U, 0x12345678U));
    header.image_crc32 = firmware_crc32(payload, sizeof(payload));
    firmware_image_header_seal(&header);
    dfu_flash_set_ops(&ops);
    assert(dfu_flash_begin(&session, &header, FIRMWARE_SLOT_A,
                           799U, false) == DFU_FLASH_OK);
    assert(dfu_flash_write_block(&session, 0U, payload,
                                 sizeof(payload)) == DFU_FLASH_OK);
    assert(dfu_flash_finish(&session) == DFU_FLASH_OK);

    assert(boot_state_load(&(boot_state_t){0}));
    header.slot = FIRMWARE_SLOT_A;
    header.load_address = FIRMWARE_SLOT_A_BASE;
    firmware_image_header_seal(&header);
    assert(dfu_flash_begin(&session, &header, FIRMWARE_SLOT_A,
                           799U, false) == DFU_FLASH_ERR_ACTIVE_SLOT);

    header = (firmware_image_header_t){
        .magic = FIRMWARE_IMAGE_MAGIC,
        .format_version = FIRMWARE_IMAGE_FORMAT_VERSION,
        .mcu_id = FIRMWARE_MCU_GD32F303CC,
        .slot = FIRMWARE_SLOT_B,
        .load_address = FIRMWARE_BOOT_BASE,
        .image_length = sizeof(payload),
        .firmware_version_code = 800U,
        .image_crc32 = firmware_crc32(payload, sizeof(payload))
    };
    firmware_image_header_seal(&header);
    assert(dfu_flash_begin(&session, &header, FIRMWARE_SLOT_A,
                           799U, false) == DFU_FLASH_ERR_ADDRESS);
    return 0;
}
