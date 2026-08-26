#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gd32f30x_usart.h"

static uint32_t test_usart_status_read(uint32_t usart_periph);
static volatile uint32_t *test_usart_data_address(uint32_t usart_periph);

#undef USART_STAT0
#undef USART_DATA
#define USART_STAT0(usartx) test_usart_status_read(usartx)
#define USART_DATA(usartx)  (*test_usart_data_address(usartx))

#include "../firmware/drivers/serial/target_uart.c"

static bool s_idle_pending;
static bool s_status_read;
static volatile uint32_t s_data_register;

static uint32_t test_usart_status_read(uint32_t usart_periph)
{
    (void)usart_periph;
    s_status_read = true;
    return s_idle_pending ? USART_STAT0_IDLEF : 0U;
}

static volatile uint32_t *test_usart_data_address(uint32_t usart_periph)
{
    (void)usart_periph;
    if (s_status_read) {
        s_idle_pending = false;
    }
    return &s_data_register;
}

FlagStatus usart_interrupt_flag_get(
    uint32_t usart_periph, usart_interrupt_flag_enum interrupt_flag)
{
    if (interrupt_flag == USART_INT_FLAG_IDLE) {
        return (test_usart_status_read(usart_periph) &
                USART_STAT0_IDLEF) != 0U ? SET : RESET;
    }
    return RESET;
}

void usart_interrupt_flag_clear(
    uint32_t usart_periph, usart_interrupt_flag_enum interrupt_flag)
{
    (void)usart_periph;
    (void)interrupt_flag;
}

uint16_t usart_data_receive(uint32_t usart_periph)
{
    return (uint16_t)USART_DATA(usart_periph);
}

uint32_t dma_transfer_number_get(uint32_t dma_periph,
                                 dma_channel_enum channelx)
{
    (void)dma_periph;
    (void)channelx;
    return TARGET_UART_BUFFER_SIZE;
}

#define DEFINE_DMA_CHANNEL_STUB(name)                                      \
    void name(uint32_t dma_periph, dma_channel_enum channelx)              \
    {                                                                      \
        (void)dma_periph;                                                  \
        (void)channelx;                                                    \
    }

#define DEFINE_DMA_CHANNEL_VALUE_STUB(name)                                \
    void name(uint32_t dma_periph, dma_channel_enum channelx,              \
              uint32_t value)                                              \
    {                                                                      \
        (void)dma_periph;                                                  \
        (void)channelx;                                                    \
        (void)value;                                                       \
    }

#define DEFINE_USART_VALUE_STUB(name)                                      \
    void name(uint32_t usart_periph, uint32_t value)                       \
    {                                                                      \
        (void)usart_periph;                                                \
        (void)value;                                                       \
    }

DEFINE_DMA_CHANNEL_STUB(dma_deinit)
DEFINE_DMA_CHANNEL_STUB(dma_circulation_enable)
DEFINE_DMA_CHANNEL_STUB(dma_channel_enable)
DEFINE_DMA_CHANNEL_STUB(dma_channel_disable)
DEFINE_DMA_CHANNEL_VALUE_STUB(dma_memory_address_config)
DEFINE_DMA_CHANNEL_VALUE_STUB(dma_transfer_number_config)
DEFINE_DMA_CHANNEL_VALUE_STUB(dma_flag_clear)
DEFINE_DMA_CHANNEL_VALUE_STUB(dma_interrupt_flag_clear)
DEFINE_DMA_CHANNEL_VALUE_STUB(dma_interrupt_enable)

void dma_struct_para_init(dma_parameter_struct *init_struct)
{
    (void)init_struct;
}

void dma_init(uint32_t dma_periph, dma_channel_enum channelx,
              dma_parameter_struct *init_struct)
{
    (void)dma_periph;
    (void)channelx;
    (void)init_struct;
}

FlagStatus dma_interrupt_flag_get(uint32_t dma_periph,
                                  dma_channel_enum channelx,
                                  uint32_t flag)
{
    (void)dma_periph;
    (void)channelx;
    (void)flag;
    return RESET;
}

void rcu_periph_clock_enable(rcu_periph_enum periph)
{
    (void)periph;
}

void nvic_irq_enable(IRQn_Type nvic_irq, uint8_t pre_priority,
                     uint8_t sub_priority)
{
    (void)nvic_irq;
    (void)pre_priority;
    (void)sub_priority;
}

void gpio_init(uint32_t gpio_periph, uint32_t mode, uint32_t speed,
               uint32_t pin)
{
    (void)gpio_periph;
    (void)mode;
    (void)speed;
    (void)pin;
}

void usart_deinit(uint32_t usart_periph)
{
    (void)usart_periph;
}

void usart_enable(uint32_t usart_periph)
{
    (void)usart_periph;
}

void usart_disable(uint32_t usart_periph)
{
    (void)usart_periph;
}

DEFINE_USART_VALUE_STUB(usart_baudrate_set)
DEFINE_USART_VALUE_STUB(usart_parity_config)
DEFINE_USART_VALUE_STUB(usart_word_length_set)
DEFINE_USART_VALUE_STUB(usart_stop_bit_set)
DEFINE_USART_VALUE_STUB(usart_transmit_config)
DEFINE_USART_VALUE_STUB(usart_receive_config)

void usart_dma_receive_config(uint32_t usart_periph, uint8_t command)
{
    (void)usart_periph;
    (void)command;
}

void usart_dma_transmit_config(uint32_t usart_periph, uint8_t command)
{
    (void)usart_periph;
    (void)command;
}

void usart_interrupt_enable(uint32_t usart_periph,
                            usart_interrupt_enum interrupt)
{
    (void)usart_periph;
    (void)interrupt;
}

void usart_interrupt_disable(uint32_t usart_periph,
                             usart_interrupt_enum interrupt)
{
    (void)usart_periph;
    (void)interrupt;
}

int main(void)
{
    target_uart_ring_init(&s_rx_ring, s_rx_buffer,
                          TARGET_UART_BUFFER_SIZE);
    s_idle_pending = true;
    s_status_read = false;
    s_data_register = 0U;

    USART0_IRQHandler();

    assert(!s_idle_pending);
    return 0;
}
