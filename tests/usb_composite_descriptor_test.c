#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "cdc_acm_core.h"
#include "cdc_request_validation.h"
#include "cmsis_dap_usb.h"
#include "firmware_version.h"
#include "usb_composite.h"
#include "usbd_enum.h"
#include "usbd_msc_core.h"

#define MS_OS_VENDOR_CODE     0x20U
#define MS_OS_COMPAT_ID_INDEX 0x0004U
#define MS_OS_STRING_INDEX    0xEEU

usb_class msc_class;
usb_class cdc_class;
usb_class cmsis_dap_usb_class;
static unsigned int s_standard_request_calls;

usb_reqsta gd32_usbd_standard_request_unchecked(
    usb_dev *udev, usb_req *request)
{
    (void)udev;
    (void)request;
    ++s_standard_request_calls;
    return REQ_SUPP;
}

static uint16_t decode_u16_le(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

int main(void)
{
    const uint8_t *device = composite_desc.dev_desc;
    const uint8_t *config = composite_desc.config_desc;
    uint16_t total_length = decode_u16_le(&config[2]);
    uint16_t offset = 0U;
    uint8_t interface_count = 0U;
    uint8_t current_interface = 0xFFU;
    uint8_t dap_endpoint_count = 0U;
    bool dap_out_found = false;
    bool dap_in_found = false;
    usb_dev udev = {0};
    usb_req request = {
        .bmRequestType = USB_TRX_IN | USB_REQTYPE_VENDOR |
                         USB_RECPTYPE_DEV,
        .bRequest = MS_OS_VENDOR_CODE,
        .wIndex = MS_OS_COMPAT_ID_INDEX,
        .wLength = 0xFFFFU
    };
    usb_req line_request = {
        .bmRequestType = USB_TRX_OUT | USB_REQTYPE_CLASS |
                         USB_RECPTYPE_ITF,
        .wIndex = CDC_COM_INTERFACE,
        .wLength = 7U
    };
    usb_req standard_request = {
        .bRequest = USB_GET_STATUS
    };

    usb_composite_prepare();
    assert(cdc_set_line_coding_request_valid(&line_request));
    line_request.wLength = 8U;
    assert(!cdc_set_line_coding_request_valid(&line_request));
    line_request.wLength = 0xFFFFU;
    assert(!cdc_set_line_coding_request_valid(&line_request));
    line_request.wLength = 7U;
    line_request.wIndex = CDC_DATA_INTERFACE;
    assert(!cdc_set_line_coding_request_valid(&line_request));
    line_request.wIndex = CDC_COM_INTERFACE;
    line_request.bmRequestType = USB_TRX_IN | USB_REQTYPE_CLASS |
                                 USB_RECPTYPE_ITF;
    assert(!cdc_set_line_coding_request_valid(&line_request));
    assert(cdc_get_line_coding_request_valid(&line_request));
    line_request.wLength = 6U;
    assert(!cdc_get_line_coding_request_valid(&line_request));
    line_request.wLength = 0U;
    line_request.bmRequestType = USB_TRX_OUT | USB_REQTYPE_CLASS |
                                 USB_RECPTYPE_ITF;
    assert(cdc_control_line_state_request_valid(&line_request));
    line_request.bmRequestType = USB_TRX_IN | USB_REQTYPE_CLASS |
                                 USB_RECPTYPE_ITF;
    assert(!cdc_control_line_state_request_valid(&line_request));

    assert(usbd_standard_request(&udev, &standard_request) == REQ_SUPP);
    assert(s_standard_request_calls == 1U);
    standard_request.bRequest = 0xFFU;
    assert(usbd_standard_request(&udev, &standard_request) ==
           REQ_NOTSUPP);
    assert(s_standard_request_calls == 1U);
    assert(usbd_standard_request(NULL, &standard_request) ==
           REQ_NOTSUPP);
    assert(usbd_standard_request(&udev, NULL) == REQ_NOTSUPP);
    standard_request.bRequest = USB_GET_STATUS;
    standard_request.bmRequestType = USB_RECPTYPE_ITF;
    standard_request.wIndex = USBD_ITF_MAX_NUM;
    assert(usbd_standard_request(&udev, &standard_request) ==
           REQ_NOTSUPP);
    standard_request.bmRequestType = USB_RECPTYPE_EP;
    standard_request.wIndex = EP_COUNT;
    assert(usbd_standard_request(&udev, &standard_request) ==
           REQ_NOTSUPP);
    standard_request.wIndex = 0x0070U;
    assert(usbd_standard_request(&udev, &standard_request) ==
           REQ_NOTSUPP);
    assert(s_standard_request_calls == 1U);

    assert(EP_COUNT == 6U);
    assert(EP0_TX_ADDR >= EP_COUNT * 8U);
    assert(EP0_RX_ADDR >= EP0_TX_ADDR + USBD_EP0_MAX_SIZE);
    assert(BULK_TX_ADDR >= EP0_RX_ADDR + USBD_EP0_MAX_SIZE);
    assert(BULK_RX_ADDR >= BULK_TX_ADDR + MSC_DATA_PACKET_SIZE);
    assert(CDC_BULK_TX_ADDR >= BULK_RX_ADDR + MSC_DATA_PACKET_SIZE);
    assert(CDC_BULK_RX_ADDR >=
           CDC_BULK_TX_ADDR + CDC_ACM_DATA_PACKET_SIZE);
    assert(CDC_INT_TX_ADDR >=
           CDC_BULK_RX_ADDR + CDC_ACM_DATA_PACKET_SIZE);
    assert(DAP_V2_TX_ADDR >=
           CDC_INT_TX_ADDR + CDC_ACM_CMD_PACKET_SIZE);
    assert(DAP_V2_RX_ADDR >= DAP_V2_TX_ADDR + DAP_USB_PACKET_SIZE);
    assert(DAP_V2_RX_ADDR + DAP_USB_PACKET_SIZE <= 512U);
    assert(EP_ID(MSC_IN_EP) < EP_COUNT);
    assert(EP_ID(MSC_OUT_EP) < EP_COUNT);
    assert(EP_ID(CDC_IN_EP) < EP_COUNT);
    assert(EP_ID(CDC_OUT_EP) < EP_COUNT);
    assert(EP_ID(CDC_CMD_EP) < EP_COUNT);
    assert(EP_ID(DAP_V2_IN_EP) < EP_COUNT);
    assert(EP_ID(DAP_V2_OUT_EP) < EP_COUNT);

    assert(device[0] == sizeof(usb_desc_dev));
    assert(device[1] == USB_DESCTYPE_DEV);
    assert(decode_u16_le(&device[8]) == 0x28E9U);
    assert(decode_u16_le(&device[10]) == 0x1290U);
    assert(decode_u16_le(&device[12]) == FIRMWARE_USB_BCD_DEVICE);
    assert(device[17] == 1U);

    assert(config[0] == sizeof(usb_desc_config));
    assert(config[1] == USB_DESCTYPE_CONFIG);
    assert(total_length == 121U);
    assert(config[4] == 4U);
    while (offset < total_length) {
        uint8_t descriptor_length = config[offset];
        uint8_t descriptor_type;

        assert(descriptor_length >= 2U);
        assert((uint16_t)(offset + descriptor_length) <= total_length);
        descriptor_type = config[offset + 1U];
        if (descriptor_type == USB_DESCTYPE_ITF) {
            current_interface = config[offset + 2U];
            assert(current_interface == interface_count);
            ++interface_count;
            if (current_interface == DAP_V2_INTERFACE) {
                assert(config[offset + 4U] == 2U);
                assert(config[offset + 5U] == 0xFFU);
                assert(config[offset + 8U] == STR_IDX_CONFIG);
            }
        } else if ((descriptor_type == USB_DESCTYPE_EP) &&
                   (current_interface == DAP_V2_INTERFACE)) {
            uint8_t endpoint = config[offset + 2U];

            assert((config[offset + 3U] & 0x03U) == USB_EP_ATTR_BULK);
            assert(decode_u16_le(&config[offset + 4U]) ==
                   DAP_USB_PACKET_SIZE);
            dap_out_found |= endpoint == DAP_V2_OUT_EP;
            dap_in_found |= endpoint == DAP_V2_IN_EP;
            ++dap_endpoint_count;
        }
        offset = (uint16_t)(offset + descriptor_length);
    }
    assert(offset == total_length);
    assert(interface_count == 4U);
    assert(dap_endpoint_count == 2U);
    assert(dap_out_found && dap_in_found);

    assert(composite_desc.strings[STR_IDX_CONFIG][0] ==
           USB_STRING_LEN(12U));
    assert(composite_desc.strings[MS_OS_STRING_INDEX][0] == 18U);
    assert(composite_desc.strings[MS_OS_STRING_INDEX][16] ==
           MS_OS_VENDOR_CODE);
    assert(composite_desc.strings[0xFFU][0] ==
           sizeof(usb_desc_header));

    udev.class_core = &composite_class;
    assert(usbd_vendor_request(&udev, &request) == REQ_SUPP);
    assert(udev.transc_in[0].xfer_len == 40U);
    assert(udev.transc_in[0].xfer_buf[16] == DAP_V2_INTERFACE);
    assert(memcmp(&udev.transc_in[0].xfer_buf[18], "WINUSB", 6U) == 0);

    request.bmRequestType = USB_REQTYPE_VENDOR | USB_RECPTYPE_DEV;
    assert(usbd_vendor_request(&udev, &request) == REQ_NOTSUPP);
    request.bmRequestType = USB_TRX_IN | USB_REQTYPE_VENDOR |
                            USB_RECPTYPE_DEV;
    request.wValue = 1U;
    assert(usbd_vendor_request(&udev, &request) == REQ_NOTSUPP);
    return 0;
}
