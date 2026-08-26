#include <assert.h>
#include <stdint.h>

#include "target_uart.h"

int main(void)
{
    uint8_t storage[8];
    uint8_t output[8];
    target_uart_ring_t ring;
    uint8_t input[] = {1U, 2U, 3U, 4U, 5U, 6U};

    target_uart_ring_init(&ring, storage, sizeof(storage));
    assert(target_uart_ring_write(&ring, input, 6U) == 6U);
    assert(target_uart_ring_consume(&ring, 2U) == 2U);
    assert(target_uart_ring_read(&ring, output, 1U) == 1U);
    assert(output[0] == 3U);
    assert(target_uart_ring_write(&ring, input, 2U) == 2U);
    assert(target_uart_ring_read(&ring, output, 2U) == 2U);
    assert(output[0] == 4U && output[1] == 5U);
    assert(target_uart_ring_push(&ring, 7U));
    assert(target_uart_ring_push(&ring, 8U));
    assert(target_uart_ring_read(&ring, output, sizeof(output)) == 5U);
    assert(output[0] == 6U && output[4] == 8U);

    target_uart_ring_init(&ring, storage, sizeof(storage));
    storage[0] = 0xA1U;
    storage[1] = 0xA2U;
    assert(target_uart_ring_dma_publish(&ring, 2U) == 2U);
    assert(target_uart_ring_read(&ring, output, 2U) == 2U);
    assert(output[0] == 0xA1U && output[1] == 0xA2U);

    target_uart_ring_init(&ring, storage, sizeof(storage));
    assert(target_uart_ring_write(&ring, input, 7U) == 7U);
    assert(target_uart_ring_free(&ring) == 0U);
    assert(!target_uart_ring_push(&ring, 9U));
    assert(target_uart_ring_overruns(&ring) == 1U);
    assert(target_uart_ring_write(&ring, input, 1U) == 0U);
    assert(target_uart_ring_read(&ring, output, sizeof(output)) == 7U);
    assert(target_uart_ring_write(&ring, NULL, 0U) == 0U);
    return 0;
}
