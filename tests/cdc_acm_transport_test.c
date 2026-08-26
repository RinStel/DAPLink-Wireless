#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "cdc_acm_transport.h"
#include "usbd_conf.h"

#define CDC_TEST_PAYLOAD_SIZE 110U
#define CDC_TEST_QUEUE_SIZE   512U

static uint8_t s_wire[1024];
static uint16_t s_wire_length;
static uint16_t s_packet_length[32];
static uint8_t s_packet_count;
static bool s_in_flight;
static uint8_t *s_receive_buffer;
static uint8_t s_receive_arm_count;

static void endpoint_setup(usb_dev *udev, uint8_t buf_kind,
                           uint32_t buf_addr, const usb_desc_ep *ep_desc)
{
    usb_transc *transc;

    (void)buf_kind;
    (void)buf_addr;
    transc = (ep_desc->bEndpointAddress & 0x80U) != 0U
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
    assert(ep_addr == CDC_IN_EP);
    assert(length <= CDC_ACM_DATA_PACKET_SIZE);
    assert(s_packet_count < (uint8_t)(sizeof(s_packet_length) /
                                      sizeof(s_packet_length[0])));
    s_packet_length[s_packet_count++] = length;
    if (length != 0U) {
        assert(buffer != NULL);
        assert((uint32_t)s_wire_length + length <= sizeof(s_wire));
        memcpy(&s_wire[s_wire_length], buffer, length);
        s_wire_length = (uint16_t)(s_wire_length + length);
    }
    s_in_flight = true;
}

void usbd_ep_recev(usb_dev *udev, uint8_t ep_addr, uint8_t *buffer,
                   uint16_t length)
{
    (void)udev;
    (void)ep_addr;
    assert(ep_addr == CDC_OUT_EP);
    assert(buffer != NULL);
    assert(length == CDC_ACM_DATA_PACKET_SIZE);
    s_receive_buffer = buffer;
    ++s_receive_arm_count;
}

static void complete_in(usb_dev *udev)
{
    assert(s_in_flight);
    s_in_flight = false;
    cdc_class.data_in(udev, EP_ID(CDC_IN_EP));
}

static void receive_out(usb_dev *udev, const uint8_t *data,
                        uint16_t length)
{
    assert(s_receive_buffer != NULL);
    assert(length <= CDC_ACM_DATA_PACKET_SIZE);
    memcpy(s_receive_buffer, data, length);
    udev->transc_out[EP_ID(CDC_OUT_EP)].xfer_count = length;
    cdc_class.data_out(udev, EP_ID(CDC_OUT_EP));
}

#include "../firmware/usb/cdc_acm_transport.c"

int main(void)
{
    usb_dev device;
    uint8_t payload[CDC_TEST_PAYLOAD_SIZE];
    uint8_t full_queue[CDC_TEST_QUEUE_SIZE];
    uint16_t index;

    memset(&device, 0, sizeof(device));
    device.drv_handler = &s_usb_handler;
    for (index = 0U; index < sizeof(payload); ++index) {
        payload[index] = (uint8_t)index;
    }
    for (index = 0U; index < sizeof(full_queue); ++index) {
        full_queue[index] = (uint8_t)(index ^ 0x5AU);
    }

    assert(cdc_class.init(&device, 0U) == USBD_OK);
    assert(cdc_acm_tx_free() == CDC_TEST_QUEUE_SIZE);
    assert(cdc_acm_write(payload, sizeof(payload)) == sizeof(payload));
    assert(s_packet_count == 1U);
    assert(s_packet_length[0] == CDC_ACM_DATA_PACKET_SIZE);
    assert(cdc_acm_tx_free() == CDC_TEST_QUEUE_SIZE - sizeof(payload));
    complete_in(&device);
    assert(s_packet_count == 2U);
    assert(s_packet_length[1] == sizeof(payload) - CDC_ACM_DATA_PACKET_SIZE);
    complete_in(&device);
    assert(!s_in_flight);
    assert(s_wire_length == sizeof(payload));
    assert(memcmp(s_wire, payload, sizeof(payload)) == 0);
    assert(cdc_acm_tx_free() == CDC_TEST_QUEUE_SIZE);

    assert(cdc_class.deinit(&device, 0U) == USBD_OK);
    assert(cdc_class.init(&device, 0U) == USBD_OK);
    s_wire_length = 0U;
    s_packet_count = 0U;
    assert(cdc_acm_write(full_queue, sizeof(full_queue)) ==
           sizeof(full_queue));
    assert(cdc_acm_tx_free() == 0U);
    assert(cdc_acm_write(payload, 1U) == 0U);
    for (index = 0U; index < (CDC_TEST_QUEUE_SIZE /
                             CDC_ACM_DATA_PACKET_SIZE); ++index) {
        complete_in(&device);
    }
    assert(s_packet_count == (CDC_TEST_QUEUE_SIZE /
                              CDC_ACM_DATA_PACKET_SIZE));
    assert(!s_in_flight);
    assert(s_wire_length == sizeof(full_queue));
    assert(memcmp(s_wire, full_queue, sizeof(full_queue)) == 0);
    assert(cdc_acm_tx_free() == CDC_TEST_QUEUE_SIZE);

    {
        uint8_t first[CDC_ACM_DATA_PACKET_SIZE];
        uint8_t second[CDC_ACM_DATA_PACKET_SIZE];
        uint8_t received[CDC_ACM_DATA_PACKET_SIZE * 2U];

        memset(first, 0x11, sizeof(first));
        memset(second, 0x22, sizeof(second));
        s_receive_arm_count = 0U;
        receive_out(&device, first, sizeof(first));
        assert(s_receive_arm_count == 1U);
        receive_out(&device, second, sizeof(second));
        assert(s_receive_arm_count == 2U);
        assert(cdc_acm_read(received, sizeof(received)) ==
               sizeof(received));
        assert(memcmp(received, first, sizeof(first)) == 0);
        assert(memcmp(&received[sizeof(first)], second,
                      sizeof(second)) == 0);
    }

    assert(cdc_class.deinit(&device, 0U) == USBD_OK);
    return 0;
}
