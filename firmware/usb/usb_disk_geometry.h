#ifndef USB_DISK_GEOMETRY_H
#define USB_DISK_GEOMETRY_H

#include <stdbool.h>
#include <stdint.h>

static inline bool usb_disk_byte_range_valid(uint32_t disk_size,
                                             uint32_t block_size,
                                             uint32_t byte_address,
                                             uint16_t block_count,
                                             uint32_t *byte_count)
{
    uint32_t length;

    if ((block_size == 0U) ||
        ((byte_address % block_size) != 0U) ||
        (byte_address > disk_size)) {
        return false;
    }
    length = (uint32_t)block_count * block_size;
    if (length > disk_size - byte_address) {
        return false;
    }
    if (byte_count != NULL) {
        *byte_count = length;
    }
    return true;
}

#endif
