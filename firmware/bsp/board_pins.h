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
#ifndef BOARD_PINS_H
#define BOARD_PINS_H

#include "gd32f30x.h"

/* Common-anode RGB LED, therefore all channels are active low. */
#define BOARD_LED_R_PORT       GPIOC
#define BOARD_LED_R_PIN        GPIO_PIN_13
#define BOARD_LED_G_PORT       GPIOC
#define BOARD_LED_G_PIN        GPIO_PIN_15
#define BOARD_LED_B_PORT       GPIOC
#define BOARD_LED_B_PIN        GPIO_PIN_14

#define BOARD_KEY_PORT         GPIOB
#define BOARD_KEY_PIN          GPIO_PIN_9

#define BOARD_USB_PULLUP_PORT  GPIOB
#define BOARD_USB_PULLUP_PIN   GPIO_PIN_8

#define BOARD_TGT_5V_EN_PORT   GPIOA
#define BOARD_TGT_5V_EN_PIN    GPIO_PIN_8
#define BOARD_TGT_3V3_EN_PORT  GPIOB
#define BOARD_TGT_3V3_EN_PIN   GPIO_PIN_15

#define BOARD_RF_NSS_PORT      GPIOA
#define BOARD_RF_NSS_PIN       GPIO_PIN_4
#define BOARD_RF_RX_EN_PORT    GPIOA
#define BOARD_RF_RX_EN_PIN     GPIO_PIN_0
#define BOARD_RF_NRESET_PORT   GPIOB
#define BOARD_RF_NRESET_PIN    GPIO_PIN_12
#define BOARD_RF_TX_EN_PORT    GPIOB
#define BOARD_RF_TX_EN_PIN     GPIO_PIN_13
#define BOARD_RF_BUSY_PORT     GPIOB
#define BOARD_RF_BUSY_PIN      GPIO_PIN_11
#define BOARD_RF_DIO1_PORT     GPIOB
#define BOARD_RF_DIO1_PIN      GPIO_PIN_10

#define BOARD_TGT_NRST_PORT    GPIOB
#define BOARD_TGT_NRST_PIN     GPIO_PIN_4
#define BOARD_TGT_BOOT_PORT    GPIOB
#define BOARD_TGT_BOOT_PIN     GPIO_PIN_5
#define BOARD_TGT_SWCLK_PORT   GPIOB
#define BOARD_TGT_SWCLK_PIN    GPIO_PIN_6
#define BOARD_TGT_SWDIO_PORT   GPIOB
#define BOARD_TGT_SWDIO_PIN    GPIO_PIN_7

#define BOARD_UART_PORT        GPIOA
#define BOARD_UART_TX_PIN      GPIO_PIN_9
#define BOARD_UART_RX_PIN      GPIO_PIN_10

#endif
