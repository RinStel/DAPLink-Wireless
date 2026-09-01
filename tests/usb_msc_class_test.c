#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "usbd_msc_bbb.h"
#include "usbd_msc_core.h"
#include "usbd_msc_mem.h"

static uint8_t s_setup_count;
static uint8_t s_disable_count;
static uint8_t s_memory_init_count;
static uint8_t s_bbb_init_count;
static uint8_t s_bbb_deinit_count;
static uint8_t s_bbb_reset_count;
static uint8_t s_bbb_clear_feature_count;
static uint8_t s_bbb_data_in_count;
static uint8_t s_bbb_data_out_count;
static uint8_t s_disabled_endpoints[2];
static uint32_t s_pma_addresses[2];
static uint8_t s_endpoint_addresses[2];
static uint16_t s_endpoint_packet_sizes[2];
static uint8_t s_clear_feature_endpoint;
static uint8_t s_data_in_endpoint;
static uint8_t s_data_out_endpoint;

static void endpoint_setup(usb_dev *udev, uint8_t buffer_kind,
                           uint32_t pma_address,
                           const usb_desc_ep *endpoint)
{
    (void)udev;
    assert(buffer_kind == EP_BUF_SNG);
    assert(s_setup_count < 2U);
    s_pma_addresses[s_setup_count] = pma_address;
    s_endpoint_addresses[s_setup_count] = endpoint->bEndpointAddress;
    s_endpoint_packet_sizes[s_setup_count] = endpoint->wMaxPacketSize;
    ++s_setup_count;
}

static void endpoint_disable(usb_dev *udev, uint8_t endpoint)
{
    (void)udev;
    assert(s_disable_count < 2U);
    s_disabled_endpoints[s_disable_count] = endpoint;
    ++s_disable_count;
}

static usb_handler s_usb_handler = {
    .ep_setup = endpoint_setup,
    .ep_disable = endpoint_disable
};

static int8_t memory_init(uint8_t lun)
{
    assert(lun == 0U);
    ++s_memory_init_count;
    return 0;
}

static int8_t memory_ready(uint8_t lun)
{
    (void)lun;
    return 0;
}

static int8_t memory_protected(uint8_t lun)
{
    (void)lun;
    return 0;
}

static int8_t memory_read(uint8_t lun, uint8_t *buffer, uint32_t block,
                          uint16_t count)
{
    (void)lun;
    (void)buffer;
    (void)block;
    (void)count;
    return 0;
}

static int8_t memory_write(uint8_t lun, uint8_t *buffer, uint32_t block,
                           uint16_t count)
{
    (void)lun;
    (void)buffer;
    (void)block;
    (void)count;
    return 0;
}

static int8_t memory_max_lun(void)
{
    return 0;
}

static usbd_mem_cb s_memory_ops = {
    .mem_init = memory_init,
    .mem_ready = memory_ready,
    .mem_protected = memory_protected,
    .mem_read = memory_read,
    .mem_write = memory_write,
    .mem_maxlun = memory_max_lun
};

usbd_mem_cb *usbd_mem_fops = &s_memory_ops;

void msc_bbb_init(usb_dev *udev)
{
    assert(udev->class_data[USBD_MSC_INTERFACE] != NULL);
    ++s_bbb_init_count;
}

void msc_bbb_reset(usb_dev *udev)
{
    (void)udev;
    ++s_bbb_reset_count;
}

void msc_bbb_deinit(usb_dev *udev)
{
    (void)udev;
    ++s_bbb_deinit_count;
}

void msc_bbb_data_in(usb_dev *udev, uint8_t endpoint)
{
    (void)udev;
    s_data_in_endpoint = endpoint;
    ++s_bbb_data_in_count;
}

void msc_bbb_data_out(usb_dev *udev, uint8_t endpoint)
{
    (void)udev;
    s_data_out_endpoint = endpoint;
    ++s_bbb_data_out_count;
}

void msc_bbb_csw_send(usb_dev *udev, uint8_t status)
{
    (void)udev;
    (void)status;
}

void msc_bbb_clrfeature(usb_dev *udev, uint8_t endpoint)
{
    (void)udev;
    s_clear_feature_endpoint = endpoint;
    ++s_bbb_clear_feature_count;
}

int main(void)
{
    usb_dev device;
    usb_req request = {
        .bmRequestType = USB_TRX_IN,
        .bRequest = BBB_GET_MAX_LUN,
        .wLength = 1U
    };

    memset(&device, 0, sizeof(device));
    device.drv_handler = &s_usb_handler;

    assert(msc_class.init(&device, 0U) == USBD_OK);
    assert(device.class_data[USBD_MSC_INTERFACE] != NULL);
    assert(s_setup_count == 2U);
    /* MSC IN/OUT 共用端点编号 1，而 usbd_ep_setup 整写 EPxCS 会清另一方向
     * STAT：IN 是唯一显式设 EPTX_NAK 的分支，必须最后 setup，OUT 的 RX 由
     * msc_bbb_init 的 usbd_ep_recev 拉 VALID。顺序即正确性，写死断言。 */
    assert(s_endpoint_addresses[0] == MSC_OUT_EP);
    assert(s_pma_addresses[0] == BULK_RX_ADDR);
    assert(s_endpoint_addresses[1] == MSC_IN_EP);
    assert(s_pma_addresses[1] == BULK_TX_ADDR);
    /* 64 B 是结构性下限：驱动不按 PMA 槽夹取单次事务长度，而 MSC 要同时与
     * EP0/CDC/DAP 共分 512 B PMA。槽小于 64 B 时厂商 BBB 的一次扇区读写会
     * 踩过其它端点缓冲，就是“插入后约 60s DAP 起不来”的根因。 */
    assert(s_endpoint_packet_sizes[0] == MSC_DATA_PACKET_SIZE);
    assert(s_endpoint_packet_sizes[1] == MSC_DATA_PACKET_SIZE);
    assert(MSC_DATA_PACKET_SIZE == 64U);
    assert(device.ep_transc[EP_ID(MSC_IN_EP)][TRANSC_IN] ==
           msc_class.data_in);
    assert(device.ep_transc[EP_ID(MSC_OUT_EP)][TRANSC_OUT] ==
           msc_class.data_out);
    assert(s_memory_init_count == 1U);
    assert(s_bbb_init_count == 1U);

    assert(msc_class.req_process(&device, &request) == USBD_OK);
    assert(device.transc_in[0].xfer_len == 1U);
    assert(device.transc_in[0].xfer_buf[0] == 0U);
    request.wLength = 0U;
    assert(msc_class.req_process(&device, &request) == USBD_FAIL);

    request.bmRequestType = USB_TRX_OUT;
    request.bRequest = BBB_RESET;
    request.wLength = 0U;
    assert(msc_class.req_process(&device, &request) == USBD_OK);
    assert(s_bbb_reset_count == 1U);

    request.bRequest = USB_CLEAR_FEATURE;
    request.wIndex = MSC_IN_EP;
    assert(msc_class.req_process(&device, &request) == USBD_OK);
    assert(s_bbb_clear_feature_count == 1U);
    assert(s_clear_feature_endpoint == MSC_IN_EP);

    msc_class.data_in(&device, EP_ID(MSC_IN_EP));
    msc_class.data_out(&device, EP_ID(MSC_OUT_EP));
    assert(s_bbb_data_in_count == 1U);
    assert(s_data_in_endpoint == EP_ID(MSC_IN_EP));
    assert(s_bbb_data_out_count == 1U);
    assert(s_data_out_endpoint == EP_ID(MSC_OUT_EP));

    assert(msc_class.deinit(&device, 0U) == USBD_OK);
    assert(s_disabled_endpoints[0] == MSC_IN_EP);
    assert(s_disabled_endpoints[1] == MSC_OUT_EP);
    assert(s_bbb_deinit_count == 1U);
    return 0;
}
