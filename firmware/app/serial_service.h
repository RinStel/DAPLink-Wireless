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
#ifndef SERIAL_SERVICE_H
#define SERIAL_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "device_config.h"
#include "radio_protocol.h"

bool serial_service_init(void);
void serial_service_process(void);
bool serial_service_wired_process(void);
bool serial_service_deliver_data(device_mode_t mode,
                                 const uint8_t *payload, uint8_t length);
bool serial_service_deliver_line_coding(const uint8_t *payload,
                                        uint8_t length);
bool serial_service_source_take(device_mode_t mode,
                                radio_frame_type_t *type,
                                uint8_t *payload, uint8_t *length);
uint32_t serial_service_rx_overruns(void);

#endif
