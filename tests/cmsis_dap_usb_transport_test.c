#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "cmsis_dap.h"
#include "cmsis_dap_usb.h"

/* 手动驱动端点回调，验证 FIFO 顺序、重新 arm 和 Abort。 */
static uint8_t *s_receive_buffer;
static uint16_t s_receive_length;
static uint32_t s_receive_calls;
static uint8_t s_sent_response[CMSIS_DAP_PACKET_SIZE];
static uint8_t s_sent_length;
static uint8_t s_submit_count;
static uint8_t s_last_submit_command;
static uint8_t s_abort_count;
static uint8_t s_response_value;
static bool s_core_busy;
static bool s_receive_armed;
static bool s_response_ready;

static void endpoint_setup(usb_dev *udev, uint8_t buf_kind,
                           uint32_t buf_addr, const usb_desc_ep *ep_desc)
{
    usb_transc *transc;

    (void)buf_kind;
    (void)buf_addr;
    transc = EP_DIR(ep_desc->bEndpointAddress) != 0U
                 ? &udev->transc_in[EP_ID(ep_desc->bEndpointAddress)]
                 : &udev->transc_out[EP_ID(ep_desc->bEndpointAddress)];
    transc->max_len = ep_desc->wMaxPacketSize;
}

static void endpoint_disable(usb_dev *udev, uint8_t ep_addr)
{
    (void)udev;
    (void)ep_addr;
}

static usb_handler s_usb_handler = {
    .ep_setup = endpoint_setup,
    .ep_disable = endpoint_disable
};

void usbd_ep_send(usb_dev *udev, uint8_t ep_addr, uint8_t *buffer,
                  uint16_t length)
{
    (void)udev;
    assert(ep_addr == DAP_V2_IN_EP);
    assert(length <= sizeof(s_sent_response));
    s_sent_length = (uint8_t)length;
    memcpy(s_sent_response, buffer, length);
}

void usbd_ep_recev(usb_dev *udev, uint8_t ep_addr, uint8_t *buffer,
                   uint16_t length)
{
    (void)udev;
    assert(ep_addr == DAP_V2_OUT_EP);
    assert(length == DAP_USB_PACKET_SIZE);
    s_receive_buffer = buffer;
    s_receive_length = length;
    s_receive_armed = true;
    ++s_receive_calls;
}

void cmsis_dap_init(void)
{
    s_core_busy = false;
    s_response_ready = false;
    s_response_value = 0x80U;
}

bool cmsis_dap_submit(const uint8_t *request, uint8_t length)
{
    assert(request != NULL);
    assert(length != 0U);
    ++s_submit_count;
    s_last_submit_command = request[0];
    s_core_busy = true;
    return true;
}

void cmsis_dap_abort(void)
{
    ++s_abort_count;
}

void cmsis_dap_process(void)
{
}

bool cmsis_dap_response_take(uint8_t *response, uint8_t *length)
{
    if (!s_response_ready) {
        return false;
    }
    response[0] = s_response_value;
    *length = 1U;
    s_response_ready = false;
    s_core_busy = false;
    return true;
}

bool cmsis_dap_busy(void)
{
    return s_core_busy;
}

#include "../firmware/usb/cmsis_dap_usb.c"

static void receive_packet(usb_dev *udev, const uint8_t *packet,
                            uint8_t length)
{
    uint8_t *buffer = s_receive_buffer;
    uint8_t ep_num = EP_ID(DAP_V2_OUT_EP);

    assert(s_receive_armed);
    assert(buffer != NULL);
    assert(s_receive_length == DAP_USB_PACKET_SIZE);
    memcpy(buffer, packet, length);
    s_receive_armed = false;
    udev->transc_out[ep_num].xfer_count = length;
    cmsis_dap_usb_class.data_out(udev, ep_num);
}

int main(void)
{
    usb_dev device;
    uint8_t request[] = {0x00U};
    uint8_t abort_request[] = {0x07U};
    uint8_t queue_request[] = {0x7EU, 1U, 0x00U, 0x01U};
    uint8_t submit_before_queue;

    memset(&device, 0, sizeof(device));
    device.drv_handler = &s_usb_handler;
    assert(cmsis_dap_usb_class.init(&device, 0U) == USBD_OK);
    assert(s_receive_buffer != NULL);
    assert(s_receive_length == DAP_USB_PACKET_SIZE);

    receive_packet(&device, request, sizeof(request));
    assert(s_submit_count == 0U);
    assert(cmsis_dap_usb_idle() == false);
    cmsis_dap_usb_process();
    assert(s_submit_count == 1U);

    receive_packet(&device, request, sizeof(request));
    assert(s_submit_count == 1U);

    s_response_ready = true;
    cmsis_dap_usb_process();
    assert(s_sent_length == 1U);
    assert(s_sent_response[0] == 0x80U);

    cmsis_dap_usb_class.data_in(&device, EP_ID(DAP_V2_IN_EP));
    cmsis_dap_usb_process();
    assert(s_submit_count == 2U);

    receive_packet(&device, abort_request, sizeof(abort_request));
    assert(s_abort_count == 1U);
    s_response_value = 0x81U;
    s_response_ready = true;
    cmsis_dap_usb_process();
    receive_packet(&device, request, sizeof(request));
    cmsis_dap_usb_process();
    assert(s_submit_count == 3U);
    assert(s_sent_response[0] == 0x81U);

    s_response_value = 0x82U;
    s_response_ready = true;
    cmsis_dap_usb_process();
    assert(s_sent_response[0] == 0x81U);
    cmsis_dap_usb_class.data_in(&device, EP_ID(DAP_V2_IN_EP));
    assert(s_sent_response[0] == 0x82U);
    cmsis_dap_usb_class.data_in(&device, EP_ID(DAP_V2_IN_EP));
    assert(cmsis_dap_usb_idle());

    assert(cmsis_dap_usb_class.deinit(&device, 0U) == USBD_OK);

    /* 独立验证 QueueCommands 延迟提交和 ExecuteCommands 转换。 */
    assert(cmsis_dap_usb_class.init(&device, 0U) == USBD_OK);
    submit_before_queue = s_submit_count;
    receive_packet(&device, queue_request, sizeof(queue_request));
    cmsis_dap_usb_process();
    assert(s_submit_count == submit_before_queue);
    receive_packet(&device, request, sizeof(request));
    cmsis_dap_usb_process();
    assert(s_submit_count == (uint8_t)(submit_before_queue + 1U));
    assert(s_last_submit_command == 0x7FU);
    assert(cmsis_dap_usb_class.deinit(&device, 0U) == USBD_OK);

    assert(cmsis_dap_usb_class.init(&device, 0U) == USBD_OK);
    receive_packet(&device, request, sizeof(request));
    receive_packet(&device, request, sizeof(request));
    receive_packet(&device, request, sizeof(request));
    receive_packet(&device, request, sizeof(request));
    /* Four advertised request slots remain available, and the extra
     * internal slot keeps the endpoint armed for an abort. */
    assert(s_receive_armed);
    receive_packet(&device, abort_request, sizeof(abort_request));
    assert(s_abort_count == 2U);
    assert(s_receive_armed);
    receive_packet(&device, request, sizeof(request));
    assert(!s_receive_armed);
    cmsis_dap_usb_process();
    assert(s_submit_count == 5U);
    assert(s_receive_armed);
    assert(cmsis_dap_usb_class.deinit(&device, 0U) == USBD_OK);
    return 0;
}
