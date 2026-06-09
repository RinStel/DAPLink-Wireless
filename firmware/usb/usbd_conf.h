#ifndef USBD_CONF_H
#define USBD_CONF_H

#include "gd32f30x.h"

#define EP_COUNT                 6U
#define USBD_ITF_MAX_NUM         4U
#define USBD_CFG_MAX_NUM         1U
#define USBD_MSC_INTERFACE       0U
#define CDC_COM_INTERFACE        1U
#define CDC_DATA_INTERFACE       2U
#define MEM_LUN_NUM              1U

#define BTABLE_OFFSET            0x0000U
#define EP0_TX_ADDR              0x0040U
#define EP0_RX_ADDR              0x0080U
#define BULK_TX_ADDR             0x00C0U
#define BULK_RX_ADDR             0x0100U
#define CDC_BULK_TX_ADDR         0x0140U
#define CDC_BULK_RX_ADDR         0x0150U
#define CDC_INT_TX_ADDR          0x0160U
#define DAP_V2_TX_ADDR           0x0168U
#define DAP_V2_RX_ADDR           0x01A8U

#define MSC_IN_EP                0x81U
#define MSC_OUT_EP               0x02U
#define MSC_DATA_PACKET_SIZE     64U
#define MSC_MEDIA_PACKET_SIZE    512U

#define CDC_IN_EP                0x83U
#define CDC_OUT_EP               0x03U
#define CDC_CMD_EP               0x84U
#define CDC_ACM_DATA_PACKET_SIZE 16U
#define CDC_ACM_CMD_PACKET_SIZE  8U
#define INT_TX_ADDR              CDC_INT_TX_ADDR

#define USB_PULLUP               GPIOB
#define USB_PULLUP_PIN           GPIO_PIN_8

#endif
