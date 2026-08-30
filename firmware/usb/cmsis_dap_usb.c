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
#include "cmsis_dap_usb.h"
#include "dap_diagnostics.h"

#include <stddef.h>
#include <string.h>

#include "cmsis_dap.h"
#include "usbd_conf.h"
#include "usbd_enum.h"
#include "usbd_transc.h"

#define NO_CMD             0xFFU
#define DAP_TRANSFER_ABORT 0x07U
#define DAP_QUEUE_COMMANDS 0x7EU
#define DAP_EXECUTE_COMMANDS 0x7FU
/* 在对外公布的四包窗口之外保留一个内部请求槽，使普通请求排队时仍能接收
 * DAP_TransferAbort。 */
#define DAP_USB_RING_SIZE  (DAP_USB_PACKET_COUNT + 2U)

typedef struct {
    uint8_t data[DAP_USB_PACKET_SIZE];
    uint8_t length;
} dap_usb_packet_t;

typedef struct {
    dap_usb_packet_t requests[DAP_USB_RING_SIZE];
    dap_usb_packet_t responses[DAP_USB_RING_SIZE];
    volatile uint8_t request_read;
    volatile uint8_t request_write;
    volatile uint8_t response_read;
    volatile uint8_t response_write;
    volatile bool out_armed;
    volatile bool tx_busy;
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

static uint8_t ring_next(uint8_t index)
{
    /* 保留一个空槽，以便用读写索引区分满和空。 */
    ++index;
    return index == DAP_USB_RING_SIZE ? 0U : index;
}

static bool request_available(void)
{
    return s_transport.request_read != s_transport.request_write;
}

static bool request_full(void)
{
    return ring_next(s_transport.request_write) ==
           s_transport.request_read;
}

#if CMSIS_DAP_DIAGNOSTICS_ENABLE
static uint8_t request_depth(void)
{
    return s_transport.request_write >= s_transport.request_read
               ? (uint8_t)(s_transport.request_write - s_transport.request_read)
               : (uint8_t)(DAP_USB_RING_SIZE - s_transport.request_read +
                           s_transport.request_write);
}
#endif

static bool response_available(void)
{
    return s_transport.response_read != s_transport.response_write;
}

static bool response_full(void)
{
    return ring_next(s_transport.response_write) ==
           s_transport.response_read;
}

static void receive_arm(uint8_t *buffer)
{
    s_transport.out_armed = true;
    usbd_ep_recev(s_usb_device, DAP_V2_OUT_EP, buffer,
                  DAP_USB_PACKET_SIZE);
}

static void receive_arm_if_space(void)
{
    /* 只有前一个包已消费或提交到请求环后才重新 arm OUT，避免 DMA 覆盖排队
     * 数据。 */
    if ((s_usb_device != NULL) && !s_transport.out_armed &&
        !request_full()) {
        receive_arm(
            s_transport.requests[s_transport.request_write].data);
    }
}

static void send_response_if_ready(void)
{
    dap_usb_packet_t *packet;

    if ((s_usb_device == NULL) || s_transport.tx_busy ||
        !response_available()) {
        return;
    }
    /* USB IN 串行发送，但主机确认上一个包时仍可继续把响应写入 FIFO。 */
    packet = &s_transport.responses[s_transport.response_read];
    s_transport.tx_busy = true;
    usbd_ep_send(s_usb_device, DAP_V2_IN_EP, packet->data,
                 packet->length);
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
    receive_arm_if_space();
    return USBD_OK;
}

static uint8_t dap_usb_deinit(usb_dev *udev, uint8_t config_index)
{
    (void)config_index;
    usbd_ep_deinit(udev, DAP_V2_IN_EP);
    usbd_ep_deinit(udev, DAP_V2_OUT_EP);
    memset(&s_transport, 0, sizeof(s_transport));
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
    DAP_DIAG(usb_in_complete());
    if (response_available()) {
        s_transport.response_read = ring_next(s_transport.response_read);
    }
    s_transport.tx_busy = false;
    receive_arm_if_space();
    send_response_if_ready();
}

static void dap_usb_data_out(usb_dev *udev, uint8_t ep_num)
{
    uint8_t length;

    if (ep_num != EP_ID(DAP_V2_OUT_EP)) {
        return;
    }
    length = (uint8_t)udev->transc_out[ep_num].xfer_count;
    s_transport.out_armed = false;
    if (length == 0U) {
        receive_arm_if_space();
        return;
    }
    DAP_DIAG(usb_out(s_transport.requests[s_transport.request_write].data,
                     length));
    if (s_transport.requests[s_transport.request_write].data[0] ==
        DAP_TRANSFER_ABORT) {
        cmsis_dap_abort();
        receive_arm_if_space();
        return;
    }
    if (request_full()) {
        receive_arm_if_space();
        return;
    }
    s_transport.requests[s_transport.request_write].length = length;
    s_transport.request_write = ring_next(s_transport.request_write);
    DAP_DIAG(request_ring_depth(request_depth()));
    receive_arm_if_space();
}

void cmsis_dap_usb_process(void)
{
    dap_usb_packet_t *request;
    dap_usb_packet_t *response;
    uint8_t length;

    if (s_usb_device == NULL) {
        return;
    }
    if (!cmsis_dap_busy() && request_available()) {
        request = &s_transport.requests[s_transport.request_read];
        /* Arm 模板把 QueueCommands 包转换为 ExecuteCommands；仅有一个
         * Queue 包时暂缓，等待后续 USB 包到达以保持队列语义。 */
        if (request->data[0] == DAP_QUEUE_COMMANDS) {
            uint8_t next = ring_next(s_transport.request_read);
            if (next == s_transport.request_write) {
                return;
            }
            request->data[0] = DAP_EXECUTE_COMMANDS;
        }
#if CMSIS_DAP_SWD_BURST_ENABLE
        if (cmsis_dap_burst_eligible(request->data, request->length)) {
            const uint8_t *requests[CMSIS_DAP_BURST_MAX_COMMANDS];
            uint8_t lengths[CMSIS_DAP_BURST_MAX_COMMANDS];
            uint8_t indices[CMSIS_DAP_BURST_MAX_COMMANDS];
            uint8_t count = 0U;
            uint8_t index = s_transport.request_read;
            uint8_t attempt;
            bool burst_submitted = false;

            while ((index != s_transport.request_write) &&
                   (count < CMSIS_DAP_BURST_MAX_COMMANDS)) {
                dap_usb_packet_t *candidate =
                    &s_transport.requests[index];

                if (!cmsis_dap_burst_eligible(candidate->data,
                                              candidate->length)) {
                    break;
                }
                requests[count] = candidate->data;
                lengths[count] = candidate->length;
                indices[count] = index;
                ++count;
                index = ring_next(index);
            }
            for (attempt = count; attempt >= 2U; --attempt) {
                if (cmsis_dap_submit_burst(requests, lengths, attempt)) {
                    s_transport.request_read =
                        ring_next(indices[attempt - 1U]);
                    receive_arm_if_space();
                    request = NULL;
                    burst_submitted = true;
                    break;
                }
            }
            if (request == NULL) {
                    goto request_submitted;
            }
            if ((count >= 2U) && !burst_submitted) {
                DAP_DIAG(burst_fallback());
            }
        }
#endif
        if (cmsis_dap_submit(request->data, request->length)) {
            s_transport.request_read =
                ring_next(s_transport.request_read);
            receive_arm_if_space();
        }
    }
#if CMSIS_DAP_SWD_BURST_ENABLE
request_submitted:
#endif
    cmsis_dap_process();
    if (!response_full()) {
        response = &s_transport.responses[s_transport.response_write];
        if (cmsis_dap_response_take(response->data, &length)) {
            response->length = length;
            s_transport.response_write =
                ring_next(s_transport.response_write);
        }
    }
    send_response_if_ready();
}

bool cmsis_dap_usb_idle(void)
{
    return !request_available() && !response_available() &&
           !s_transport.tx_busy &&
           !cmsis_dap_busy();
}
