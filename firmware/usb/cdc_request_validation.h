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
#ifndef CDC_REQUEST_VALIDATION_H
#define CDC_REQUEST_VALIDATION_H

#include <stdbool.h>
#include <stddef.h>

#include "usb_ch9_std.h"
#include "usbd_conf.h"

static inline bool cdc_set_line_coding_request_valid(
    const usb_req *request)
{
    return (request != NULL) &&
           (request->bmRequestType ==
            (USB_TRX_OUT | USB_REQTYPE_CLASS | USB_RECPTYPE_ITF)) &&
           ((uint8_t)request->wIndex == CDC_COM_INTERFACE) &&
           (request->wValue == 0U) &&
           (request->wLength == 7U);
}

static inline bool cdc_get_line_coding_request_valid(
    const usb_req *request)
{
    return (request != NULL) &&
           (request->bmRequestType ==
            (USB_TRX_IN | USB_REQTYPE_CLASS | USB_RECPTYPE_ITF)) &&
           ((uint8_t)request->wIndex == CDC_COM_INTERFACE) &&
           (request->wValue == 0U) &&
           (request->wLength == 7U);
}

static inline bool cdc_control_line_state_request_valid(
    const usb_req *request)
{
    return (request != NULL) &&
           (request->bmRequestType ==
            (USB_TRX_OUT | USB_REQTYPE_CLASS | USB_RECPTYPE_ITF)) &&
           ((uint8_t)request->wIndex == CDC_COM_INTERFACE) &&
           (request->wLength == 0U);
}

#endif
