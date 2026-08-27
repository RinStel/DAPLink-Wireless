/*
 * DAPLink-Wireless — Wireless CMSIS-DAP v2 debug probe firmware
 * Copyright (C) 2025 RinStel <me@rinx.nz>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */
#include "gd32f30x_gpio.h"

#include "board_pins.h"
#include "usbd_conf.h"

/* 编译期板级契约：目标、无线、LED 和 USB 引脚不得重叠。 */
_Static_assert(BOARD_LED_R_PORT == GPIOC, "LED R port changed");
_Static_assert(BOARD_LED_R_PIN == GPIO_PIN_14, "LED R pin must be PC14");
_Static_assert(BOARD_LED_G_PORT == GPIOC, "LED G port changed");
_Static_assert(BOARD_LED_G_PIN == GPIO_PIN_15, "LED G pin must be PC15");
_Static_assert(BOARD_LED_B_PORT == GPIOC, "LED B port changed");
_Static_assert(BOARD_LED_B_PIN == GPIO_PIN_13, "LED B pin must be PC13");
_Static_assert(BOARD_USB_AUTO_EN_PORT == GPIOA, "USB_AUTO_EN must be PA0");
_Static_assert(BOARD_USB_AUTO_EN_PIN == GPIO_PIN_0, "USB_AUTO_EN must be PA0");
_Static_assert(BOARD_USB_PULLUP_PORT == GPIOA, "USB pull-up must be PA8");
_Static_assert(BOARD_USB_PULLUP_PIN == GPIO_PIN_8, "USB pull-up must be PA8");
_Static_assert(BOARD_RF_RX_EN_PORT == GPIOA, "RF RX_EN must be PA1");
_Static_assert(BOARD_RF_RX_EN_PIN == GPIO_PIN_1, "RF RX_EN must be PA1");
_Static_assert(BOARD_RF_TX_EN_PORT == GPIOA, "RF TX_EN must be PA2");
_Static_assert(BOARD_RF_TX_EN_PIN == GPIO_PIN_2, "RF TX_EN must be PA2");
_Static_assert(BOARD_RF_NRESET_PORT == GPIOA, "RF NRESET must be PA3");
_Static_assert(BOARD_RF_NRESET_PIN == GPIO_PIN_3, "RF NRESET must be PA3");
_Static_assert(BOARD_RF_NSS_PORT == GPIOA, "RF NSS must be PA4");
_Static_assert(BOARD_RF_NSS_PIN == GPIO_PIN_4, "RF NSS must be PA4");
_Static_assert(BOARD_RF_BUSY_PORT == GPIOB, "RF BUSY must be PB1");
_Static_assert(BOARD_RF_BUSY_PIN == GPIO_PIN_1, "RF BUSY must be PB1");
_Static_assert(BOARD_RF_DIO1_PORT == GPIOB, "RF DIO1 must be PB5");
_Static_assert(BOARD_RF_DIO1_PIN == GPIO_PIN_5, "RF DIO1 must be PB5");
_Static_assert(BOARD_TGT_NRST_PORT == GPIOB, "target NRST port changed");
_Static_assert(BOARD_TGT_NRST_PIN == GPIO_PIN_15,
               "target NRST must be PB15");
_Static_assert(BOARD_TGT_BOOT_PORT == GPIOB, "target BOOT port changed");
_Static_assert(BOARD_TGT_BOOT_PIN == GPIO_PIN_14,
               "target BOOT must be PB14");
_Static_assert(BOARD_TGT_SWCLK_PORT == GPIOB, "target SWCLK port changed");
_Static_assert(BOARD_TGT_SWCLK_PIN == GPIO_PIN_13,
               "target SWCLK must be PB13");
_Static_assert(BOARD_TGT_SWDIO_PORT == GPIOB, "target SWDIO port changed");
_Static_assert(BOARD_TGT_SWDIO_PIN == GPIO_PIN_12,
               "target SWDIO must be PB12");
_Static_assert(USB_PULLUP == BOARD_USB_PULLUP_PORT,
               "USB port mapping diverged");
_Static_assert(USB_PULLUP_PIN == BOARD_USB_PULLUP_PIN,
               "USB pin mapping diverged");

#ifdef BOARD_TGT_5V_EN_PORT
#error "independent target 5V output must be removed"
#endif

#ifdef BOARD_TGT_3V3_EN_PORT
#error "independent target 3V3 output must be removed"
#endif

int main(void)
{
    return 0;
}
