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

#include "board_pins.h"
#include "gd32f30x_fwdgt.h"
#include "gd32f30x_gpio.h"
#include "gd32f30x_rcu.h"

#define BOARD_DEVICE_ID1_ADDRESS 0x1FFFF7E8U
#define BOARD_DEVICE_ID2_ADDRESS 0x1FFFF7ECU
#define BOARD_DEVICE_ID3_ADDRESS 0x1FFFF7F0U
#define BOARD_WATCHDOG_RELOAD     1874U

static volatile uint32_t s_millis;
static uint8_t s_reset_cause;

static void reset_cause_capture(void)
{
    if (rcu_flag_get(RCU_FLAG_EPRST) == SET) {
        s_reset_cause |= BOARD_RESET_EXTERNAL;
    }
    if (rcu_flag_get(RCU_FLAG_PORRST) == SET) {
        s_reset_cause |= BOARD_RESET_POWER_ON;
    }
    if (rcu_flag_get(RCU_FLAG_SWRST) == SET) {
        s_reset_cause |= BOARD_RESET_SOFTWARE;
    }
    if (rcu_flag_get(RCU_FLAG_FWDGTRST) == SET) {
        s_reset_cause |= BOARD_RESET_WATCHDOG;
    }
    if (rcu_flag_get(RCU_FLAG_WWDGTRST) == SET) {
        s_reset_cause |= BOARD_RESET_WINDOW_WATCHDOG;
    }
    if (rcu_flag_get(RCU_FLAG_LPRST) == SET) {
        s_reset_cause |= BOARD_RESET_LOW_POWER;
    }
    rcu_all_reset_flag_clear();
}

static void output_write(uint32_t port, uint32_t pin, bool high)
{
    if (high) {
        gpio_bit_set(port, pin);
    } else {
        gpio_bit_reset(port, pin);
    }
}

void board_init(void)
{
    reset_cause_capture();
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_GPIOB);
    rcu_periph_clock_enable(RCU_GPIOC);

    /* Set safe output levels before changing the pins to push-pull mode. */
    output_write(BOARD_LED_R_PORT, BOARD_LED_R_PIN, true);
    output_write(BOARD_LED_G_PORT, BOARD_LED_G_PIN, true);
    output_write(BOARD_LED_B_PORT, BOARD_LED_B_PIN, true);
    output_write(BOARD_USB_PULLUP_PORT, BOARD_USB_PULLUP_PIN, false);
    output_write(BOARD_TGT_5V_EN_PORT, BOARD_TGT_5V_EN_PIN, false);
    output_write(BOARD_TGT_3V3_EN_PORT, BOARD_TGT_3V3_EN_PIN, false);
    output_write(BOARD_RF_NSS_PORT, BOARD_RF_NSS_PIN, true);
    output_write(BOARD_RF_RX_EN_PORT, BOARD_RF_RX_EN_PIN, false);
    output_write(BOARD_RF_NRESET_PORT, BOARD_RF_NRESET_PIN, false);
    output_write(BOARD_RF_TX_EN_PORT, BOARD_RF_TX_EN_PIN, false);

    gpio_init(GPIOC, GPIO_MODE_OUT_PP, GPIO_OSPEED_2MHZ,
              BOARD_LED_R_PIN | BOARD_LED_G_PIN | BOARD_LED_B_PIN);
    gpio_init(GPIOA, GPIO_MODE_OUT_PP, GPIO_OSPEED_2MHZ,
              BOARD_TGT_5V_EN_PIN | BOARD_RF_NSS_PIN | BOARD_RF_RX_EN_PIN);
    gpio_init(GPIOB, GPIO_MODE_OUT_PP, GPIO_OSPEED_2MHZ,
              BOARD_USB_PULLUP_PIN | BOARD_TGT_3V3_EN_PIN |
              BOARD_RF_NRESET_PIN | BOARD_RF_TX_EN_PIN);

    gpio_init(BOARD_KEY_PORT, GPIO_MODE_IPU, GPIO_OSPEED_2MHZ, BOARD_KEY_PIN);
    gpio_init(BOARD_RF_BUSY_PORT, GPIO_MODE_IPD, GPIO_OSPEED_2MHZ,
              BOARD_RF_BUSY_PIN);
    gpio_init(BOARD_RF_DIO1_PORT, GPIO_MODE_IPD, GPIO_OSPEED_2MHZ,
              BOARD_RF_DIO1_PIN);

    /* Do not drive an attached target until a debug session requests it. */
    gpio_init(BOARD_TGT_NRST_PORT, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_2MHZ,
              BOARD_TGT_NRST_PIN);
    gpio_init(BOARD_TGT_BOOT_PORT, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_2MHZ,
              BOARD_TGT_BOOT_PIN);

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    SysTick_Config(SystemCoreClock / 1000U);
}

void board_systick_isr(void)
{
    ++s_millis;
}

uint32_t board_millis(void)
{
    return s_millis;
}

uint32_t board_cycle_count(void)
{
    return DWT->CYCCNT;
}

uint32_t board_cycles_from_us(uint32_t delay_us)
{
    return (SystemCoreClock / 1000000U) * delay_us;
}

void board_delay_ms(uint32_t delay_ms)
{
    uint32_t start = board_millis();

    while ((uint32_t)(board_millis() - start) < delay_ms) {
    }
}

void board_delay_us(uint32_t delay_us)
{
    volatile uint32_t loops =
        (SystemCoreClock / 5000000U) * delay_us;

    while (loops-- != 0U) {
        __NOP();
    }
}

uint32_t board_device_id_hash(void)
{
    const uint32_t id1 = *(const uint32_t *)BOARD_DEVICE_ID1_ADDRESS;
    const uint32_t id2 = *(const uint32_t *)BOARD_DEVICE_ID2_ADDRESS;
    const uint32_t id3 = *(const uint32_t *)BOARD_DEVICE_ID3_ADDRESS;
    uint32_t hash = 2166136261U;

    hash = (hash ^ id1) * 16777619U;
    hash = (hash ^ id2) * 16777619U;
    hash = (hash ^ id3) * 16777619U;
    return hash;
}

uint8_t board_reset_cause(void)
{
    return s_reset_cause;
}

bool board_watchdog_start(void)
{
    return fwdgt_config(BOARD_WATCHDOG_RELOAD,
                        FWDGT_PSC_DIV256) == SUCCESS;
}

void board_watchdog_feed(void)
{
    fwdgt_counter_reload();
}

void board_led_set(board_led_t led, bool on)
{
    switch (led) {
    case BOARD_LED_RED:
        output_write(BOARD_LED_R_PORT, BOARD_LED_R_PIN, !on);
        break;
    case BOARD_LED_GREEN:
        output_write(BOARD_LED_G_PORT, BOARD_LED_G_PIN, !on);
        break;
    case BOARD_LED_BLUE:
        output_write(BOARD_LED_B_PORT, BOARD_LED_B_PIN, !on);
        break;
    default:
        break;
    }
}

bool board_key_pressed(void)
{
    return gpio_input_bit_get(BOARD_KEY_PORT, BOARD_KEY_PIN) == RESET;
}

void board_usb_connect(bool connect)
{
    output_write(BOARD_USB_PULLUP_PORT, BOARD_USB_PULLUP_PIN, connect);
}

void board_target_5v_enable(bool enable)
{
    output_write(BOARD_TGT_5V_EN_PORT, BOARD_TGT_5V_EN_PIN, enable);
}

void board_target_3v3_enable(bool enable)
{
    output_write(BOARD_TGT_3V3_EN_PORT, BOARD_TGT_3V3_EN_PIN, enable);
}
