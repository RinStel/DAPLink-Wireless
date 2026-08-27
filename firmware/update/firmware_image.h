#ifndef FIRMWARE_IMAGE_H
#define FIRMWARE_IMAGE_H

#include <stdbool.h>
#include <stdint.h>

#include "firmware_layout.h"

#define FIRMWARE_IMAGE_MAGIC          0x44574655U
#define FIRMWARE_IMAGE_FORMAT_VERSION 1U
#define FIRMWARE_IMAGE_HEADER_SIZE    64U

typedef struct {
    uint32_t magic;
    uint16_t format_version;
    uint16_t header_size;
    uint32_t mcu_id;
    uint8_t slot;
    uint8_t reserved0[3];
    uint32_t load_address;
    uint32_t image_length;
    uint32_t firmware_version_code;
    uint32_t image_crc32;
    uint32_t header_crc32;
    uint8_t reserved1[28];
} firmware_image_header_t;

_Static_assert(sizeof(firmware_image_header_t) == FIRMWARE_IMAGE_HEADER_SIZE,
               "firmware image header must be 64 bytes");

typedef enum {
    FIRMWARE_IMAGE_VALID = 0,
    FIRMWARE_IMAGE_ERR_NULL,
    FIRMWARE_IMAGE_ERR_MAGIC,
    FIRMWARE_IMAGE_ERR_FORMAT,
    FIRMWARE_IMAGE_ERR_MCU,
    FIRMWARE_IMAGE_ERR_SLOT,
    FIRMWARE_IMAGE_ERR_ADDRESS,
    FIRMWARE_IMAGE_ERR_LENGTH,
    FIRMWARE_IMAGE_ERR_VERSION,
    FIRMWARE_IMAGE_ERR_HEADER_CRC
} firmware_image_result_t;

uint32_t firmware_crc32(const void *data, uint32_t length);
void firmware_image_header_seal(firmware_image_header_t *header);
firmware_image_result_t firmware_image_header_validate(
    const firmware_image_header_t *header,
    firmware_slot_t expected_slot,
    uint32_t confirmed_version,
    bool recovery_mode);
bool firmware_image_vectors_validate(uint32_t slot_base,
                                     uint32_t image_length,
                                     uint32_t initial_msp,
                                     uint32_t reset_vector);

#endif
