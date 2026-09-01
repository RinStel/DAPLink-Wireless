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
#ifndef USBD_CONF_H
#define USBD_CONF_H

#include "board_pins.h"

#define EP_COUNT                 6U
#define USBD_ITF_MAX_NUM         4U
#define USBD_CFG_MAX_NUM         1U
#define USBD_MSC_INTERFACE       0U
#define CDC_COM_INTERFACE        1U
#define CDC_DATA_INTERFACE       2U
#define MEM_LUN_NUM              1U

/* 运行期 bulk 端点保持全速 USB 最大包长；仅低频控制端点 EP0 使用
 * USB 2.0 允许的 32 字节包，以便所有单缓冲端点装入 512 字节 PMA。EP0 与 MSC
 * 争的是同一块 512 字节：曾把 MSC 的 64 B 挪给 EP0，结果一个扇区事务直接溢出
 * PMA，详见 firmware/usb/usb_msc_scsi.c 与
 * docs/development_release_manual.md 的 PMA 预算一节。 */
#define USBD_EP0_MAX_SIZE         32U
#define MSC_DATA_PACKET_SIZE      64U
#define CDC_ACM_DATA_PACKET_SIZE  64U
#define CDC_ACM_CMD_PACKET_SIZE    8U
#define DAP_USB_PACKET_SIZE       64U

/* PMA 地址按实际包长连续推导，所有边界至少 8 字节对齐。 */
#define USB_PMA_SIZE             0x0200U
#define BTABLE_OFFSET            0x0000U
#define USB_BTABLE_SIZE          (EP_COUNT * 8U)
#define EP0_TX_ADDR              (BTABLE_OFFSET + USB_BTABLE_SIZE)
#define EP0_RX_ADDR              (EP0_TX_ADDR + USBD_EP0_MAX_SIZE)
#define BULK_TX_ADDR             (EP0_RX_ADDR + USBD_EP0_MAX_SIZE)
#define BULK_RX_ADDR             (BULK_TX_ADDR + MSC_DATA_PACKET_SIZE)
#define CDC_BULK_TX_ADDR         (BULK_RX_ADDR + MSC_DATA_PACKET_SIZE)
#define CDC_BULK_RX_ADDR         (CDC_BULK_TX_ADDR + CDC_ACM_DATA_PACKET_SIZE)
#define CDC_INT_TX_ADDR          (CDC_BULK_RX_ADDR + CDC_ACM_DATA_PACKET_SIZE)
#define DAP_V2_TX_ADDR           (CDC_INT_TX_ADDR + CDC_ACM_CMD_PACKET_SIZE)
#define DAP_V2_RX_ADDR           (DAP_V2_TX_ADDR + DAP_USB_PACKET_SIZE)
#define USB_PMA_END_ADDR         (DAP_V2_RX_ADDR + DAP_USB_PACKET_SIZE)

#if USB_PMA_END_ADDR > USB_PMA_SIZE
#error "USB PMA layout exceeds the 512-byte hardware limit"
#endif

/* 端点编号必须与复合描述符及接口所有权一致。 */
#define MSC_IN_EP                0x81U
#define MSC_OUT_EP               0x02U
#define MSC_MEDIA_PACKET_SIZE    512U

/* SCSI 数据平面的硬约束：驱动层 usbd_ep_data_write() 既不按 maxpacket 也不按
 * 端点 PMA 槽长夹取单次事务，且 tx_count 取请求字节数，所以“一次事务 ≤ 该端点
 * 槽长”只能由调用方保证。firmware/usb/usb_msc_scsi.c 把扇区暂存在 RAM，每次至多
 * 交出 MSC_DATA_PACKET_SIZE 字节，正是为满足这条不变量；槽小于 64 B 时一个扇区
 * 读写会踩过相邻端点缓冲并越出 PMA 末尾，现场表现为插入后近一分钟 DAP 不可访问。 */
_Static_assert(MSC_MEDIA_PACKET_SIZE % MSC_DATA_PACKET_SIZE == 0U,
               "扇区暂存长度必须是端点事务长度的整数倍");
_Static_assert(MSC_DATA_PACKET_SIZE >= 31U,
               "MSC OUT 槽必须容得下 31 字节 CBW");
_Static_assert(MSC_DATA_PACKET_SIZE >= 13U,
               "MSC IN 槽必须容得下 13 字节 CSW");

#define CDC_IN_EP                0x83U
#define CDC_OUT_EP               0x03U
#define CDC_CMD_EP               0x84U
#define INT_TX_ADDR              CDC_INT_TX_ADDR

#define USB_PULLUP               BOARD_USB_PULLUP_PORT
#define USB_PULLUP_PIN           BOARD_USB_PULLUP_PIN

#endif
