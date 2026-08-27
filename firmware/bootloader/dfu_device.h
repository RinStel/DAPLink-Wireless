#ifndef DFU_DEVICE_H
#define DFU_DEVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dfu_flash.h"

#define DFU_DEVICE_VID 0x28E9U
#define DFU_DEVICE_PID 0x1291U
/* dfu-util splits the download stream into fixed wTransferSize blocks.  Block
 * zero is the product image header, so the advertised transfer size must
 * equal that fixed header size. */
#define DFU_TRANSFER_SIZE FIRMWARE_IMAGE_HEADER_SIZE

typedef enum {
    DFU_STATE_IDLE = 0U,
    DFU_STATE_DNLOAD_SYNC,
    DFU_STATE_DNLOAD_IDLE,
    DFU_STATE_MANIFEST_SYNC,
    DFU_STATE_MANIFEST,
    DFU_STATE_MANIFEST_WAIT_RESET,
    DFU_STATE_ERROR
} dfu_state_t;

typedef enum {
    DFU_STATUS_OK = 0U,
    DFU_STATUS_ERR_TARGET,
    DFU_STATUS_ERR_FILE,
    DFU_STATUS_ERR_WRITE,
    DFU_STATUS_ERR_ERASE,
    DFU_STATUS_ERR_VERIFY,
    DFU_STATUS_ERR_ADDRESS,
    DFU_STATUS_ERR_SEQUENCE,
    DFU_STATUS_ERR_FIRMWARE,
    DFU_STATUS_ERR_UNKNOWN
} dfu_status_t;

typedef struct {
    dfu_state_t state;
    dfu_status_t status;
    dfu_flash_session_t flash;
    firmware_slot_t active_slot;
    uint32_t confirmed_version;
    bool recovery_mode;
    bool header_received;
    bool manifest_complete;
} dfu_device_t;

void dfu_device_init(dfu_device_t *device,
                     firmware_slot_t active_slot,
                     uint32_t confirmed_version,
                     bool recovery_mode);
dfu_status_t dfu_device_dnload(dfu_device_t *device,
                               uint16_t block,
                               const void *data,
                               size_t length);
dfu_status_t dfu_device_get_status(dfu_device_t *device);
dfu_state_t dfu_device_state(const dfu_device_t *device);
bool dfu_device_manifest_complete(const dfu_device_t *device);
void dfu_device_clear_status(dfu_device_t *device);
void dfu_device_abort(dfu_device_t *device);
uint16_t dfu_device_descriptor_pid(void);
bool dfu_device_upload_allowed(void);

/* The host model deliberately has no dependency on the GD32 USB headers.  The
 * target-only adapter below owns the USB descriptors and EP0 callbacks. */
#ifdef DFU_DEVICE_USB_TARGET
#include "usbd_core.h"
extern usb_desc dfu_device_desc;
extern usb_class dfu_device_class;

void dfu_device_usb_bind(dfu_device_t *device);
void dfu_device_usb_unbind(void);
void dfu_device_usb_init(usb_dev *udev);
void dfu_device_usb_irq(void);
void dfu_device_usb_hp_irq(void);
void dfu_device_usb_wakeup_irq(usb_dev *udev);
bool dfu_device_usb_manifest_status_sent(void);
usb_dev *dfu_device_usb_current(void);
#endif

#endif
