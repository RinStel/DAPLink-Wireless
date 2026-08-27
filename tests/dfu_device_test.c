#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "boot_state.h"
#include "dfu_device.h"

static uint8_t s_flash[256U * 1024U];

static bool flash_range(uint32_t address, size_t length, size_t *offset)
{
    if ((address < 0x08000000U) ||
        ((uint64_t)address + length > 0x08040000ULL)) {
        return false;
    }
    *offset = address - 0x08000000U;
    return true;
}

bool boot_state_test_read(uint32_t address, void *data, size_t length)
{
    size_t offset;
    if ((data == NULL) || !flash_range(address, length, &offset)) {
        return false;
    }
    memcpy(data, &s_flash[offset], length);
    return true;
}

bool boot_state_test_erase(uint32_t address)
{
    size_t offset;
    if ((address & 0x7FFU) != 0U || !flash_range(address, 2048U, &offset)) {
        return false;
    }
    memset(&s_flash[offset], 0xFF, 2048U);
    return true;
}

bool boot_state_test_program(uint32_t address, uint32_t value)
{
    size_t offset;
    uint32_t current;
    if ((address & 3U) != 0U || !flash_range(address, 4U, &offset)) {
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
    dfu_device_t device;
    firmware_image_header_t header = {
        .magic = FIRMWARE_IMAGE_MAGIC,
        .format_version = FIRMWARE_IMAGE_FORMAT_VERSION,
        .mcu_id = FIRMWARE_MCU_GD32F303CC,
        .slot = FIRMWARE_SLOT_B,
        .load_address = FIRMWARE_SLOT_B_BASE,
        .image_length = 8U,
        .firmware_version_code = 800U,
        .image_crc32 = 0x6013522AU
    };
    uint32_t payload[2] = {0x20000008U, 0x08021005U};
    dfu_flash_ops_t ops = {
        .read = dfu_read,
        .erase_page = dfu_erase,
        .program_word = dfu_program
    };

    assert(DFU_TRANSFER_SIZE == FIRMWARE_IMAGE_HEADER_SIZE);

    memset(s_flash, 0xFF, sizeof(s_flash));
    assert(boot_state_factory_init(FIRMWARE_SLOT_A, 799U,
                                   47512U, 0x12345678U));
    firmware_image_header_seal(&header);
    dfu_flash_set_ops(&ops);
    dfu_device_init(&device, FIRMWARE_SLOT_A, 799U, false);
    assert(dfu_device_state(&device) == DFU_STATE_IDLE);
    assert(dfu_device_descriptor_pid() == 0x1291U);
    assert(dfu_device_dnload(&device, 0U, &header, sizeof(header)) ==
           DFU_STATUS_OK);
    assert(dfu_device_state(&device) == DFU_STATE_DNLOAD_SYNC);
    assert(dfu_device_get_status(&device) == DFU_STATUS_OK);
    assert(dfu_device_dnload(&device, 1U, payload, sizeof(payload)) ==
           DFU_STATUS_OK);
    assert(dfu_device_get_status(&device) == DFU_STATUS_OK);
    assert(dfu_device_dnload(&device, 2U, NULL, 0U) == DFU_STATUS_OK);
    assert(dfu_device_get_status(&device) == DFU_STATUS_OK);
    assert(dfu_device_state(&device) == DFU_STATE_MANIFEST);
    assert(dfu_device_manifest_complete(&device));
    assert(dfu_device_get_status(&device) == DFU_STATUS_OK);
    assert(dfu_device_state(&device) == DFU_STATE_MANIFEST_WAIT_RESET);

    dfu_device_init(&device, FIRMWARE_SLOT_A, 799U, false);
    assert(dfu_device_dnload(&device, 1U, payload, sizeof(payload)) ==
           DFU_STATUS_ERR_SEQUENCE);
    assert(dfu_device_state(&device) == DFU_STATE_ERROR);
    assert(dfu_device_upload_allowed() == false);
    return 0;
}
