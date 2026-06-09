#include "usb_composite.h"

#include "cdc_acm_core.h"
#include "cdc_acm_transport.h"
#include "cmsis_dap_usb.h"
#include "firmware_version.h"
#include "usbd_msc_core.h"
#include "usbd_transc.h"

#include <stddef.h>

#define COMPOSITE_CONFIG_DESC_SIZE 121U
#define USB_DESCRIPTOR_TYPE_IAD    0x0BU
#define USB_CLASS_VENDOR_SPECIFIC  0xFFU
#define MS_OS_STRING_INDEX         0xEEU
#define MS_OS_VENDOR_CODE          0x20U
#define MS_OS_COMPAT_ID_INDEX      0x0004U

#pragma pack(1)
typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bFirstInterface;
    uint8_t bInterfaceCount;
    uint8_t bFunctionClass;
    uint8_t bFunctionSubClass;
    uint8_t bFunctionProtocol;
    uint8_t iFunction;
} usb_desc_iad_t;

typedef struct {
    usb_desc_config config;
    usb_desc_itf msc_itf;
    usb_desc_ep msc_epin;
    usb_desc_ep msc_epout;
    usb_desc_iad_t cdc_iad;
    usb_desc_itf cdc_cmd_itf;
    usb_desc_header_func cdc_header;
    usb_desc_call_managment_func cdc_call_management;
    usb_desc_acm_func cdc_acm;
    usb_desc_union_func cdc_union;
    usb_desc_ep cdc_cmd_ep;
    usb_desc_itf cdc_data_itf;
    usb_desc_ep cdc_out_ep;
    usb_desc_ep cdc_in_ep;
    usb_desc_itf dap_v2_itf;
    usb_desc_ep dap_v2_out_ep;
    usb_desc_ep dap_v2_in_ep;
} composite_config_desc_t;
#pragma pack()

_Static_assert(sizeof(composite_config_desc_t) ==
               COMPOSITE_CONFIG_DESC_SIZE,
               "Composite USB descriptor size mismatch");

static uint8_t composite_init(usb_dev *udev, uint8_t config_index);
static uint8_t composite_deinit(usb_dev *udev, uint8_t config_index);
static uint8_t composite_request(usb_dev *udev, usb_req *req);
static uint8_t composite_control_out(usb_dev *udev);

static const usb_desc_dev s_device_desc = {
    .header = {sizeof(usb_desc_dev), USB_DESCTYPE_DEV},
    .bcdUSB = 0x0200U,
    .bDeviceClass = 0xEFU,
    .bDeviceSubClass = 0x02U,
    .bDeviceProtocol = 0x01U,
    .bMaxPacketSize0 = USBD_EP0_MAX_SIZE,
    .idVendor = 0x28E9U,
    .idProduct = 0x1290U,
    .bcdDevice = FIRMWARE_USB_BCD_DEVICE,
    .iManufacturer = STR_IDX_MFC,
    .iProduct = STR_IDX_PRODUCT,
    .iSerialNumber = STR_IDX_SERIAL,
    .bNumberConfigurations = 1U
};

static const composite_config_desc_t s_config_desc = {
    .config = {
        .header = {sizeof(usb_desc_config), USB_DESCTYPE_CONFIG},
        .wTotalLength = COMPOSITE_CONFIG_DESC_SIZE,
        .bNumInterfaces = 4U,
        .bConfigurationValue = 1U,
        .iConfiguration = 0U,
        .bmAttributes = 0x80U,
        .bMaxPower = 0x32U
    },
    .msc_itf = {
        .header = {sizeof(usb_desc_itf), USB_DESCTYPE_ITF},
        .bInterfaceNumber = USBD_MSC_INTERFACE,
        .bAlternateSetting = 0U,
        .bNumEndpoints = 2U,
        .bInterfaceClass = USB_CLASS_MSC,
        .bInterfaceSubClass = USB_MSC_SUBCLASS_SCSI,
        .bInterfaceProtocol = USB_MSC_PROTOCOL_BBB,
        .iInterface = 0U
    },
    .msc_epin = {
        .header = {sizeof(usb_desc_ep), USB_DESCTYPE_EP},
        .bEndpointAddress = MSC_IN_EP,
        .bmAttributes = USB_EP_ATTR_BULK,
        .wMaxPacketSize = MSC_DATA_PACKET_SIZE,
        .bInterval = 0U
    },
    .msc_epout = {
        .header = {sizeof(usb_desc_ep), USB_DESCTYPE_EP},
        .bEndpointAddress = MSC_OUT_EP,
        .bmAttributes = USB_EP_ATTR_BULK,
        .wMaxPacketSize = MSC_DATA_PACKET_SIZE,
        .bInterval = 0U
    },
    .cdc_iad = {
        .bLength = sizeof(usb_desc_iad_t),
        .bDescriptorType = USB_DESCRIPTOR_TYPE_IAD,
        .bFirstInterface = CDC_COM_INTERFACE,
        .bInterfaceCount = 2U,
        .bFunctionClass = USB_CLASS_CDC,
        .bFunctionSubClass = USB_CDC_SUBCLASS_ACM,
        .bFunctionProtocol = USB_CDC_PROTOCOL_AT,
        .iFunction = 0U
    },
    .cdc_cmd_itf = {
        .header = {sizeof(usb_desc_itf), USB_DESCTYPE_ITF},
        .bInterfaceNumber = CDC_COM_INTERFACE,
        .bAlternateSetting = 0U,
        .bNumEndpoints = 1U,
        .bInterfaceClass = USB_CLASS_CDC,
        .bInterfaceSubClass = USB_CDC_SUBCLASS_ACM,
        .bInterfaceProtocol = USB_CDC_PROTOCOL_AT,
        .iInterface = 0U
    },
    .cdc_header = {
        .header = {sizeof(usb_desc_header_func), USB_DESCTYPE_CS_INTERFACE},
        .bDescriptorSubtype = 0U,
        .bcdCDC = 0x0110U
    },
    .cdc_call_management = {
        .header = {sizeof(usb_desc_call_managment_func),
                   USB_DESCTYPE_CS_INTERFACE},
        .bDescriptorSubtype = 1U,
        .bmCapabilities = 0U,
        .bDataInterface = CDC_DATA_INTERFACE
    },
    .cdc_acm = {
        .header = {sizeof(usb_desc_acm_func), USB_DESCTYPE_CS_INTERFACE},
        .bDescriptorSubtype = 2U,
        .bmCapabilities = 2U
    },
    .cdc_union = {
        .header = {sizeof(usb_desc_union_func), USB_DESCTYPE_CS_INTERFACE},
        .bDescriptorSubtype = 6U,
        .bMasterInterface = CDC_COM_INTERFACE,
        .bSlaveInterface0 = CDC_DATA_INTERFACE
    },
    .cdc_cmd_ep = {
        .header = {sizeof(usb_desc_ep), USB_DESCTYPE_EP},
        .bEndpointAddress = CDC_CMD_EP,
        .bmAttributes = USB_EP_ATTR_INT,
        .wMaxPacketSize = CDC_ACM_CMD_PACKET_SIZE,
        .bInterval = 10U
    },
    .cdc_data_itf = {
        .header = {sizeof(usb_desc_itf), USB_DESCTYPE_ITF},
        .bInterfaceNumber = CDC_DATA_INTERFACE,
        .bAlternateSetting = 0U,
        .bNumEndpoints = 2U,
        .bInterfaceClass = USB_CLASS_DATA,
        .bInterfaceSubClass = 0U,
        .bInterfaceProtocol = 0U,
        .iInterface = 0U
    },
    .cdc_out_ep = {
        .header = {sizeof(usb_desc_ep), USB_DESCTYPE_EP},
        .bEndpointAddress = CDC_OUT_EP,
        .bmAttributes = USB_EP_ATTR_BULK,
        .wMaxPacketSize = CDC_ACM_DATA_PACKET_SIZE,
        .bInterval = 0U
    },
    .cdc_in_ep = {
        .header = {sizeof(usb_desc_ep), USB_DESCTYPE_EP},
        .bEndpointAddress = CDC_IN_EP,
        .bmAttributes = USB_EP_ATTR_BULK,
        .wMaxPacketSize = CDC_ACM_DATA_PACKET_SIZE,
        .bInterval = 0U
    },
    .dap_v2_itf = {
        .header = {sizeof(usb_desc_itf), USB_DESCTYPE_ITF},
        .bInterfaceNumber = DAP_V2_INTERFACE,
        .bAlternateSetting = 0U,
        .bNumEndpoints = 2U,
        .bInterfaceClass = USB_CLASS_VENDOR_SPECIFIC,
        .bInterfaceSubClass = 0U,
        .bInterfaceProtocol = 0U,
        .iInterface = STR_IDX_CONFIG
    },
    .dap_v2_out_ep = {
        .header = {sizeof(usb_desc_ep), USB_DESCTYPE_EP},
        .bEndpointAddress = DAP_V2_OUT_EP,
        .bmAttributes = USB_EP_ATTR_BULK,
        .wMaxPacketSize = DAP_USB_PACKET_SIZE,
        .bInterval = 0U
    },
    .dap_v2_in_ep = {
        .header = {sizeof(usb_desc_ep), USB_DESCTYPE_EP},
        .bEndpointAddress = DAP_V2_IN_EP,
        .bmAttributes = USB_EP_ATTR_BULK,
        .wMaxPacketSize = DAP_USB_PACKET_SIZE,
        .bInterval = 0U
    }
};

static const usb_desc_LANGID s_language = {
    .header = {sizeof(usb_desc_LANGID), USB_DESCTYPE_STR},
    .wLANGID = ENG_LANGID
};

static const usb_desc_str s_manufacturer = {
    .header = {USB_STRING_LEN(7U), USB_DESCTYPE_STR},
    .unicode_string = {'D', 'A', 'P', 'L', 'i', 'n', 'k'}
};

static const usb_desc_str s_product = {
    .header = {USB_STRING_LEN(16U), USB_DESCTYPE_STR},
    .unicode_string = {
        'D', 'A', 'P', 'L', 'i', 'n', 'k', '-',
        'W', 'i', 'r', 'e', 'l', 'e', 's', 's'
    }
};

static usb_desc_str s_serial = {
    .header = {USB_STRING_LEN(12U), USB_DESCTYPE_STR}
};

static const usb_desc_str s_dap_v2_interface = {
    .header = {USB_STRING_LEN(12U), USB_DESCTYPE_STR},
    .unicode_string = {
        'C', 'M', 'S', 'I', 'S', '-', 'D', 'A', 'P', ' ', 'v', '2'
    }
};

static const usb_desc_header s_empty_string = {
    sizeof(usb_desc_header), USB_DESCTYPE_STR
};

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
    DAP_V2_INTERFACE,
    1U,
    'W', 'I', 'N', 'U', 'S', 'B', 0U, 0U,
    0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U,
    0U, 0U, 0U, 0U, 0U, 0U
};

/*
 * The GD32 USB stack indexes this table directly for GET_DESCRIPTOR(String).
 * Keep the Microsoft OS string at its standard 0xEE index without extending
 * or patching the vendor-owned usb_desc type.
 */
static uint8_t *s_strings[256] = {
    [STR_IDX_LANGID] = (uint8_t *)&s_language,
    [STR_IDX_MFC] = (uint8_t *)&s_manufacturer,
    [STR_IDX_PRODUCT] = (uint8_t *)&s_product,
    [STR_IDX_SERIAL] = (uint8_t *)&s_serial,
    [STR_IDX_CONFIG] = (uint8_t *)&s_dap_v2_interface,
    [MS_OS_STRING_INDEX] = (uint8_t *)s_ms_os_string
};

usb_desc composite_desc = {
    .dev_desc = (uint8_t *)&s_device_desc,
    .config_desc = (uint8_t *)&s_config_desc,
    .bos_desc = NULL,
    .strings = s_strings
};

usb_class composite_class = {
    .req_cmd = NO_CMD,
    .init = composite_init,
    .deinit = composite_deinit,
    .req_process = composite_request,
    .ctlx_out = composite_control_out
};

void usb_composite_prepare(void)
{
    uint16_t index;

    for (index = 0U; index < 256U; ++index) {
        if (s_strings[index] == NULL) {
            s_strings[index] = (uint8_t *)&s_empty_string;
        }
    }
}

static uint8_t composite_init(usb_dev *udev, uint8_t config_index)
{
    if (msc_class.init(udev, config_index) != USBD_OK) {
        return USBD_FAIL;
    }
    if (cdc_class.init(udev, config_index) != USBD_OK) {
        (void)msc_class.deinit(udev, config_index);
        return USBD_FAIL;
    }
    if (cmsis_dap_usb_class.init(udev, config_index) != USBD_OK) {
        (void)cdc_class.deinit(udev, config_index);
        (void)msc_class.deinit(udev, config_index);
        return USBD_FAIL;
    }
    return USBD_OK;
}

static uint8_t composite_deinit(usb_dev *udev, uint8_t config_index)
{
    uint8_t dap_status =
        cmsis_dap_usb_class.deinit(udev, config_index);
    uint8_t cdc_status = cdc_class.deinit(udev, config_index);
    uint8_t msc_status = msc_class.deinit(udev, config_index);

    return (dap_status == USBD_OK) &&
                   (cdc_status == USBD_OK) &&
                   (msc_status == USBD_OK)
               ? USBD_OK
               : USBD_FAIL;
}

static uint8_t composite_request(usb_dev *udev, usb_req *req)
{
    uint8_t recipient = req->bmRequestType & 0x1FU;
    uint8_t index = (uint8_t)req->wIndex;

    if (req->bmRequestType ==
            (USB_TRX_IN | USB_REQTYPE_VENDOR | USB_RECPTYPE_DEV) &&
        (req->bRequest == MS_OS_VENDOR_CODE) &&
        (req->wValue == 0U) &&
        (req->wIndex == MS_OS_COMPAT_ID_INDEX)) {
        usb_transc_config(
            &udev->transc_in[0], (uint8_t *)s_ms_compat_id,
            USB_MIN(req->wLength, sizeof(s_ms_compat_id)), 0U);
        return REQ_SUPP;
    }
    if (recipient == USB_RECPTYPE_ITF) {
        if (index == USBD_MSC_INTERFACE) {
            return msc_class.req_process(udev, req);
        }
        if ((index == CDC_COM_INTERFACE) ||
            (index == CDC_DATA_INTERFACE)) {
            return cdc_class.req_process(udev, req);
        }
        return cmsis_dap_usb_class.req_process(udev, req);
    }
    if (recipient == USB_RECPTYPE_EP) {
        if ((index == MSC_IN_EP) || (index == MSC_OUT_EP)) {
            return msc_class.req_process(udev, req);
        }
        if ((index == CDC_IN_EP) || (index == CDC_OUT_EP) ||
            (index == CDC_CMD_EP)) {
            return cdc_class.req_process(udev, req);
        }
        return cmsis_dap_usb_class.req_process(udev, req);
    }
    return REQ_NOTSUPP;
}

static uint8_t composite_control_out(usb_dev *udev)
{
    return cdc_class.ctlx_out != NULL
               ? cdc_class.ctlx_out(udev)
               : USBD_OK;
}
