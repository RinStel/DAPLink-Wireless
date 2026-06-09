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
#include "target_uart.h"

#include "board_pins.h"
#include "gd32f30x_gpio.h"
#include "gd32f30x_rcu.h"
#include "gd32f30x_usart.h"

#define TARGET_UART                 USART0
#define TARGET_UART_BUFFER_SIZE     512U
#define TARGET_UART_MIN_BAUD        1200U
#define TARGET_UART_MAX_BAUD        3000000U

static uint8_t s_rx_buffer[TARGET_UART_BUFFER_SIZE];
static uint8_t s_tx_buffer[TARGET_UART_BUFFER_SIZE];
static uint16_t s_rx_read;
static uint16_t s_rx_write;
static uint16_t s_tx_read;
static uint16_t s_tx_write;
static uint32_t s_rx_overruns;
static target_uart_config_t s_config;

static uint16_t next_index(uint16_t index)
{
    return (uint16_t)((index + 1U) % TARGET_UART_BUFFER_SIZE);
}

static bool config_valid(const target_uart_config_t *config)
{
    return (config != NULL) &&
           (config->baud_rate >= TARGET_UART_MIN_BAUD) &&
           (config->baud_rate <= TARGET_UART_MAX_BAUD) &&
           ((config->data_bits == 7U) || (config->data_bits == 8U)) &&
           !((config->data_bits == 7U) &&
             (config->parity == TARGET_UART_PARITY_NONE)) &&
           ((config->stop_bits == 1U) || (config->stop_bits == 2U)) &&
           (config->parity <= TARGET_UART_PARITY_EVEN);
}

bool target_uart_configure(const target_uart_config_t *config)
{
    uint32_t word_length;
    uint32_t parity;

    if (!config_valid(config)) {
        return false;
    }

    /*
     * With parity enabled the parity bit occupies the MSB, therefore an
     * 8-data-bit frame uses the peripheral's 9-bit word length.
     */
    word_length = (config->data_bits == 8U &&
                   config->parity != TARGET_UART_PARITY_NONE)
                      ? USART_WL_9BIT
                      : USART_WL_8BIT;
    if (config->parity == TARGET_UART_PARITY_ODD) {
        parity = USART_PM_ODD;
    } else if (config->parity == TARGET_UART_PARITY_EVEN) {
        parity = USART_PM_EVEN;
    } else {
        parity = USART_PM_NONE;
    }

    usart_disable(TARGET_UART);
    usart_baudrate_set(TARGET_UART, config->baud_rate);
    usart_word_length_set(TARGET_UART, word_length);
    usart_stop_bit_set(TARGET_UART, config->stop_bits == 2U
                                        ? USART_STB_2BIT
                                        : USART_STB_1BIT);
    usart_parity_config(TARGET_UART, parity);
    usart_receive_config(TARGET_UART, USART_RECEIVE_ENABLE);
    usart_transmit_config(TARGET_UART, USART_TRANSMIT_ENABLE);
    usart_enable(TARGET_UART);
    s_config = *config;
    return true;
}

bool target_uart_init(const target_uart_config_t *config)
{
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_USART0);
    gpio_init(BOARD_UART_PORT, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ,
              BOARD_UART_TX_PIN);
    gpio_init(BOARD_UART_PORT, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_50MHZ,
              BOARD_UART_RX_PIN);
    usart_deinit(TARGET_UART);
    s_rx_read = 0U;
    s_rx_write = 0U;
    s_tx_read = 0U;
    s_tx_write = 0U;
    s_rx_overruns = 0U;
    return target_uart_configure(config);
}

void target_uart_process(void)
{
    while (usart_flag_get(TARGET_UART, USART_FLAG_RBNE) != RESET) {
        uint16_t next = next_index(s_rx_write);
        uint8_t data = (uint8_t)usart_data_receive(TARGET_UART);

        if (next == s_rx_read) {
            ++s_rx_overruns;
        } else {
            s_rx_buffer[s_rx_write] = data;
            s_rx_write = next;
        }
    }

    while ((s_tx_read != s_tx_write) &&
           (usart_flag_get(TARGET_UART, USART_FLAG_TBE) != RESET)) {
        usart_data_transmit(TARGET_UART, s_tx_buffer[s_tx_read]);
        s_tx_read = next_index(s_tx_read);
    }
}

size_t target_uart_read(uint8_t *data, size_t capacity)
{
    size_t count = 0U;

    if (data == NULL) {
        return 0U;
    }
    while ((count < capacity) && (s_rx_read != s_rx_write)) {
        data[count++] = s_rx_buffer[s_rx_read];
        s_rx_read = next_index(s_rx_read);
    }
    return count;
}

size_t target_uart_write(const uint8_t *data, size_t length)
{
    size_t count = 0U;

    if ((data == NULL) || (length > target_uart_tx_free())) {
        return 0U;
    }
    while (count < length) {
        uint16_t next = next_index(s_tx_write);

        if (next == s_tx_read) {
            break;
        }
        s_tx_buffer[s_tx_write] = data[count++];
        s_tx_write = next;
    }
    target_uart_process();
    return count;
}

size_t target_uart_tx_free(void)
{
    if (s_tx_write >= s_tx_read) {
        return TARGET_UART_BUFFER_SIZE - 1U -
               (size_t)(s_tx_write - s_tx_read);
    }
    return (size_t)(s_tx_read - s_tx_write - 1U);
}

const target_uart_config_t *target_uart_config(void)
{
    return &s_config;
}

uint32_t target_uart_rx_overruns(void)
{
    return s_rx_overruns;
}
