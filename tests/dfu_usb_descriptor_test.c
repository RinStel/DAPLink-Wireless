#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "dfu_device.h"
#include "usbd_enum.h"

#define MS_OS_STRING_INDEX    0xEEU
#define MS_OS_VENDOR_CODE     0x20U
#define MS_OS_COMPAT_ID_INDEX 0x0004U
#define MS_OS_EXT_PROP_INDEX  0x0005U

dfu_flash_result_t dfu_flash_begin(dfu_flash_session_t *session,
                                   const firmware_image_header_t *header,
                                   firmware_slot_t active_slot,
                                   uint32_t confirmed_version,
                                   bool recovery_mode)
{
    (void)session;
    (void)header;
    (void)active_slot;
    (void)confirmed_version;
    (void)recovery_mode;
    return DFU_FLASH_OK;
}

dfu_flash_result_t dfu_flash_write_block(dfu_flash_session_t *session,
                                         uint32_t offset,
                                         const uint8_t *data,
                                         size_t length)
{
    (void)session;
    (void)offset;
    (void)data;
    (void)length;
    return DFU_FLASH_OK;
}

dfu_flash_result_t dfu_flash_finish(dfu_flash_session_t *session)
{
    (void)session;
    return DFU_FLASH_OK;
}

void dfu_flash_abort(dfu_flash_session_t *session)
{
    (void)session;
}

void usbd_init(usb_dev *udev, usb_desc *desc, usb_class *usbc)
{
    (void)udev;
    (void)desc;
    (void)usbc;
}

void usbd_isr(void)
{
}

void usbd_int_hpst(void)
{
}

static void assert_usb_string(const uint8_t *descriptor,
                              const char *expected)
{
    size_t length = strlen(expected);
    size_t index;

    assert(descriptor != NULL);
    assert(descriptor[0] == USB_STRING_LEN(length));
    assert(descriptor[1] == USB_DESCTYPE_STR);
    for (index = 0U; index < length; ++index) {
        assert(descriptor[2U + index * 2U] == (uint8_t)expected[index]);
        assert(descriptor[3U + index * 2U] == 0U);
    }
}

int main(void)
{
    static const uint8_t expected_ms_os_name[] = {
        'M', 0U, 'S', 0U, 'F', 0U, 'T', 0U,
        '1', 0U, '0', 0U, '0', 0U
    };
    static const uint8_t expected_guid_utf16[] = {
        '{', 0U, '8', 0U, '6', 0U, '2', 0U, '7', 0U, '4', 0U,
        'D', 0U, '7', 0U, '3', 0U, '-', 0U, '5', 0U, '8', 0U,
        '3', 0U, '1', 0U, '-', 0U, '4', 0U, '9', 0U, '4', 0U,
        '3', 0U, '-', 0U, '9', 0U, '5', 0U, '2', 0U, 'E', 0U,
        '-', 0U, '7', 0U, '4', 0U, '3', 0U, '3', 0U, 'C', 0U,
        '7', 0U, '3', 0U, 'B', 0U, '9', 0U, '0', 0U, 'C', 0U,
        '4', 0U, '}', 0U, 0U, 0U
    };
    dfu_device_t device;
    usb_dev udev = {0};
    usb_req compatible_request = {
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
        .wValue = 0U,
        .wIndex = MS_OS_EXT_PROP_INDEX,
        .wLength = 0xFFFFU
    };
    usb_req invalid_request = compatible_request;
    const uint8_t *os_string;

    dfu_device_init(&device, FIRMWARE_SLOT_A, 799U, false);
    dfu_device_usb_bind(&device);
    assert(dfu_device_desc.config_desc[20] == 0x01U);
    assert_usb_string(dfu_device_desc.strings[STR_IDX_ITF],
                      "inactive=B;addr=0x08021000;version=799;mode=normal");

    os_string = dfu_device_desc.strings[MS_OS_STRING_INDEX];
    assert(os_string != NULL);
    assert(os_string[0] == 18U);
    assert(os_string[1] == USB_DESCTYPE_STR);
    assert(memcmp(&os_string[2], expected_ms_os_name,
                  sizeof(expected_ms_os_name)) == 0);
    assert(os_string[16] == MS_OS_VENDOR_CODE);

    assert(dfu_device_class.req_process(&udev, &compatible_request) ==
           REQ_SUPP);
    assert(udev.transc_in[0].xfer_len == 40U);
    assert(udev.transc_in[0].xfer_buf[16] == 0U);
    assert(memcmp(&udev.transc_in[0].xfer_buf[18], "WINUSB", 6U) == 0);

    assert(dfu_device_class.req_process(&udev, &property_request) ==
           REQ_SUPP);
    assert(udev.transc_in[0].xfer_len == 146U);
    assert(memcmp(&udev.transc_in[0].xfer_buf[66], expected_guid_utf16,
                  sizeof(expected_guid_utf16)) == 0);

    invalid_request.bRequest = MS_OS_VENDOR_CODE + 1U;
    assert(dfu_device_class.req_process(&udev, &invalid_request) ==
           REQ_NOTSUPP);

    dfu_device_init(&device, FIRMWARE_SLOT_NONE, 0U, true);
    dfu_device_usb_bind(&device);
    assert_usb_string(dfu_device_desc.strings[STR_IDX_ITF],
                      "inactive=A;addr=0x08004000;version=0;mode=recovery");
    return 0;
}
