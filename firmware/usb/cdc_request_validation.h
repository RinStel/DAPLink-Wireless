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
