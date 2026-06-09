#ifndef USB_COMPOSITE_H
#define USB_COMPOSITE_H

#include "usbd_core.h"

extern usb_desc composite_desc;
extern usb_class composite_class;

void usb_composite_prepare(void);

#endif
