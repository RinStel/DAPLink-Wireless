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

void board_init(void);
void board_systick_isr(void);
uint32_t board_millis(void);
uint32_t board_cycle_count(void);
uint32_t board_cycles_from_us(uint32_t delay_us);
void board_delay_ms(uint32_t delay_ms);
void board_delay_us(uint32_t delay_us);
uint32_t board_device_id_hash(void);
uint8_t board_reset_cause(void);
bool board_watchdog_start(void);
void board_watchdog_feed(void);

void board_led_set(board_led_t led, bool on);
bool board_key_pressed(void);

void board_usb_connect(bool connect);
void board_target_5v_enable(bool enable);
void board_target_3v3_enable(bool enable);

#endif
