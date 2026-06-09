/*
 * DAPLink-Wireless — Wireless CMSIS-DAP v2 debug probe firmware
 * Copyright (C) 2025 RinStel <me@rinx.nz>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
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
