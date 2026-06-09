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
