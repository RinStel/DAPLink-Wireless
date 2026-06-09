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
#include "board.h"
#include "device_config.h"
#include "serial_bridge.h"
#include "usb_config_disk.h"

#define BUTTON_DEBOUNCE_MS 30U
#define BUTTON_LONG_PRESS_MS 2000U

static void configuration_button_process(void)
{
    static bool raw_pressed;
    static bool stable_pressed;
    static uint32_t raw_changed_at;
    static uint32_t pressed_at;
    bool pressed = board_key_pressed();
    uint32_t now = board_millis();

    if (pressed != raw_pressed) {
        raw_pressed = pressed;
        raw_changed_at = now;
    }
    if ((raw_pressed == stable_pressed) ||
        ((uint32_t)(now - raw_changed_at) < BUTTON_DEBOUNCE_MS)) {
        return;
    }

    stable_pressed = raw_pressed;
    if (stable_pressed) {
        pressed_at = now;
    } else {
        device_config_t previous = *device_config_get();
        serial_bridge_status_t status;

        serial_bridge_status_get(&status);
        if (status.swd_request_active) {
            return;
        }

        if ((uint32_t)(now - pressed_at) >= BUTTON_LONG_PRESS_MS) {
            device_config_button_cycle_mode();
        } else {
            device_config_button_cycle_rate();
        }
        if (serial_bridge_apply_config()) {
            usb_config_disk_refresh(&previous);
        } else {
            (void)device_config_apply(previous.sync_code,
                                      previous.device_mode,
                                      previous.rate_mode,
                                      previous.fixed_profile);
            (void)serial_bridge_apply_config();
        }
    }
}

static void update_alive_indicator(void)
{
    static uint32_t last_toggle;
    static bool led_on;
    uint32_t now = board_millis();

    if ((uint32_t)(now - last_toggle) >= 500U) {
        last_toggle = now;
        led_on = !led_on;
        board_led_set(BOARD_LED_GREEN, led_on);
    }
}

int main(void)
{
    bool bridge_ready;
    bool watchdog_ready;

    board_init();
    board_led_set(BOARD_LED_BLUE, false);
    bridge_ready = serial_bridge_init();
    (void)usb_config_disk_init();
    watchdog_ready = board_watchdog_start();
    board_led_set(BOARD_LED_RED, !bridge_ready || !watchdog_ready);

    for (;;) {
        serial_bridge_process();
        usb_config_disk_process();
        configuration_button_process();
        board_led_set(BOARD_LED_RED,
                      !watchdog_ready || serial_bridge_has_error());
        board_led_set(BOARD_LED_BLUE, serial_bridge_activity_led());

        if (!serial_bridge_has_error()) {
            update_alive_indicator();
        }
        board_watchdog_feed();
    }
}

void SysTick_Handler(void)
{
    board_systick_isr();
}

void USBD_LP_CAN0_RX0_IRQHandler(void)
{
    usb_config_disk_irq();
}

void USBD_HP_CAN0_TX_IRQHandler(void)
{
    usb_config_disk_hp_irq();
}

void USBD_WKUP_IRQHandler(void)
{
    usb_config_disk_wakeup_irq();
}
