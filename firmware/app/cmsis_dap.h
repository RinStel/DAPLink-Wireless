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
#ifndef CMSIS_DAP_H
#define CMSIS_DAP_H

#include <stdbool.h>
#include <stdint.h>

#define CMSIS_DAP_PACKET_SIZE 64U

void cmsis_dap_init(void);
bool cmsis_dap_submit(const uint8_t *request, uint8_t length);
void cmsis_dap_abort(void);
void cmsis_dap_process(void);
bool cmsis_dap_busy(void);
bool cmsis_dap_response_take(uint8_t *response, uint8_t *length);

#endif
