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
#include "boot_mailbox.h"
#include "boot_state.h"
#include "boot_confirm_once.h"
#include "device_config.h"
#include "gd32f30x_misc.h"
#include "serial_bridge.h"
#include "status_indicator.h"
#include "usb_config_disk.h"

/* 主循环顺序固定：先服务传输，再应用用户输入，最后更新 LED 并喂狗。 */
#define BUTTON_DEBOUNCE_MS 30U
#define BUTTON_LONG_PRESS_MS 2000U

static void configuration_button_process(void)
{
    static bool raw_pressed;
    static bool stable_pressed;
    static uint32_t raw_changed_at;
    static uint32_t pressed_at;
    bool pressed = board_keya_pressed();
    uint32_t now = board_millis();

    /* 在释放时完成消抖，从稳定边沿计算按键持续时间。 */
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

static bool alive_indicator_on(void)
{
    static uint32_t last_toggle;
    static bool led_on;
    uint32_t now = board_millis();

    if ((uint32_t)(now - last_toggle) >= 500U) {
        last_toggle = now;
        led_on = !led_on;
    }

    return led_on;
}

static void status_indicator_apply(status_indicator_leds_t leds)
{
    board_led_set(BOARD_LED_RED, leds.red);
    board_led_set(BOARD_LED_GREEN, leds.green);
    board_led_set(BOARD_LED_BLUE, leds.blue);
}

int main(void)
{
    bool activity;
    bool bridge_ready;
    bool heartbeat_on;
    bool initialization_failed;
    bool runtime_error;
    bool watchdog_ready;
    boot_confirm_once_t boot_confirm_guard = {false, false};
    status_indicator_t indicator;
    status_indicator_leds_t leds;

    /* 指示灯分别报告桥接和看门狗初始化失败；看门狗仍监督主循环。 */
    board_init();
    board_led_set(BOARD_LED_BLUE, false);
    /* USB 配置盘快照依赖当前设备配置；先加载配置，但不在 USB 连接前
     * 初始化可能阻塞的无线桥接。 */
    device_config_init();
    /* 先拉起 USB D+，避免无线收发器的同步初始化阻塞主机枚举。各 USB 类
     * 在主机 SET_CONFIGURATION 时才初始化，因此此处不依赖 serial bridge。 */
    (void)usb_config_disk_init();
    bridge_ready = serial_bridge_init();
    /* Start watchdog AFTER all initialization to avoid timeout during init */
    watchdog_ready = board_watchdog_start();
    initialization_failed = !bridge_ready || !watchdog_ready;
    status_indicator_init(&indicator, initialization_failed);
    leds = status_indicator_update(&indicator, false, false, false,
                                   board_millis());
    status_indicator_apply(leds);

    for (;;) {
        usb_config_disk_process();
        if (usb_config_disk_dfu_reset_pending()) {
            boot_mailbox_request_dfu();
            board_usb_connect(false);
            board_delay_ms(100U);
            NVIC_SystemReset();
        }
        serial_bridge_process();
        configuration_button_process();
        runtime_error = serial_bridge_has_error();
        if (!initialization_failed && !runtime_error) {
            if (!boot_confirm_once(
                    &boot_confirm_guard,
                    (firmware_slot_t)FIRMWARE_LINK_SLOT,
                    boot_state_confirm)) {
                runtime_error = true;
            }
        }
        activity = serial_bridge_activity_led();
        heartbeat_on = false;
        if (!initialization_failed && !runtime_error) {
            heartbeat_on = alive_indicator_on();
        }
        leds = status_indicator_update(&indicator, runtime_error, activity,
                                       heartbeat_on, board_millis());
        status_indicator_apply(leds);
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
