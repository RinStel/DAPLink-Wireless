#include "firmware_image.h"

#include <stddef.h>

static uint32_t crc32_update(uint32_t crc, const uint8_t *bytes,
                             uint32_t length)
{
    uint32_t index;
    uint8_t bit;

    for (index = 0U; index < length; ++index) {
        crc ^= bytes[index];
        for (bit = 0U; bit < 8U; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
        }
    }
    return crc;
}

uint32_t firmware_crc32(const void *data, uint32_t length)
{
    if ((data == NULL) && (length != 0U)) {
        return 0U;
    }
    return ~crc32_update(0xFFFFFFFFU, (const uint8_t *)data, length);
}

void firmware_image_header_seal(firmware_image_header_t *header)
{
    if (header == NULL) {
        return;
    }
    header->header_size = FIRMWARE_IMAGE_HEADER_SIZE;
    header->header_crc32 = 0U;
    header->header_crc32 = firmware_crc32(header,
                                          (uint32_t)offsetof(
                                              firmware_image_header_t,
                                              header_crc32));
}

firmware_image_result_t firmware_image_header_validate(
    const firmware_image_header_t *header,
    firmware_slot_t expected_slot,
    uint32_t confirmed_version,
    bool recovery_mode)
{
    uint64_t end_address;

    if (header == NULL) {
        return FIRMWARE_IMAGE_ERR_NULL;
    }
    if (header->magic != FIRMWARE_IMAGE_MAGIC) {
        return FIRMWARE_IMAGE_ERR_MAGIC;
    }
    if ((header->format_version != FIRMWARE_IMAGE_FORMAT_VERSION) ||
        (header->header_size != FIRMWARE_IMAGE_HEADER_SIZE)) {
        return FIRMWARE_IMAGE_ERR_FORMAT;
    }
    if (header->mcu_id != FIRMWARE_MCU_GD32F303CC) {
        return FIRMWARE_IMAGE_ERR_MCU;
    }
    if (header->slot != (uint8_t)expected_slot) {
        return FIRMWARE_IMAGE_ERR_SLOT;
    }
    if (header->load_address != firmware_slot_base(expected_slot)) {
        return FIRMWARE_IMAGE_ERR_ADDRESS;
    }
    if (header->image_length == 0U) {
        return FIRMWARE_IMAGE_ERR_LENGTH;
    }
    end_address = (uint64_t)header->load_address + header->image_length;
    if ((header->image_length > FIRMWARE_SLOT_SIZE) ||
        (end_address > (uint64_t)firmware_slot_base(expected_slot) +
                       FIRMWARE_SLOT_SIZE)) {
        return FIRMWARE_IMAGE_ERR_LENGTH;
    }
    if (!recovery_mode &&
        (header->firmware_version_code <= confirmed_version)) {
        return FIRMWARE_IMAGE_ERR_VERSION;
    }
    {
        uint32_t calculated = firmware_crc32(
            header, (uint32_t)offsetof(firmware_image_header_t,
                                      header_crc32));
        if (calculated != header->header_crc32) {
            return FIRMWARE_IMAGE_ERR_HEADER_CRC;
        }
    }
    return FIRMWARE_IMAGE_VALID;
}

bool firmware_image_vectors_validate(uint32_t slot_base,
                                     uint32_t image_length,
                                     uint32_t initial_msp,
                                     uint32_t reset_vector)
{
    uint64_t image_end = (uint64_t)slot_base + image_length;
    uint32_t reset_address = reset_vector & ~1U;

    return (image_length >= 8U) &&
           ((initial_msp & 7U) == 0U) &&
           (initial_msp >= FIRMWARE_SRAM_BASE) &&
           (initial_msp <= FIRMWARE_SRAM_END) &&
           ((reset_vector & 1U) != 0U) &&
           ((uint64_t)reset_address >= slot_base) &&
           ((uint64_t)reset_address < image_end);
}
