#include "dfu_device.h"

#include <string.h>

static dfu_status_t map_flash_error(dfu_flash_result_t result)
{
    switch (result) {
    case DFU_FLASH_OK:
        return DFU_STATUS_OK;
    case DFU_FLASH_ERR_ADDRESS:
        return DFU_STATUS_ERR_ADDRESS;
    case DFU_FLASH_ERR_CRC:
    case DFU_FLASH_ERR_READ:
        return DFU_STATUS_ERR_VERIFY;
    case DFU_FLASH_ERR_VECTOR:
        return DFU_STATUS_ERR_FIRMWARE;
    case DFU_FLASH_ERR_ERASE:
        return DFU_STATUS_ERR_ERASE;
    case DFU_FLASH_ERR_PROGRAM:
        return DFU_STATUS_ERR_WRITE;
    case DFU_FLASH_ERR_SEQUENCE:
        return DFU_STATUS_ERR_SEQUENCE;
    default:
        return DFU_STATUS_ERR_FIRMWARE;
    }
}

void dfu_device_init(dfu_device_t *device,
                     firmware_slot_t active_slot,
                     uint32_t confirmed_version,
                     bool recovery_mode)
{
    if (device == NULL) {
        return;
    }
    memset(device, 0, sizeof(*device));
    device->state = DFU_STATE_IDLE;
    device->status = DFU_STATUS_OK;
    device->active_slot = active_slot;
    device->confirmed_version = confirmed_version;
    device->recovery_mode = recovery_mode;
}

dfu_status_t dfu_device_dnload(dfu_device_t *device,
                               uint16_t block,
                               const void *data,
                               size_t length)
{
    dfu_flash_result_t result;
    firmware_image_header_t header;

    if ((device == NULL) || (device->state == DFU_STATE_ERROR)) {
        return DFU_STATUS_ERR_UNKNOWN;
    }
    if (length == 0U) {
        if ((device->state != DFU_STATE_DNLOAD_IDLE) ||
            !device->header_received) {
            device->state = DFU_STATE_ERROR;
            device->status = DFU_STATUS_ERR_SEQUENCE;
            return device->status;
        }
        device->state = DFU_STATE_MANIFEST_SYNC;
        return DFU_STATUS_OK;
    }
    if (data == NULL) {
        device->state = DFU_STATE_ERROR;
        device->status = DFU_STATUS_ERR_FILE;
        return device->status;
    }
    if (block == 0U) {
        if ((length != FIRMWARE_IMAGE_HEADER_SIZE) ||
            device->header_received) {
            device->state = DFU_STATE_ERROR;
            device->status = DFU_STATUS_ERR_SEQUENCE;
            return device->status;
        }
        /* USB EP0 buffers are byte-aligned; copy before reading the fixed
         * width header so Cortex-M unaligned access settings cannot turn a
         * malformed host packet into a fault. */
        memcpy(&header, data, sizeof(header));
        result = dfu_flash_begin(&device->flash, &header,
                                 device->active_slot,
                                 device->confirmed_version,
                                 device->recovery_mode);
        device->status = map_flash_error(result);
        if (result != DFU_FLASH_OK) {
            device->state = DFU_STATE_ERROR;
            return device->status;
        }
        device->header_received = true;
        device->state = DFU_STATE_DNLOAD_SYNC;
        return DFU_STATUS_OK;
    }
    if (!device->header_received ||
        ((uint32_t)(block - 1U) * DFU_TRANSFER_SIZE !=
         device->flash.next_offset) ||
        length > DFU_TRANSFER_SIZE) {
        device->state = DFU_STATE_ERROR;
        device->status = DFU_STATUS_ERR_SEQUENCE;
        return device->status;
    }
    result = dfu_flash_write_block(&device->flash,
                                   device->flash.next_offset,
                                   (const uint8_t *)data, length);
    device->status = map_flash_error(result);
    if (result != DFU_FLASH_OK) {
        device->state = DFU_STATE_ERROR;
        return device->status;
    }
    device->state = DFU_STATE_DNLOAD_SYNC;
    return DFU_STATUS_OK;
}

dfu_status_t dfu_device_get_status(dfu_device_t *device)
{
    dfu_flash_result_t result;

    if (device == NULL) {
        return DFU_STATUS_ERR_UNKNOWN;
    }
    if (device->state == DFU_STATE_DNLOAD_SYNC) {
        device->state = DFU_STATE_DNLOAD_IDLE;
    } else if (device->state == DFU_STATE_MANIFEST_SYNC) {
        result = dfu_flash_finish(&device->flash);
        device->status = map_flash_error(result);
        if (result != DFU_FLASH_OK) {
            device->state = DFU_STATE_ERROR;
            return device->status;
        }
        device->state = DFU_STATE_MANIFEST;
        device->manifest_complete = true;
    } else if (device->state == DFU_STATE_MANIFEST) {
        device->state = DFU_STATE_MANIFEST_WAIT_RESET;
    }
    return device->status;
}

dfu_state_t dfu_device_state(const dfu_device_t *device)
{
    return device == NULL ? DFU_STATE_ERROR : device->state;
}

bool dfu_device_manifest_complete(const dfu_device_t *device)
{
    return device != NULL && device->manifest_complete;
}

void dfu_device_clear_status(dfu_device_t *device)
{
    if (device == NULL) {
        return;
    }
    if (device->state == DFU_STATE_ERROR) {
        device->state = DFU_STATE_IDLE;
        device->status = DFU_STATUS_OK;
        device->header_received = false;
        device->manifest_complete = false;
        dfu_flash_abort(&device->flash);
    }
}

void dfu_device_abort(dfu_device_t *device)
{
    if (device == NULL) {
        return;
    }
    dfu_flash_abort(&device->flash);
    device->state = DFU_STATE_IDLE;
    device->status = DFU_STATUS_OK;
    device->header_received = false;
    device->manifest_complete = false;
}

uint16_t dfu_device_descriptor_pid(void)
{
    return DFU_DEVICE_PID;
}

bool dfu_device_upload_allowed(void)
{
    return false;
}

#ifdef DFU_DEVICE_USB_TARGET

#include "usbd_enum.h"
#include "usbd_lld_int.h"
#include "usbd_transc.h"

#define DFU_USB_INTERFACE             0U
#define DFU_USB_FUNCTION_DESC_TYPE    0x21U
#define DFU_USB_CLASS                 0xFEU
#define DFU_USB_SUBCLASS              0x01U
#define DFU_USB_PROTOCOL              0x02U
#define DFU_USB_STATUS_LENGTH         6U
#define DFU_USB_STRING_COUNT          256U
#define DFU_USB_NO_CMD                0xFFU
#define MS_OS_STRING_INDEX            0xEEU
#define MS_OS_VENDOR_CODE             0x20U
#define MS_OS_COMPAT_ID_INDEX         0x0004U
#define MS_OS_EXT_PROP_INDEX          0x0005U

#pragma pack(1)
typedef struct {
    usb_desc_config config;
    usb_desc_itf interface_desc;
    usb_desc_header functional_header;
    uint8_t attributes;
    uint16_t detach_timeout;
    uint16_t transfer_size;
    uint16_t dfu_version;
} dfu_usb_config_desc_t;

typedef struct {
    uint32_t length;
    uint16_t version;
    uint16_t index;
    uint16_t property_count;
    uint32_t property_size;
    uint32_t property_type;
    uint16_t property_name_length;
    uint16_t property_name[21];
    uint32_t property_data_length;
    uint16_t property_data[40];
} ms_os_ext_property_desc_t;
#pragma pack()

_Static_assert(sizeof(ms_os_ext_property_desc_t) == 146U,
               "Microsoft OS property descriptor size mismatch");

static usb_dev *s_usb;
static dfu_device_t *s_device;
static uint16_t s_pending_block;
static uint16_t s_pending_length;
static uint8_t s_dnload_buffer[DFU_TRANSFER_SIZE];
static uint8_t s_status_buffer[DFU_USB_STATUS_LENGTH];
static bool s_manifest_status_sent;

static const usb_desc_dev s_device_desc = {
    .header = {sizeof(usb_desc_dev), USB_DESCTYPE_DEV},
    .bcdUSB = 0x0200U,
    .bDeviceClass = 0x00U,
    .bDeviceSubClass = 0x00U,
    .bDeviceProtocol = 0x00U,
    .bMaxPacketSize0 = USBD_EP0_MAX_SIZE,
    .idVendor = DFU_DEVICE_VID,
    .idProduct = DFU_DEVICE_PID,
    .bcdDevice = 0x0100U,
    .iManufacturer = STR_IDX_MFC,
    .iProduct = STR_IDX_PRODUCT,
    .iSerialNumber = STR_IDX_SERIAL,
    .bNumberConfigurations = 1U
};

static const dfu_usb_config_desc_t s_config_desc = {
    .config = {
        .header = {sizeof(usb_desc_config), USB_DESCTYPE_CONFIG},
        .wTotalLength = sizeof(dfu_usb_config_desc_t),
        .bNumInterfaces = 1U,
        .bConfigurationValue = 1U,
        .iConfiguration = 0U,
        .bmAttributes = 0x80U,
        .bMaxPower = 0x32U
    },
    .interface_desc = {
        .header = {sizeof(usb_desc_itf), USB_DESCTYPE_ITF},
        .bInterfaceNumber = DFU_USB_INTERFACE,
        .bAlternateSetting = 0U,
        .bNumEndpoints = 0U,
        .bInterfaceClass = DFU_USB_CLASS,
        .bInterfaceSubClass = DFU_USB_SUBCLASS,
        .bInterfaceProtocol = DFU_USB_PROTOCOL,
        .iInterface = STR_IDX_ITF
    },
    .functional_header = {9U, DFU_USB_FUNCTION_DESC_TYPE},
    .attributes = 0x01U, /* download; manifestation resets, UPLOAD disabled */
    .detach_timeout = 0U,
    .transfer_size = DFU_TRANSFER_SIZE,
    .dfu_version = 0x0110U
};

static const usb_desc_LANGID s_language = {
    .header = {sizeof(usb_desc_LANGID), USB_DESCTYPE_STR},
    .wLANGID = ENG_LANGID
};
static const usb_desc_str s_manufacturer = {
    .header = {USB_STRING_LEN(7U), USB_DESCTYPE_STR},
    .unicode_string = {'R', 'i', 'n', 'S', 't', 'e', 'l'}
};
static const usb_desc_str s_product = {
    .header = {USB_STRING_LEN(16U), USB_DESCTYPE_STR},
    .unicode_string = {'D', 'A', 'P', 'L', 'i', 'n', 'k', '-', 'W',
                       'i', 'r', 'e', 'l', 'e', 's', 's'}
};
static const usb_desc_str s_serial = {
    .header = {USB_STRING_LEN(8U), USB_DESCTYPE_STR},
    .unicode_string = {'D', 'F', 'U', '-', '1', '2', '9', '1'}
};
static usb_desc_str s_interface;

static const uint8_t s_ms_os_string[18] = {
    18U, USB_DESCTYPE_STR,
    'M', 0U, 'S', 0U, 'F', 0U, 'T', 0U,
    '1', 0U, '0', 0U, '0', 0U,
    MS_OS_VENDOR_CODE, 0U
};

static const uint8_t s_ms_compat_id[40] = {
    40U, 0U, 0U, 0U,
    0x00U, 0x01U,
    0x04U, 0x00U,
    1U,
    0U, 0U, 0U, 0U, 0U, 0U, 0U,
    DFU_USB_INTERFACE,
    1U,
    'W', 'I', 'N', 'U', 'S', 'B', 0U, 0U,
    0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U,
    0U, 0U, 0U, 0U, 0U, 0U
};

static const ms_os_ext_property_desc_t s_ms_ext_property = {
    .length = sizeof(ms_os_ext_property_desc_t),
    .version = 0x0100U,
    .index = MS_OS_EXT_PROP_INDEX,
    .property_count = 1U,
    .property_size = sizeof(ms_os_ext_property_desc_t) - 10U,
    .property_type = 7U,
    .property_name_length = 42U,
    .property_name = {
        'D', 'e', 'v', 'i', 'c', 'e', 'I', 'n', 't', 'e', 'r', 'f',
        'a', 'c', 'e', 'G', 'U', 'I', 'D', 's', 0U
    },
    .property_data_length = 80U,
    .property_data = {
        '{', '8', '6', '2', '7', '4', 'D', '7', '3', '-',
        '5', '8', '3', '1', '-', '4', '9', '4', '3', '-',
        '9', '5', '2', 'E', '-', '7', '4', '3', '3', 'C',
        '7', '3', 'B', '9', '0', 'C', '4', '}', 0U, 0U
    }
};

static uint8_t *s_strings[DFU_USB_STRING_COUNT] = {
    [STR_IDX_LANGID] = (uint8_t *)&s_language,
    [STR_IDX_MFC] = (uint8_t *)&s_manufacturer,
    [STR_IDX_PRODUCT] = (uint8_t *)&s_product,
    [STR_IDX_SERIAL] = (uint8_t *)&s_serial,
    [STR_IDX_ITF] = (uint8_t *)&s_interface,
    [MS_OS_STRING_INDEX] = (uint8_t *)s_ms_os_string
};

static void interface_append_char(uint8_t *length, char value)
{
    if (*length < 64U) {
        s_interface.unicode_string[*length] = (uint16_t)(uint8_t)value;
        ++(*length);
    }
}

static void interface_append_text(uint8_t *length, const char *text)
{
    while (*text != '\0') {
        interface_append_char(length, *text);
        ++text;
    }
}

static void interface_append_hex32(uint8_t *length, uint32_t value)
{
    static const char digits[] = "0123456789ABCDEF";
    uint8_t shift;

    interface_append_text(length, "0x");
    for (shift = 28U;; shift -= 4U) {
        interface_append_char(length, digits[(value >> shift) & 0x0FU]);
        if (shift == 0U) {
            break;
        }
    }
}

static void interface_append_u32(uint8_t *length, uint32_t value)
{
    char digits[10];
    uint8_t count = 0U;

    do {
        digits[count++] = (char)('0' + value % 10U);
        value /= 10U;
    } while ((value != 0U) && (count < sizeof(digits)));
    while (count > 0U) {
        interface_append_char(length, digits[--count]);
    }
}

static void interface_prepare(const dfu_device_t *device)
{
    firmware_slot_t inactive_slot = FIRMWARE_SLOT_A;
    uint8_t length = 0U;

    memset(&s_interface, 0, sizeof(s_interface));
    if ((device != NULL) && (device->active_slot == FIRMWARE_SLOT_A)) {
        inactive_slot = FIRMWARE_SLOT_B;
    }
    interface_append_text(&length, "inactive=");
    interface_append_char(&length,
                          inactive_slot == FIRMWARE_SLOT_A ? 'A' : 'B');
    interface_append_text(&length, ";addr=");
    interface_append_hex32(&length, firmware_slot_base(inactive_slot));
    interface_append_text(&length, ";version=");
    interface_append_u32(&length,
                         device == NULL ? 0U : device->confirmed_version);
    interface_append_text(&length, ";mode=");
    interface_append_text(&length,
                          (device != NULL) && device->recovery_mode
                              ? "recovery" : "normal");
    s_interface.header.bLength = USB_STRING_LEN(length);
    s_interface.header.bDescriptorType = USB_DESCTYPE_STR;
}

usb_desc dfu_device_desc = {
    .dev_desc = (uint8_t *)&s_device_desc,
    .config_desc = (uint8_t *)&s_config_desc,
    .bos_desc = NULL,
    .strings = s_strings
};

static uint8_t dfu_usb_init(usb_dev *udev, uint8_t config_index);
static uint8_t dfu_usb_deinit(usb_dev *udev, uint8_t config_index);
static uint8_t dfu_usb_request(usb_dev *udev, usb_req *req);
static uint8_t dfu_usb_control_in(usb_dev *udev);
static uint8_t dfu_usb_control_out(usb_dev *udev);

usb_class dfu_device_class = {
    .req_cmd = DFU_USB_NO_CMD,
    .req_altset = 0U,
    .init = dfu_usb_init,
    .deinit = dfu_usb_deinit,
    .req_process = dfu_usb_request,
    .ctlx_in = dfu_usb_control_in,
    .ctlx_out = dfu_usb_control_out,
    .data_in = NULL,
    .data_out = NULL
};

static uint8_t dfu_usb_status_code(dfu_status_t status)
{
    switch (status) {
    case DFU_STATUS_OK: return 0x00U;
    case DFU_STATUS_ERR_TARGET: return 0x01U;
    case DFU_STATUS_ERR_FILE: return 0x02U;
    case DFU_STATUS_ERR_WRITE: return 0x03U;
    case DFU_STATUS_ERR_ERASE: return 0x04U;
    case DFU_STATUS_ERR_VERIFY: return 0x07U;
    case DFU_STATUS_ERR_ADDRESS: return 0x08U;
    case DFU_STATUS_ERR_FIRMWARE: return 0x0AU;
    case DFU_STATUS_ERR_SEQUENCE: return 0x0FU;
    default: return 0x0EU;
    }
}

static uint8_t dfu_usb_state_code(const dfu_device_t *device)
{
    switch (dfu_device_state(device)) {
    case DFU_STATE_IDLE: return 0x02U;
    case DFU_STATE_DNLOAD_SYNC: return 0x03U;
    case DFU_STATE_DNLOAD_IDLE: return 0x05U;
    case DFU_STATE_MANIFEST_SYNC: return 0x06U;
    case DFU_STATE_MANIFEST: return 0x07U;
    case DFU_STATE_MANIFEST_WAIT_RESET: return 0x08U;
    case DFU_STATE_ERROR: return 0x0AU;
    default: return 0x0AU;
    }
}

static uint8_t dfu_usb_init(usb_dev *udev, uint8_t config_index)
{
    (void)config_index;
    if (udev == NULL) {
        return USBD_FAIL;
    }
    udev->class_data[DFU_USB_INTERFACE] = s_device;
    return USBD_OK;
}

static uint8_t dfu_usb_deinit(usb_dev *udev, uint8_t config_index)
{
    (void)config_index;
    if (udev != NULL) {
        udev->class_data[DFU_USB_INTERFACE] = NULL;
    }
    return USBD_OK;
}

static uint8_t dfu_usb_request(usb_dev *udev, usb_req *req)
{
    if ((udev == NULL) || (req == NULL) || (s_device == NULL)) {
        return REQ_NOTSUPP;
    }
    if ((req->bmRequestType ==
             (USB_TRX_IN | USB_REQTYPE_VENDOR | USB_RECPTYPE_DEV)) &&
        (req->bRequest == MS_OS_VENDOR_CODE) &&
        (req->wValue == 0U) &&
        (req->wIndex == MS_OS_COMPAT_ID_INDEX)) {
        usb_transc_config(&udev->transc_in[0], (uint8_t *)s_ms_compat_id,
                          USB_MIN(req->wLength, sizeof(s_ms_compat_id)), 0U);
        return REQ_SUPP;
    }
    if ((req->bmRequestType ==
             (USB_TRX_IN | USB_REQTYPE_VENDOR | USB_RECPTYPE_ITF)) &&
        (req->bRequest == MS_OS_VENDOR_CODE) &&
        (req->wValue == DFU_USB_INTERFACE) &&
        (req->wIndex == MS_OS_EXT_PROP_INDEX)) {
        usb_transc_config(&udev->transc_in[0],
                          (uint8_t *)&s_ms_ext_property,
                          USB_MIN(req->wLength, sizeof(s_ms_ext_property)),
                          0U);
        return REQ_SUPP;
    }
    if (((req->bmRequestType & USB_RECPTYPE_MASK) != USB_RECPTYPE_ITF) ||
        (req->wIndex != DFU_USB_INTERFACE) ||
        ((req->bmRequestType & USB_REQTYPE_MASK) != USB_REQTYPE_CLASS)) {
        return REQ_NOTSUPP;
    }

    switch (req->bRequest) {
    case 1U: /* DFU_DNLOAD */
        if ((req->bmRequestType & USB_TRX_IN) != USB_TRX_OUT) {
            return REQ_NOTSUPP;
        }
        if (req->wLength == 0U) {
            return dfu_device_dnload(s_device, req->wValue, NULL, 0U) ==
                           DFU_STATUS_OK ? REQ_SUPP : REQ_NOTSUPP;
        }
        if ((req->wLength > DFU_TRANSFER_SIZE) ||
            (s_device->state == DFU_STATE_ERROR)) {
            return REQ_NOTSUPP;
        }
        s_pending_block = req->wValue;
        s_pending_length = req->wLength;
        usb_transc_config(&udev->transc_out[0], s_dnload_buffer,
                          s_pending_length, 0U);
        return REQ_SUPP;
    case 2U: /* DFU_UPLOAD is intentionally disabled. */
        return REQ_NOTSUPP;
    case 3U: /* DFU_GETSTATUS */
        if ((req->bmRequestType & USB_TRX_IN) != USB_TRX_IN ||
            req->wLength < DFU_USB_STATUS_LENGTH) {
            return REQ_NOTSUPP;
        }
        (void)dfu_device_get_status(s_device);
        memset(s_status_buffer, 0, sizeof(s_status_buffer));
        s_status_buffer[0] = dfu_usb_status_code(s_device->status);
        s_status_buffer[4] = dfu_usb_state_code(s_device);
        usb_transc_config(&udev->transc_in[0], s_status_buffer,
                          sizeof(s_status_buffer), 0U);
        return REQ_SUPP;
    case 4U: /* DFU_CLRSTATUS */
        if ((req->bmRequestType & USB_TRX_IN) ||
            (s_device->state != DFU_STATE_ERROR)) {
            return REQ_NOTSUPP;
        }
        dfu_device_clear_status(s_device);
        return REQ_SUPP;
    case 5U: /* DFU_GETSTATE */
        if ((req->bmRequestType & USB_TRX_IN) != USB_TRX_IN ||
            req->wLength < 1U) {
            return REQ_NOTSUPP;
        }
        s_status_buffer[0] = dfu_usb_state_code(s_device);
        usb_transc_config(&udev->transc_in[0], s_status_buffer, 1U, 0U);
        return REQ_SUPP;
    case 6U: /* DFU_ABORT */
        if ((req->bmRequestType & USB_TRX_IN) ||
            ((s_device->state != DFU_STATE_IDLE) &&
             (s_device->state != DFU_STATE_DNLOAD_SYNC) &&
             (s_device->state != DFU_STATE_DNLOAD_IDLE))) {
            return REQ_NOTSUPP;
        }
        dfu_device_abort(s_device);
        return REQ_SUPP;
    default:
        return REQ_NOTSUPP;
    }
}

static uint8_t dfu_usb_control_in(usb_dev *udev)
{
    if ((udev != NULL) && (s_device != NULL) &&
        (udev->control.req.bRequest == 3U) &&
        (dfu_device_state(s_device) == DFU_STATE_MANIFEST_WAIT_RESET)) {
        /* This callback runs after dfuMANIFEST-WAIT-RESET has reached the
         * host.  Resetting after the earlier dfuMANIFEST response makes
         * dfu-util report the expected disconnect as LIBUSB_ERROR_IO. */
        s_manifest_status_sent = true;
    }
    return USBD_OK;
}

static uint8_t dfu_usb_control_out(usb_dev *udev)
{
    if ((udev == NULL) || (s_device == NULL) || (s_pending_length == 0U)) {
        return USBD_OK;
    }
    (void)dfu_device_dnload(s_device, s_pending_block,
                            s_dnload_buffer, s_pending_length);
    s_pending_length = 0U;
    return USBD_OK;
}

void dfu_device_usb_bind(dfu_device_t *device)
{
    s_device = device;
    interface_prepare(device);
    s_manifest_status_sent = false;
}

void dfu_device_usb_unbind(void)
{
    s_device = NULL;
    s_usb = NULL;
    s_pending_length = 0U;
    s_manifest_status_sent = false;
}

void dfu_device_usb_init(usb_dev *udev)
{
    s_usb = udev;
    usbd_init(udev, &dfu_device_desc, &dfu_device_class);
}

void dfu_device_usb_irq(void)
{
    usbd_isr();
}

void dfu_device_usb_hp_irq(void)
{
    usbd_int_hpst();
}

void dfu_device_usb_wakeup_irq(usb_dev *udev)
{
    resume_mcu(udev);
}

bool dfu_device_usb_manifest_status_sent(void)
{
    return s_manifest_status_sent;
}

usb_dev *dfu_device_usb_current(void)
{
    return s_usb;
}

#endif /* DFU_DEVICE_USB_TARGET */
