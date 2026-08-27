#include <assert.h>
#include <stdint.h>

#include "firmware_image.h"

static firmware_image_header_t valid_header(void)
{
    firmware_image_header_t header = {
        .magic = FIRMWARE_IMAGE_MAGIC,
        .format_version = FIRMWARE_IMAGE_FORMAT_VERSION,
        .mcu_id = FIRMWARE_MCU_GD32F303CC,
        .slot = FIRMWARE_SLOT_B,
        .load_address = FIRMWARE_SLOT_B_BASE,
        .image_length = 47512U,
        .firmware_version_code = 800U
    };

    firmware_image_header_seal(&header);
    return header;
}

int main(void)
{
    firmware_image_header_t header = valid_header();

    assert(sizeof(header) == FIRMWARE_IMAGE_HEADER_SIZE);
    assert(firmware_image_header_validate(&header, FIRMWARE_SLOT_B,
                                          799U, false) ==
           FIRMWARE_IMAGE_VALID);

    header.image_length = FIRMWARE_SLOT_SIZE + 1U;
    firmware_image_header_seal(&header);
    assert(firmware_image_header_validate(&header, FIRMWARE_SLOT_B,
                                          799U, false) ==
           FIRMWARE_IMAGE_ERR_LENGTH);

    header = valid_header();
    header.mcu_id ^= 1U;
    firmware_image_header_seal(&header);
    assert(firmware_image_header_validate(&header, FIRMWARE_SLOT_B,
                                          799U, false) ==
           FIRMWARE_IMAGE_ERR_MCU);

    header = valid_header();
    assert(firmware_image_header_validate(&header, FIRMWARE_SLOT_B,
                                          800U, false) ==
           FIRMWARE_IMAGE_ERR_VERSION);
    assert(firmware_image_header_validate(&header, FIRMWARE_SLOT_B,
                                          800U, true) ==
           FIRMWARE_IMAGE_VALID);

    header = valid_header();
    header.load_address = FIRMWARE_SLOT_A_BASE;
    firmware_image_header_seal(&header);
    assert(firmware_image_header_validate(&header, FIRMWARE_SLOT_B,
                                          799U, false) ==
           FIRMWARE_IMAGE_ERR_ADDRESS);

    assert(firmware_image_vectors_validate(FIRMWARE_SLOT_B_BASE,
                                           1024U,
                                           FIRMWARE_SRAM_BASE + 1024U,
                                           FIRMWARE_SLOT_B_BASE + 5U));
    assert(!firmware_image_vectors_validate(FIRMWARE_SLOT_B_BASE,
                                            1024U,
                                            FIRMWARE_SRAM_END + 4U,
                                            FIRMWARE_SLOT_B_BASE + 5U));
    assert(!firmware_image_vectors_validate(FIRMWARE_SLOT_B_BASE,
                                            1024U,
                                            FIRMWARE_SRAM_BASE + 1024U,
                                            FIRMWARE_SLOT_B_BASE + 1025U));

    return 0;
}
