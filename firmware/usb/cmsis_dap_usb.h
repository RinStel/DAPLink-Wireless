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
#ifndef CMSIS_DAP_USB_H
#define CMSIS_DAP_USB_H

#include <stdbool.h>
#include <stdint.h>

#include "usbd_core.h"

#define DAP_V2_INTERFACE 3U

#define DAP_V2_IN_EP     0x85U
#define DAP_V2_OUT_EP    0x05U

#define DAP_USB_PACKET_SIZE 64U

extern usb_class cmsis_dap_usb_class;

void cmsis_dap_usb_process(void);
bool cmsis_dap_usb_idle(void);

#endif
