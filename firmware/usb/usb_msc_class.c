/*
 * DAPLink-Wireless — Wireless CMSIS-DAP v2 debug probe firmware
 * Copyright (C) 2025 RinStel <me@rinx.nz>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */
#include "usbd_msc_core.h"

#include <string.h>

#include "usbd_msc_bbb.h"
#include "usbd_msc_mem.h"
#include "usbd_transc.h"

#define NO_CMD 0xFFU

static usbd_msc_handler s_msc_handler;
static uint8_t s_msc_max_lun;

static uint8_t msc_init(usb_dev *udev, uint8_t config_index);
static uint8_t msc_deinit(usb_dev *udev, uint8_t config_index);
static uint8_t msc_request(usb_dev *udev, usb_req *req);
static void msc_data_in(usb_dev *udev, uint8_t ep_num);
static void msc_data_out(usb_dev *udev, uint8_t ep_num);

usb_class msc_class = {
    .req_cmd = NO_CMD,
    .init = msc_init,
    .deinit = msc_deinit,
    .req_process = msc_request,
    .ctlx_in = NULL,
    .ctlx_out = NULL,
    .data_in = msc_data_in,
    .data_out = msc_data_out
};

static const usb_desc_ep s_msc_in_desc = {
    .header = {sizeof(usb_desc_ep), USB_DESCTYPE_EP},
    .bEndpointAddress = MSC_IN_EP,
    .bmAttributes = USB_EP_ATTR_BULK,
    .wMaxPacketSize = MSC_DATA_PACKET_SIZE,
    .bInterval = 0U
};

static const usb_desc_ep s_msc_out_desc = {
    .header = {sizeof(usb_desc_ep), USB_DESCTYPE_EP},
    .bEndpointAddress = MSC_OUT_EP,
    .bmAttributes = USB_EP_ATTR_BULK,
    .wMaxPacketSize = MSC_DATA_PACKET_SIZE,
    .bInterval = 0U
};

static uint8_t msc_init(usb_dev *udev, uint8_t config_index)
{
    uint8_t lun;

    (void)config_index;
    memset(&s_msc_handler, 0, sizeof(s_msc_handler));
    udev->class_data[USBD_MSC_INTERFACE] = &s_msc_handler;
    /* MSC IN/OUT 复用端点编号 1。vendor usbd_ep_setup 整写 EPxCS 会清掉另一
     * 方向的 STAT，故先 OUT 后 IN，让 IN 的 EPTX_NAK 保留；OUT 的 RX 由后面
     * msc_bbb_init 的 usbd_ep_recev 拉成 EPRX_VALID。 */
    usbd_ep_init(udev, EP_BUF_SNG, BULK_RX_ADDR, &s_msc_out_desc);
    usbd_ep_init(udev, EP_BUF_SNG, BULK_TX_ADDR, &s_msc_in_desc);
    udev->ep_transc[EP_ID(MSC_IN_EP)][TRANSC_IN] = msc_class.data_in;
    udev->ep_transc[EP_ID(MSC_OUT_EP)][TRANSC_OUT] = msc_class.data_out;
    for (lun = 0U; lun < MEM_LUN_NUM; ++lun) {
        usbd_mem_fops->mem_init(lun);
    }
    msc_bbb_init(udev);
    return USBD_OK;
}

static uint8_t msc_deinit(usb_dev *udev, uint8_t config_index)
{
    (void)config_index;
    usbd_ep_deinit(udev, MSC_IN_EP);
    usbd_ep_deinit(udev, MSC_OUT_EP);
    msc_bbb_deinit(udev);
    return USBD_OK;
}

static uint8_t msc_request(usb_dev *udev, usb_req *req)
{
    switch (req->bRequest) {
    case BBB_GET_MAX_LUN:
        if ((req->wValue != 0U) || (req->wLength != 1U) ||
            ((req->bmRequestType & USB_TRX_IN) != USB_TRX_IN)) {
            return USBD_FAIL;
        }
        s_msc_max_lun = (uint8_t)usbd_mem_fops->mem_maxlun();
        usb_transc_config(&udev->transc_in[0], &s_msc_max_lun, 1U, 0U);
        return USBD_OK;

    case BBB_RESET:
        if ((req->wValue != 0U) || (req->wLength != 0U) ||
            ((req->bmRequestType & USB_TRX_IN) == USB_TRX_IN)) {
            return USBD_FAIL;
        }
        msc_bbb_reset(udev);
        return USBD_OK;

    case USB_CLEAR_FEATURE:
        msc_bbb_clrfeature(udev, (uint8_t)req->wIndex);
        return USBD_OK;

    default:
        return USBD_FAIL;
    }
}

static void msc_data_in(usb_dev *udev, uint8_t ep_num)
{
    msc_bbb_data_in(udev, ep_num);
}

static void msc_data_out(usb_dev *udev, uint8_t ep_num)
{
    msc_bbb_data_out(udev, ep_num);
}
