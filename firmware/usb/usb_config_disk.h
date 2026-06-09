#ifndef USB_CONFIG_DISK_H
#define USB_CONFIG_DISK_H

#include <stdbool.h>

#include "device_config.h"

bool usb_config_disk_init(void);
void usb_config_disk_process(void);
bool usb_config_disk_configured(void);

void usb_config_disk_irq(void);
void usb_config_disk_hp_irq(void);
void usb_config_disk_wakeup_irq(void);
void usb_config_disk_refresh(const device_config_t *previous);

#endif
