/*
 * DAPLink-Wireless — Wireless CMSIS-DAP v2 debug probe firmware
 * Copyright (C) 2025 RinStel <me@rinx.nz>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "cdc_acm_transport.h"

#include <stddef.h>
#include <string.h>

#include "cdc_request_validation.h"
#include "usbd_conf.h"
#include "usbd_transc.h"

/* USB 回调拥有端点缓冲区；主循环可以把多个业务写入排队，但一次只提交一个
 * 64 字节 USB IN 包。 */
#define CDC_BUFFER_SIZE      64U
#define CDC_TX_QUEUE_SIZE    512U
#define CDC_RX_QUEUE_SIZE    512U

typedef struct {
    usb_dev *device;
    uint8_t rx_buffer[CDC_BUFFER_SIZE];
    uint8_t rx_queue[CDC_RX_QUEUE_SIZE];
    uint8_t tx_packet[CDC_BUFFER_SIZE];
    uint8_t tx_queue[CDC_TX_QUEUE_SIZE];
    volatile uint16_t rx_head;
    volatile uint16_t rx_tail;
    acm_line line_coding;
    volatile bool line_coding_changed;
    volatile bool line_coding_pending;
    volatile bool rx_armed;
    volatile bool tx_busy;
    volatile uint16_t tx_head;
    volatile uint16_t tx_tail;
    volatile uint16_t tx_packet_length;
} cdc_transport_t;

_Static_assert(CDC_ACM_DATA_PACKET_SIZE <= CDC_BUFFER_SIZE,
               "CDC RX packet exceeds transport buffer");
_Static_assert(CDC_BUFFER_SIZE <= UINT16_MAX,
               "CDC buffer length does not fit USB API");
_Static_assert(CDC_TX_QUEUE_SIZE <= UINT16_MAX,
               "CDC TX queue length does not fit USB API");
_Static_assert(CDC_RX_QUEUE_SIZE <= UINT16_MAX,
               "CDC RX queue length does not fit USB API");

static cdc_transport_t s_transport;

static uint8_t cdc_init(usb_dev *udev, uint8_t config_index);
static uint8_t cdc_deinit(usb_dev *udev, uint8_t config_index);
static uint8_t cdc_request(usb_dev *udev, usb_req *req);
static uint8_t cdc_control_out(usb_dev *udev);
static void cdc_data_in(usb_dev *udev, uint8_t ep_num);
static void cdc_data_out(usb_dev *udev, uint8_t ep_num);
static void tx_start(void);

static uint16_t tx_free_unchecked(void)
{
    uint16_t used = (uint16_t)(s_transport.tx_tail -
                               s_transport.tx_head);

    return (uint16_t)(CDC_TX_QUEUE_SIZE - used);
}

static void tx_queue_consume(uint16_t length)
{
    s_transport.tx_head =
        (uint16_t)(s_transport.tx_head + length);
}

static void tx_packet_prepare(void)
{
    uint16_t length;
    uint16_t first;

    length = USB_MIN((uint16_t)(s_transport.tx_tail -
                                s_transport.tx_head),
                     CDC_BUFFER_SIZE);
    first = USB_MIN(length,
                    (uint16_t)(CDC_TX_QUEUE_SIZE -
                               (s_transport.tx_head %
                                CDC_TX_QUEUE_SIZE)));
    memcpy(s_transport.tx_packet,
           &s_transport.tx_queue[s_transport.tx_head %
                                 CDC_TX_QUEUE_SIZE], first);
    if (first < length) {
        memcpy(&s_transport.tx_packet[first], s_transport.tx_queue,
               (size_t)(length - first));
    }
    s_transport.tx_packet_length = length;
}

static void tx_start(void)
{
    if ((s_transport.device == NULL) || s_transport.tx_busy) {
        return;
    }
    if (s_transport.tx_head == s_transport.tx_tail) {
        return;
    }
    tx_packet_prepare();
    s_transport.tx_busy = true;
    usbd_ep_send(s_transport.device, CDC_IN_EP,
                 s_transport.tx_packet, s_transport.tx_packet_length);
}

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
    /* 回调先把 OUT 包复制到软件环形缓冲，再立即重新接收。缓冲剩余空间不足
     * 一个最大包时使用 USB NAK 形成背压。 */
    if ((s_transport.device != NULL) && !s_transport.rx_armed &&
        ((CDC_RX_QUEUE_SIZE -
          (uint16_t)(s_transport.rx_tail - s_transport.rx_head)) >=
         CDC_ACM_DATA_PACKET_SIZE)) {
        s_transport.rx_armed = true;
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
    (void)udev;
    if (ep_num != EP_ID(CDC_IN_EP)) {
        return;
    }
    if (s_transport.tx_packet_length == 0U) {
        s_transport.tx_busy = false;
        return;
    }
    tx_queue_consume(s_transport.tx_packet_length);
    s_transport.tx_packet_length = 0U;
    s_transport.tx_busy = false;
    tx_start();
}

static void cdc_data_out(usb_dev *udev, uint8_t ep_num)
{
    uint16_t length;
    uint16_t first;

    if (ep_num != EP_ID(CDC_OUT_EP)) {
        return;
    }
    s_transport.rx_armed = false;
    length = (uint16_t)udev->transc_out[ep_num].xfer_count;
    if (length >
        (CDC_RX_QUEUE_SIZE -
         (uint16_t)(s_transport.rx_tail - s_transport.rx_head))) {
        return;
    }
    first = USB_MIN(length,
                    (uint16_t)(CDC_RX_QUEUE_SIZE -
                               (s_transport.rx_tail %
                                CDC_RX_QUEUE_SIZE)));
    memcpy(&s_transport.rx_queue[s_transport.rx_tail %
                                 CDC_RX_QUEUE_SIZE],
           s_transport.rx_buffer, first);
    if (first < length) {
        memcpy(s_transport.rx_queue, &s_transport.rx_buffer[first],
               (size_t)(length - first));
    }
    s_transport.rx_tail =
        (uint16_t)(s_transport.rx_tail + length);
    arm_receive();
}

uint16_t cdc_acm_read(uint8_t *data, uint16_t capacity)
{
    uint16_t length;
    uint16_t first;

    if ((data == NULL) || (capacity == 0U) ||
        (s_transport.rx_head == s_transport.rx_tail)) {
        return 0U;
    }
    length = USB_MIN((uint16_t)(s_transport.rx_tail -
                                s_transport.rx_head), capacity);
    first = USB_MIN(length,
                    (uint16_t)(CDC_RX_QUEUE_SIZE -
                               (s_transport.rx_head %
                                CDC_RX_QUEUE_SIZE)));
    memcpy(data, &s_transport.rx_queue[s_transport.rx_head %
                                       CDC_RX_QUEUE_SIZE], first);
    if (first < length) {
        memcpy(&data[first], s_transport.rx_queue,
               (size_t)(length - first));
    }
    s_transport.rx_head =
        (uint16_t)(s_transport.rx_head + length);
    arm_receive();
    return length;
}

uint16_t cdc_acm_write(const uint8_t *data, uint16_t length)
{
    uint16_t first;

    if ((data == NULL) || (length == 0U) ||
        (length > tx_free_unchecked()) ||
        (s_transport.device == NULL)) {
        return 0U;
    }

    first = USB_MIN(length,
                    (uint16_t)(CDC_TX_QUEUE_SIZE -
                               (s_transport.tx_tail %
                                CDC_TX_QUEUE_SIZE)));
    memcpy(&s_transport.tx_queue[s_transport.tx_tail %
                                 CDC_TX_QUEUE_SIZE], data, first);
    if (first < length) {
        memcpy(s_transport.tx_queue, &data[first],
               (size_t)(length - first));
    }
    s_transport.tx_tail =
        (uint16_t)(s_transport.tx_tail + length);
    tx_start();
    return length;
}

uint16_t cdc_acm_tx_free(void)
{
    if (s_transport.device == NULL) {
        return 0U;
    }
    return tx_free_unchecked();
}

bool cdc_acm_tx_ready(void)
{
    return cdc_acm_tx_free() != 0U;
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
