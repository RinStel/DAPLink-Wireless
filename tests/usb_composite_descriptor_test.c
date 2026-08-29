#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "board.h"
#include "cdc_acm_core.h"
#include "cdc_request_validation.h"
#include "cmsis_dap_usb.h"
#include "device_config.h"
#include "firmware_version.h"
#include "usb_composite.h"
#include "usbd_enum.h"
#include "usbd_msc_core.h"

/* 按主机方式解析描述符：接口、端点和 OS 元数据必须保持一致。 */
#define MS_OS_VENDOR_CODE     0x20U
#define MS_OS_COMPAT_ID_INDEX 0x0004U
#define MS_OS_EXT_PROP_INDEX  0x0005U
#define MS_OS_STRING_INDEX    0xEEU

usb_class msc_class;
usb_class cdc_class;
usb_class cmsis_dap_usb_class;
static device_config_t s_device_config;
static unsigned int s_board_delay_calls;
static unsigned int s_board_led_calls;
static unsigned int s_class_init_calls;
static unsigned int s_standard_request_calls;

const device_config_t *device_config_get(void)
{
    return &s_device_config;
}

uint32_t board_device_id_hash(void)
{
    return 0x12345678U;
}

void board_delay_ms(uint32_t delay_ms)
{
    (void)delay_ms;
    ++s_board_delay_calls;
}

void board_led_set(board_led_t led, bool on)
{
    (void)led;
    (void)on;
    ++s_board_led_calls;
}

static uint8_t class_init_ok(usb_dev *udev, uint8_t config_index)
{
    (void)udev;
    (void)config_index;
    ++s_class_init_calls;
    return USBD_OK;
}

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
    static const char cmsis_dap_v2_guid[] =
        "{CDB3B5AD-293B-4663-AA36-1AAE46463776}";
    const uint8_t *device = composite_desc.dev_desc;
    const uint8_t *config = composite_desc.config_desc;
    const uint8_t *manufacturer = composite_desc.strings[STR_IDX_MFC];
    const uint8_t *product = composite_desc.strings[STR_IDX_PRODUCT];
    uint16_t total_length = decode_u16_le(&config[2]);
    uint16_t offset = 0U;
    uint8_t interface_count = 0U;
    uint8_t guid_index;
    uint8_t current_interface = 0xFFU;
    uint8_t dap_endpoint_count = 0U;
    bool dap_out_found = false;
    bool dap_in_found = false;
    bool msc_size_valid = false;
    bool cdc_size_valid = false;
    usb_dev udev = {0};
    usb_req request = {
        .bmRequestType = USB_TRX_IN | USB_REQTYPE_VENDOR |
                         USB_RECPTYPE_DEV,
        .bRequest = MS_OS_VENDOR_CODE,
        .wIndex = MS_OS_COMPAT_ID_INDEX,
        .wLength = 0xFFFFU
    };
    usb_req property_request = {
        .bmRequestType = USB_TRX_IN | USB_REQTYPE_VENDOR |
                         USB_RECPTYPE_ITF,
        .bRequest = MS_OS_VENDOR_CODE,
        .wValue = DAP_V2_INTERFACE,
        .wIndex = MS_OS_EXT_PROP_INDEX,
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

    s_device_config.device_mode = DEVICE_MODE_WIRED;
    usb_composite_prepare();

    msc_class.init = class_init_ok;
    cdc_class.init = class_init_ok;
    cmsis_dap_usb_class.init = class_init_ok;
    assert(composite_class.init(&udev, 0U) == USBD_OK);
    assert(s_class_init_calls == 3U);
    assert(s_board_delay_calls == 0U);
    assert(s_board_led_calls == 0U);

    usb_composite_prepare();
    assert(manufacturer[0] == USB_STRING_LEN(7U));
    assert(manufacturer[2] == 'R');
    assert(manufacturer[4] == 'i');
    assert(manufacturer[6] == 'n');
    assert(manufacturer[8] == 'S');
    assert(manufacturer[10] == 't');
    assert(manufacturer[12] == 'e');
    assert(manufacturer[14] == 'l');
    assert(product[0] == USB_STRING_LEN(9U));
    assert(product[2] == 'C');
    assert(product[4] == 'M');
    assert(product[6] == 'S');
    assert(product[8] == 'I');
    assert(product[10] == 'S');
    assert(product[12] == '-');
    assert(product[14] == 'D');
    assert(product[16] == 'A');
    assert(product[18] == 'P');
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
    assert(USBD_EP0_MAX_SIZE == 32U);
    assert(MSC_DATA_PACKET_SIZE == 64U);
    assert(CDC_ACM_DATA_PACKET_SIZE == 64U);
    assert(DAP_USB_PACKET_SIZE == 64U);
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
    assert(DAP_V2_RX_ADDR + DAP_USB_PACKET_SIZE == 0x01F8U);
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
    assert(device[7] == 32U);
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
        } else if ((descriptor_type == USB_DESCTYPE_EP) &&
                   (current_interface == USBD_MSC_INTERFACE)) {
            assert(decode_u16_le(&config[offset + 4U]) ==
                   MSC_DATA_PACKET_SIZE);
            msc_size_valid = true;
        } else if ((descriptor_type == USB_DESCTYPE_EP) &&
                   (current_interface == CDC_DATA_INTERFACE)) {
            assert(decode_u16_le(&config[offset + 4U]) ==
                   CDC_ACM_DATA_PACKET_SIZE);
            cdc_size_valid = true;
        }
        offset = (uint16_t)(offset + descriptor_length);
    }
    assert(offset == total_length);
    assert(interface_count == 4U);
    assert(dap_endpoint_count == 2U);
    assert(dap_out_found && dap_in_found);
    assert(msc_size_valid);
    assert(cdc_size_valid);

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

    assert(usbd_vendor_request(&udev, &property_request) == REQ_SUPP);
    assert(udev.transc_in[0].xfer_len == 146U);
    assert(udev.transc_in[0].xfer_buf[0] == 146U);
    assert(udev.transc_in[0].xfer_buf[6] == MS_OS_EXT_PROP_INDEX);
    assert(udev.transc_in[0].xfer_buf[8] == 1U);
    assert(udev.transc_in[0].xfer_buf[14] == 7U);
    assert(udev.transc_in[0].xfer_buf[18] == 42U);
    assert(udev.transc_in[0].xfer_buf[20] == 'D');
    assert(udev.transc_in[0].xfer_buf[22] == 'e');
    assert(udev.transc_in[0].xfer_buf[62] == 80U);
    for (guid_index = 0U;
         guid_index < sizeof(cmsis_dap_v2_guid) - 1U;
         ++guid_index) {
        assert(udev.transc_in[0].xfer_buf[66U + guid_index * 2U] ==
               (uint8_t)cmsis_dap_v2_guid[guid_index]);
        assert(udev.transc_in[0].xfer_buf[67U + guid_index * 2U] == 0U);
    }
    assert(udev.transc_in[0].xfer_buf[142] == 0U);
    assert(udev.transc_in[0].xfer_buf[144] == 0U);

    request.bmRequestType = USB_REQTYPE_VENDOR | USB_RECPTYPE_DEV;
    assert(usbd_vendor_request(&udev, &request) == REQ_NOTSUPP);
    request.bmRequestType = USB_TRX_IN | USB_REQTYPE_VENDOR |
                            USB_RECPTYPE_DEV;
    request.wValue = 1U;
    assert(usbd_vendor_request(&udev, &request) == REQ_NOTSUPP);

    s_device_config.device_mode = DEVICE_MODE_WIRELESS_SLAVE;
    usb_composite_prepare();
    config = composite_desc.config_desc;
    assert(decode_u16_le(&config[2]) == 32U);
    assert(config[4] == 1U);
    assert(config[9] == sizeof(usb_desc_itf));
    assert(config[11] == USBD_MSC_INTERFACE);
    assert(config[13] == 2U);
    assert(config[14] == USB_CLASS_MSC);
    assert(config[18] == sizeof(usb_desc_ep));
    assert(config[20] == MSC_IN_EP);
    assert(config[25] == sizeof(usb_desc_ep));
    assert(config[27] == MSC_OUT_EP);

    s_class_init_calls = 0U;
    assert(composite_class.init(&udev, 0U) == USBD_OK);
    assert(s_class_init_calls == 1U);

    request.bmRequestType = USB_TRX_IN | USB_REQTYPE_VENDOR |
                            USB_RECPTYPE_DEV;
    request.bRequest = MS_OS_VENDOR_CODE;
    request.wValue = 0U;
    request.wIndex = MS_OS_COMPAT_ID_INDEX;
    assert(composite_class.req_process(&udev, &request) == REQ_NOTSUPP);

    request.bmRequestType = USB_TRX_OUT | USB_REQTYPE_CLASS |
                            USB_RECPTYPE_ITF;
    request.wIndex = CDC_COM_INTERFACE;
    assert(composite_class.req_process(&udev, &request) == REQ_NOTSUPP);

    request.bmRequestType = USB_TRX_OUT | USB_REQTYPE_STRD |
                            USB_RECPTYPE_EP;
    request.wIndex = DAP_V2_OUT_EP;
    assert(composite_class.req_process(&udev, &request) == REQ_NOTSUPP);
    return 0;
}
