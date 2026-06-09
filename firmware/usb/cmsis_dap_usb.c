#include "cmsis_dap_usb.h"

#include <stddef.h>
#include <string.h>

#include "cmsis_dap.h"
#include "usbd_conf.h"
#include "usbd_enum.h"
#include "usbd_transc.h"

#define NO_CMD             0xFFU
#define DAP_TRANSFER_ABORT 0x07U

typedef struct {
    uint8_t request[DAP_USB_PACKET_SIZE];
    uint8_t abort_request[DAP_USB_PACKET_SIZE];
    uint8_t response[DAP_USB_PACKET_SIZE];
    volatile uint8_t request_length;
    volatile bool request_pending;
    volatile bool tx_busy;
    volatile bool abort_receive_active;
} dap_usb_transport_t;

static usb_dev *s_usb_device;
static dap_usb_transport_t s_transport;

static uint8_t dap_usb_init(usb_dev *udev, uint8_t config_index);
static uint8_t dap_usb_deinit(usb_dev *udev, uint8_t config_index);
static uint8_t dap_usb_request(usb_dev *udev, usb_req *req);
static void dap_usb_data_in(usb_dev *udev, uint8_t ep_num);
static void dap_usb_data_out(usb_dev *udev, uint8_t ep_num);

usb_class cmsis_dap_usb_class = {
    .req_cmd = NO_CMD,
    .init = dap_usb_init,
    .deinit = dap_usb_deinit,
    .req_process = dap_usb_request,
    .ctlx_in = NULL,
    .ctlx_out = NULL,
    .data_in = dap_usb_data_in,
    .data_out = dap_usb_data_out
};

static const usb_desc_ep s_in_desc = {
    .header = {sizeof(usb_desc_ep), USB_DESCTYPE_EP},
    .bEndpointAddress = DAP_V2_IN_EP,
    .bmAttributes = USB_EP_ATTR_BULK,
    .wMaxPacketSize = DAP_USB_PACKET_SIZE,
    .bInterval = 0U
};

static const usb_desc_ep s_out_desc = {
    .header = {sizeof(usb_desc_ep), USB_DESCTYPE_EP},
    .bEndpointAddress = DAP_V2_OUT_EP,
    .bmAttributes = USB_EP_ATTR_BULK,
    .wMaxPacketSize = DAP_USB_PACKET_SIZE,
    .bInterval = 0U
};

static void receive_arm(uint8_t *buffer, bool abort_receive)
{
    s_transport.abort_receive_active = abort_receive;
    usbd_ep_recev(s_usb_device, DAP_V2_OUT_EP, buffer,
                  DAP_USB_PACKET_SIZE);
}

static uint8_t dap_usb_init(usb_dev *udev, uint8_t config_index)
{
    (void)config_index;
    usbd_ep_init(udev, EP_BUF_SNG, DAP_V2_TX_ADDR, &s_in_desc);
    usbd_ep_init(udev, EP_BUF_SNG, DAP_V2_RX_ADDR, &s_out_desc);

    udev->ep_transc[EP_ID(DAP_V2_IN_EP)][TRANSC_IN] =
        cmsis_dap_usb_class.data_in;
    udev->ep_transc[EP_ID(DAP_V2_OUT_EP)][TRANSC_OUT] =
        cmsis_dap_usb_class.data_out;

    memset(&s_transport, 0, sizeof(s_transport));
    s_usb_device = udev;
    cmsis_dap_init();
    receive_arm(s_transport.request, false);
    return USBD_OK;
}

static uint8_t dap_usb_deinit(usb_dev *udev, uint8_t config_index)
{
    (void)config_index;
    usbd_ep_deinit(udev, DAP_V2_IN_EP);
    usbd_ep_deinit(udev, DAP_V2_OUT_EP);
    s_usb_device = NULL;
    return USBD_OK;
}

static uint8_t dap_usb_request(usb_dev *udev, usb_req *req)
{
    (void)udev;
    return ((uint8_t)req->wIndex == DAP_V2_INTERFACE)
               ? REQ_SUPP
               : REQ_NOTSUPP;
}

static void dap_usb_data_in(usb_dev *udev, uint8_t ep_num)
{
    (void)udev;
    if (ep_num != EP_ID(DAP_V2_IN_EP)) {
        return;
    }
    s_transport.tx_busy = false;
    receive_arm(s_transport.request, false);
}

static void dap_usb_data_out(usb_dev *udev, uint8_t ep_num)
{
    uint8_t length;

    if (ep_num != EP_ID(DAP_V2_OUT_EP)) {
        return;
    }
    length = (uint8_t)udev->transc_out[ep_num].xfer_count;

    if (s_transport.abort_receive_active) {
        if ((length != 0U) &&
            (s_transport.abort_request[0] == DAP_TRANSFER_ABORT)) {
            cmsis_dap_abort();
        }
        return;
    }
    if ((length != 0U) &&
        (s_transport.request[0] == DAP_TRANSFER_ABORT)) {
        cmsis_dap_abort();
        receive_arm(s_transport.request, false);
        return;
    }
    if (length == 0U) {
        receive_arm(s_transport.request, false);
        return;
    }
    s_transport.request_length = length;
    s_transport.request_pending = true;
    receive_arm(s_transport.abort_request, true);
}

void cmsis_dap_usb_process(void)
{
    uint8_t length;

    if (s_usb_device == NULL) {
        return;
    }
    if (s_transport.request_pending &&
        cmsis_dap_submit(s_transport.request,
                         s_transport.request_length)) {
        s_transport.request_pending = false;
    }
    cmsis_dap_process();
    if (s_transport.tx_busy ||
        !cmsis_dap_response_take(s_transport.response, &length)) {
        return;
    }
    s_transport.tx_busy = true;
    usbd_ep_send(s_usb_device, DAP_V2_IN_EP, s_transport.response,
                 length);
}

bool cmsis_dap_usb_idle(void)
{
    return !s_transport.request_pending &&
           !s_transport.tx_busy &&
           !cmsis_dap_busy();
}
