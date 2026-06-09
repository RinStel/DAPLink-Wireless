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
#ifndef CDC_ACM_TRANSPORT_H
#define CDC_ACM_TRANSPORT_H

#include <stdbool.h>
#include <stdint.h>

#include "cdc_acm_core.h"

extern usb_class cdc_class;

uint16_t cdc_acm_read(uint8_t *data, uint16_t capacity);
uint16_t cdc_acm_write(const uint8_t *data, uint16_t length);
bool cdc_acm_tx_ready(void);
bool cdc_acm_line_coding_take(acm_line *line);

#endif
