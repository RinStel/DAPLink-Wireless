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
#include "gd32f30x_dma.h"
#include "gd32f30x_misc.h"
#include "gd32f30x_gpio.h"
#include "gd32f30x_rcu.h"
#include "gd32f30x_usart.h"

/* 缓冲环保留一个空槽来区分满和空。数据收发由 USART 中断推进，主循环不再
 * 逐字节轮询外设状态。 */
#define TARGET_UART                 USART0
#define TARGET_UART_BUFFER_SIZE     512U
#define TARGET_UART_MIN_BAUD        1200U
#define TARGET_UART_MAX_BAUD        3000000U
#define TARGET_UART_DMA_RX_CHANNEL  DMA_CH5
#define TARGET_UART_DMA_TX_CHANNEL  DMA_CH4

static uint8_t s_rx_buffer[TARGET_UART_BUFFER_SIZE];
static uint8_t s_tx_buffer[TARGET_UART_BUFFER_SIZE];
static target_uart_ring_t s_rx_ring;
static target_uart_ring_t s_tx_ring;
static target_uart_config_t s_config;
static volatile bool s_tx_dma_active;
static volatile uint16_t s_tx_dma_length;

static uint16_t dma_rx_producer(void)
{
    uint32_t remaining = dma_transfer_number_get(
        DMA0, TARGET_UART_DMA_RX_CHANNEL);
    uint32_t producer = TARGET_UART_BUFFER_SIZE - remaining;

    return (uint16_t)(producer % TARGET_UART_BUFFER_SIZE);
}

static void rx_dma_publish(void)
{
    (void)target_uart_ring_dma_publish(&s_rx_ring, dma_rx_producer());
}

static void tx_dma_start(void)
{
    uint16_t length;

    if (s_tx_dma_active || (s_tx_ring.read == s_tx_ring.write)) {
        return;
    }
    if (s_tx_ring.write > s_tx_ring.read) {
        length = (uint16_t)(s_tx_ring.write - s_tx_ring.read);
    } else {
        length = (uint16_t)(s_tx_ring.capacity - s_tx_ring.read);
    }
    dma_channel_disable(DMA0, TARGET_UART_DMA_TX_CHANNEL);
    dma_memory_address_config(
        DMA0, TARGET_UART_DMA_TX_CHANNEL,
        (uint32_t)(uintptr_t)&s_tx_ring.storage[s_tx_ring.read]);
    dma_transfer_number_config(DMA0, TARGET_UART_DMA_TX_CHANNEL, length);
    dma_flag_clear(DMA0, TARGET_UART_DMA_TX_CHANNEL,
                   DMA_FLAG_G | DMA_FLAG_FTF | DMA_FLAG_ERR);
    s_tx_dma_length = length;
    s_tx_dma_active = true;
    dma_channel_enable(DMA0, TARGET_UART_DMA_TX_CHANNEL);
}

static void target_uart_dma_init(void)
{
    dma_parameter_struct parameter;

    rcu_periph_clock_enable(RCU_DMA0);
    dma_deinit(DMA0, TARGET_UART_DMA_RX_CHANNEL);
    dma_struct_para_init(&parameter);
    parameter.periph_addr = (uint32_t)&USART_DATA(TARGET_UART);
    parameter.periph_width = DMA_PERIPHERAL_WIDTH_8BIT;
    parameter.memory_addr = (uint32_t)s_rx_buffer;
    parameter.memory_width = DMA_MEMORY_WIDTH_8BIT;
    parameter.number = TARGET_UART_BUFFER_SIZE;
    parameter.priority = DMA_PRIORITY_HIGH;
    parameter.periph_inc = DMA_PERIPH_INCREASE_DISABLE;
    parameter.memory_inc = DMA_MEMORY_INCREASE_ENABLE;
    parameter.direction = DMA_PERIPHERAL_TO_MEMORY;
    dma_init(DMA0, TARGET_UART_DMA_RX_CHANNEL, &parameter);
    dma_circulation_enable(DMA0, TARGET_UART_DMA_RX_CHANNEL);
    dma_interrupt_enable(DMA0, TARGET_UART_DMA_RX_CHANNEL,
                         DMA_INT_HTF | DMA_INT_FTF | DMA_INT_ERR);
    /* USART0 开启接收 DMA 前先启动循环通道，避免首个字节仍由轮询路径处理。 */
    dma_channel_enable(DMA0, TARGET_UART_DMA_RX_CHANNEL);

    dma_deinit(DMA0, TARGET_UART_DMA_TX_CHANNEL);
    parameter.memory_addr = (uint32_t)s_tx_buffer;
    parameter.number = 1U;
    parameter.priority = DMA_PRIORITY_MEDIUM;
    parameter.direction = DMA_MEMORY_TO_PERIPHERAL;
    dma_init(DMA0, TARGET_UART_DMA_TX_CHANNEL, &parameter);
    dma_interrupt_enable(DMA0, TARGET_UART_DMA_TX_CHANNEL,
                         DMA_INT_FTF | DMA_INT_ERR);
    nvic_irq_enable(DMA0_Channel4_IRQn, 1U, 0U);
    nvic_irq_enable(DMA0_Channel5_IRQn, 1U, 0U);
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
    usart_interrupt_enable(TARGET_UART, USART_INT_IDLE);
    usart_interrupt_enable(TARGET_UART, USART_INT_ERR);
    usart_interrupt_disable(TARGET_UART, USART_INT_RBNE);
    usart_dma_receive_config(TARGET_UART, USART_RECEIVE_DMA_ENABLE);
    usart_dma_transmit_config(TARGET_UART, USART_TRANSMIT_DMA_ENABLE);
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
    target_uart_ring_init(&s_rx_ring, s_rx_buffer,
                          TARGET_UART_BUFFER_SIZE);
    target_uart_ring_init(&s_tx_ring, s_tx_buffer,
                          TARGET_UART_BUFFER_SIZE);
    s_tx_dma_active = false;
    s_tx_dma_length = 0U;
    target_uart_dma_init();
    nvic_irq_enable(USART0_IRQn, 2U, 0U);
    return target_uart_configure(config);
}

void target_uart_process(void)
{
    /* 收发由 USART0_IRQHandler 完成。保留此函数作为服务层调度接口，避免
     * 改变现有主循环调用顺序。 */
}

size_t target_uart_read(uint8_t *data, size_t capacity)
{
    if (data == NULL) {
        return 0U;
    }
    return target_uart_ring_read(&s_rx_ring, data, capacity);
}

size_t target_uart_write(const uint8_t *data, size_t length)
{
    size_t count = 0U;

    if ((data == NULL) || (length > target_uart_tx_free())) {
        return 0U;
    }
    count = target_uart_ring_write(&s_tx_ring, data, length);
    if (count != 0U) {
        tx_dma_start();
    }
    return count;
}

size_t target_uart_tx_free(void)
{
    return target_uart_ring_free(&s_tx_ring);
}

const target_uart_config_t *target_uart_config(void)
{
    return &s_config;
}

uint32_t target_uart_rx_overruns(void)
{
    return target_uart_ring_overruns(&s_rx_ring);
}

void USART0_IRQHandler(void)
{
    if (usart_interrupt_flag_get(TARGET_UART, USART_INT_FLAG_IDLE) != RESET) {
        /* GD32F30x 必须先读 STAT0，再读 DATA，才能清除 IDLEF。IDLEF 不支持
         * usart_interrupt_flag_clear()；错误清旗会使 CPU 重复进入 USART0 中断。 */
        (void)USART_STAT0(TARGET_UART);
        (void)USART_DATA(TARGET_UART);
        rx_dma_publish();
    }
    if ((usart_interrupt_flag_get(TARGET_UART,
                                  USART_INT_FLAG_ERR_ORERR) != RESET) ||
        (usart_interrupt_flag_get(TARGET_UART,
                                  USART_INT_FLAG_ERR_NERR) != RESET) ||
        (usart_interrupt_flag_get(TARGET_UART,
                                  USART_INT_FLAG_ERR_FERR) != RESET)) {
        /* 读取数据寄存器清除 ORE/噪声/帧错误；有效字节仍进入 RX 环。 */
        (void)usart_data_receive(TARGET_UART);
    }
}

void DMA0_Channel4_IRQHandler(void)
{
    if (dma_interrupt_flag_get(DMA0, TARGET_UART_DMA_TX_CHANNEL,
                               DMA_INT_FLAG_FTF) != RESET) {
        dma_interrupt_flag_clear(DMA0, TARGET_UART_DMA_TX_CHANNEL,
                                 DMA_INT_FLAG_FTF | DMA_INT_FLAG_G);
        (void)target_uart_ring_consume(&s_tx_ring, s_tx_dma_length);
        s_tx_dma_length = 0U;
        s_tx_dma_active = false;
        tx_dma_start();
    }
    if (dma_interrupt_flag_get(DMA0, TARGET_UART_DMA_TX_CHANNEL,
                               DMA_INT_FLAG_ERR) != RESET) {
        dma_interrupt_flag_clear(DMA0, TARGET_UART_DMA_TX_CHANNEL,
                                 DMA_INT_FLAG_ERR | DMA_INT_FLAG_G);
        s_tx_dma_active = false;
    }
}

void DMA0_Channel5_IRQHandler(void)
{
    if (dma_interrupt_flag_get(DMA0, TARGET_UART_DMA_RX_CHANNEL,
                               DMA_INT_FLAG_HTF) != RESET) {
        dma_interrupt_flag_clear(DMA0, TARGET_UART_DMA_RX_CHANNEL,
                                 DMA_INT_FLAG_HTF | DMA_INT_FLAG_G);
        rx_dma_publish();
    }
    if (dma_interrupt_flag_get(DMA0, TARGET_UART_DMA_RX_CHANNEL,
                               DMA_INT_FLAG_FTF) != RESET) {
        dma_interrupt_flag_clear(DMA0, TARGET_UART_DMA_RX_CHANNEL,
                                 DMA_INT_FLAG_FTF | DMA_INT_FLAG_G);
        rx_dma_publish();
    }
    if (dma_interrupt_flag_get(DMA0, TARGET_UART_DMA_RX_CHANNEL,
                               DMA_INT_FLAG_ERR) != RESET) {
        dma_interrupt_flag_clear(DMA0, TARGET_UART_DMA_RX_CHANNEL,
                                 DMA_INT_FLAG_ERR | DMA_INT_FLAG_G);
        rx_dma_publish();
    }
}
