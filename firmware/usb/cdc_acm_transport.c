#include "cdc_acm_transport.h"

#include <stddef.h>
#include <string.h>

#include "cdc_request_validation.h"
#include "usbd_conf.h"
#include "usbd_transc.h"

#define CDC_BUFFER_SIZE 64U

typedef struct {
    usb_dev *device;
    uint8_t rx_buffer[CDC_BUFFER_SIZE];
    uint8_t tx_buffer[CDC_BUFFER_SIZE];
    volatile uint16_t rx_length;
    volatile uint16_t rx_offset;
    acm_line line_coding;
    volatile bool line_coding_changed;
    volatile bool line_coding_pending;
    volatile bool tx_busy;
    volatile bool tx_zlp_pending;
} cdc_transport_t;

_Static_assert(CDC_ACM_DATA_PACKET_SIZE <= CDC_BUFFER_SIZE,
               "CDC RX packet exceeds transport buffer");
_Static_assert(CDC_BUFFER_SIZE <= UINT16_MAX,
               "CDC buffer length does not fit USB API");

static cdc_transport_t s_transport;

static uint8_t cdc_init(usb_dev *udev, uint8_t config_index);
static uint8_t cdc_deinit(usb_dev *udev, uint8_t config_index);
static uint8_t cdc_request(usb_dev *udev, usb_req *req);
static uint8_t cdc_control_out(usb_dev *udev);
static void cdc_data_in(usb_dev *udev, uint8_t ep_num);
static void cdc_data_out(usb_dev *udev, uint8_t ep_num);

static const usb_desc_ep s_in_desc = {
    .header = {sizeof(usb_desc_ep), USB_DESCTYPE_EP},
    .bEndpointAddress = CDC_IN_EP,
    .bmAttributes = USB_EP_ATTR_BULK,
    .wMaxPacketSize = CDC_ACM_DATA_PACKET_SIZE,
    .bInterval = 0U
};

static const usb_desc_ep s_out_desc = {
    .header = {sizeof(usb_desc_ep), USB_DESCTYPE_EP},
    .bEndpointAddress = CDC_OUT_EP,
    .bmAttributes = USB_EP_ATTR_BULK,
    .wMaxPacketSize = CDC_ACM_DATA_PACKET_SIZE,
    .bInterval = 0U
};

static const usb_desc_ep s_command_desc = {
    .header = {sizeof(usb_desc_ep), USB_DESCTYPE_EP},
    .bEndpointAddress = CDC_CMD_EP,
    .bmAttributes = USB_EP_ATTR_INT,
    .wMaxPacketSize = CDC_ACM_CMD_PACKET_SIZE,
    .bInterval = 10U
};

usb_class cdc_class = {
    .req_cmd = NO_CMD,
    .init = cdc_init,
    .deinit = cdc_deinit,
    .req_process = cdc_request,
    .ctlx_in = NULL,
    .ctlx_out = cdc_control_out,
    .data_in = cdc_data_in,
    .data_out = cdc_data_out
};

static void arm_receive(void)
{
    if ((s_transport.device != NULL) &&
        (s_transport.rx_offset == s_transport.rx_length)) {
        s_transport.rx_offset = 0U;
        s_transport.rx_length = 0U;
        usbd_ep_recev(s_transport.device, CDC_OUT_EP,
                      s_transport.rx_buffer,
                      CDC_ACM_DATA_PACKET_SIZE);
    }
}

static uint8_t cdc_init(usb_dev *udev, uint8_t config_index)
{
    (void)config_index;

    memset(&s_transport, 0, sizeof(s_transport));
    s_transport.device = udev;
    s_transport.line_coding.dwDTERate = 115200U;
    s_transport.line_coding.bDataBits = 8U;

    usbd_ep_init(udev, EP_BUF_SNG, CDC_BULK_TX_ADDR, &s_in_desc);
    usbd_ep_init(udev, EP_BUF_SNG, CDC_BULK_RX_ADDR, &s_out_desc);
    usbd_ep_init(udev, EP_BUF_SNG, CDC_INT_TX_ADDR, &s_command_desc);
    udev->ep_transc[EP_ID(CDC_IN_EP)][TRANSC_IN] = cdc_data_in;
    udev->ep_transc[EP_ID(CDC_OUT_EP)][TRANSC_OUT] = cdc_data_out;
    arm_receive();
    return USBD_OK;
}

static uint8_t cdc_deinit(usb_dev *udev, uint8_t config_index)
{
    (void)config_index;
    usbd_ep_deinit(udev, CDC_IN_EP);
    usbd_ep_deinit(udev, CDC_OUT_EP);
    usbd_ep_deinit(udev, CDC_CMD_EP);
    memset(&s_transport, 0, sizeof(s_transport));
    return USBD_OK;
}

static uint8_t cdc_request(usb_dev *udev, usb_req *req)
{
    if ((uint8_t)req->wIndex != CDC_COM_INTERFACE) {
        return REQ_NOTSUPP;
    }

    switch (req->bRequest) {
    case SET_LINE_CODING:
        if (!cdc_set_line_coding_request_valid(req)) {
            return REQ_NOTSUPP;
        }
        s_transport.line_coding_pending = true;
        usb_transc_config(&udev->transc_out[0],
                          (uint8_t *)&s_transport.line_coding,
                          sizeof(s_transport.line_coding), 0U);
        return REQ_SUPP;

    case GET_LINE_CODING:
        if (!cdc_get_line_coding_request_valid(req)) {
            return REQ_NOTSUPP;
        }
        usb_transc_config(&udev->transc_in[0],
                          (uint8_t *)&s_transport.line_coding,
                          sizeof(s_transport.line_coding),
                          0U);
        return REQ_SUPP;

    case SET_CONTROL_LINE_STATE:
        return cdc_control_line_state_request_valid(req)
                   ? REQ_SUPP
                   : REQ_NOTSUPP;

    default:
        return REQ_NOTSUPP;
    }
}

static uint8_t cdc_control_out(usb_dev *udev)
{
    (void)udev;
    if (s_transport.line_coding_pending) {
        s_transport.line_coding_pending = false;
        s_transport.line_coding_changed = true;
    }
    return USBD_OK;
}

static void cdc_data_in(usb_dev *udev, uint8_t ep_num)
{
    if (ep_num != EP_ID(CDC_IN_EP)) {
        return;
    }
    if (s_transport.tx_zlp_pending) {
        s_transport.tx_zlp_pending = false;
        usbd_ep_send(udev, CDC_IN_EP, NULL, 0U);
    } else {
        s_transport.tx_busy = false;
    }
}

static void cdc_data_out(usb_dev *udev, uint8_t ep_num)
{
    if (ep_num != EP_ID(CDC_OUT_EP)) {
        return;
    }
    s_transport.rx_offset = 0U;
    s_transport.rx_length =
        (uint16_t)udev->transc_out[ep_num].xfer_count;
    if (s_transport.rx_length == 0U) {
        arm_receive();
    }
}

uint16_t cdc_acm_read(uint8_t *data, uint16_t capacity)
{
    uint16_t available;
    uint16_t length;

    if ((data == NULL) || (capacity == 0U) ||
        (s_transport.rx_offset >= s_transport.rx_length)) {
        return 0U;
    }
    available = (uint16_t)(s_transport.rx_length -
                           s_transport.rx_offset);
    length = USB_MIN(available, capacity);
    memcpy(data, &s_transport.rx_buffer[s_transport.rx_offset], length);
    s_transport.rx_offset = (uint16_t)(s_transport.rx_offset + length);
    arm_receive();
    return length;
}

uint16_t cdc_acm_write(const uint8_t *data, uint16_t length)
{
    if ((data == NULL) || (length == 0U) ||
        (length > sizeof(s_transport.tx_buffer)) ||
        !cdc_acm_tx_ready()) {
        return 0U;
    }

    memcpy(s_transport.tx_buffer, data, length);
    s_transport.tx_busy = true;
    s_transport.tx_zlp_pending =
        (length % CDC_ACM_DATA_PACKET_SIZE) == 0U;
    usbd_ep_send(s_transport.device, CDC_IN_EP,
                 s_transport.tx_buffer, length);
    return length;
}

bool cdc_acm_tx_ready(void)
{
    return (s_transport.device != NULL) && !s_transport.tx_busy;
}

bool cdc_acm_line_coding_take(acm_line *line)
{
    if ((line == NULL) || !s_transport.line_coding_changed) {
        return false;
    }
    *line = s_transport.line_coding;
    s_transport.line_coding_changed = false;
    return true;
}
