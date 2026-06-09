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
