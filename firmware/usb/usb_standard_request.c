#include "usbd_enum.h"

#include <stdbool.h>
#include <stddef.h>

usb_reqsta gd32_usbd_standard_request_unchecked(
    usb_dev *udev, usb_req *req);

static bool request_recipient_valid(const usb_req *req)
{
    uint8_t recipient = req->bmRequestType & USB_RECPTYPE_MASK;

    if (recipient == USB_RECPTYPE_DEV) {
        return true;
    }
    if (recipient == USB_RECPTYPE_ITF) {
        return (req->wIndex < USBD_ITF_MAX_NUM);
    }
    if (recipient == USB_RECPTYPE_EP) {
        uint8_t endpoint = (uint8_t)req->wIndex;

        return ((req->wIndex & 0xFF70U) == 0U) &&
               (EP_ID(endpoint) < EP_COUNT);
    }
    return false;
}

usb_reqsta usbd_standard_request(usb_dev *udev, usb_req *req)
{
    if ((udev == NULL) || (req == NULL) ||
        (req->bRequest > USB_SYNCH_FRAME) ||
        !request_recipient_valid(req)) {
        return REQ_NOTSUPP;
    }
    return gd32_usbd_standard_request_unchecked(udev, req);
}
