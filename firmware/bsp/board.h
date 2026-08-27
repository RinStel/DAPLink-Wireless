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
#ifndef BOARD_H
#define BOARD_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    BOARD_LED_RED = 0,
    BOARD_LED_GREEN,
    BOARD_LED_BLUE
} board_led_t;

#define BOARD_RESET_EXTERNAL  (1U << 0)
#define BOARD_RESET_POWER_ON  (1U << 1)
#define BOARD_RESET_SOFTWARE  (1U << 2)
#define BOARD_RESET_WATCHDOG  (1U << 3)
#define BOARD_RESET_WINDOW_WATCHDOG (1U << 4)
#define BOARD_RESET_LOW_POWER (1U << 5)

/* board_millis() 由 1 kHz 的 SysTick 驱动，uint32_t 回绕是预期行为。 */
void board_init(void);
void board_systick_isr(void);
uint32_t board_millis(void);
/* 返回自由运行的 Cortex-M DWT 周期计数器。 */
uint32_t board_cycle_count(void);
uint32_t board_cycles_from_us(uint32_t delay_us);
void board_delay_ms(uint32_t delay_ms);
void board_delay_us(uint32_t delay_us);
uint32_t board_device_id_hash(void);
uint8_t board_reset_cause(void);
bool board_watchdog_start(void);
/* 必须从主循环喂狗；中断服务程序不得调用。 */
void board_watchdog_feed(void);

/* on/off 是逻辑状态；board.c 负责适配低电平有效的 LED 接线。 */
void board_led_set(board_led_t led, bool on);
/* KEYA drives the current configuration-button behavior. */
bool board_keya_pressed(void);
/* KEYB is initialized by the BSP but has no application behavior yet. */
bool board_keyb_pressed(void);

void board_usb_connect(bool connect);
/* 返回 USB 供电检测输入，不代表 D+ 上拉状态。 */
bool board_usb_power_present(void);

#endif
