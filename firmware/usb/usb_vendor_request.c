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
#include "usbd_enum.h"

#include <stddef.h>

usb_reqsta usbd_vendor_request(usb_dev *udev, usb_req *req)
{
    if ((udev == NULL) || (req == NULL) ||
        (udev->class_core == NULL) ||
        (udev->class_core->req_process == NULL)) {
        return REQ_NOTSUPP;
    }
    return (usb_reqsta)udev->class_core->req_process(udev, req);
}
