/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "target_uart.h"

#include <string.h>

static uint16_t next_index(const target_uart_ring_t *ring,
                           uint16_t index)
{
    return (uint16_t)((index + 1U) % ring->capacity);
}

void target_uart_ring_init(target_uart_ring_t *ring, uint8_t *storage,
                           uint16_t capacity)
{
    if ((ring == NULL) || (storage == NULL) || (capacity < 2U)) {
        return;
    }
    ring->storage = storage;
    ring->capacity = capacity;
    ring->read = 0U;
    ring->write = 0U;
    ring->overruns = 0U;
}

bool target_uart_ring_push(target_uart_ring_t *ring, uint8_t value)
{
    uint16_t next;

    if ((ring == NULL) || (ring->storage == NULL) ||
        (ring->capacity < 2U)) {
        return false;
    }
    next = next_index(ring, ring->write);
    if (next == ring->read) {
        ++ring->overruns;
        return false;
    }
    ring->storage[ring->write] = value;
    ring->write = next;
    return true;
}

uint16_t target_uart_ring_dma_publish(target_uart_ring_t *ring,
                                      uint16_t producer)
{
    uint16_t published = 0U;

    if ((ring == NULL) || (ring->storage == NULL) ||
        (ring->capacity < 2U) || (producer >= ring->capacity)) {
        return 0U;
    }
    while (ring->write != producer) {
        uint16_t next = next_index(ring, ring->write);

        if (next == ring->read) {
            ++ring->overruns;
            break;
        }
        ring->write = next;
        ++published;
    }
    return published;
}

size_t target_uart_ring_read(target_uart_ring_t *ring, uint8_t *data,
                             size_t capacity)
{
    size_t count = 0U;

    if ((ring == NULL) || (data == NULL)) {
        return 0U;
    }
    while ((count < capacity) && (ring->read != ring->write)) {
        data[count++] = ring->storage[ring->read];
        ring->read = next_index(ring, ring->read);
    }
    return count;
}

size_t target_uart_ring_write(target_uart_ring_t *ring,
                              const uint8_t *data, size_t length)
{
    size_t count = 0U;

    if ((ring == NULL) || (data == NULL) ||
        (length > target_uart_ring_free(ring))) {
        return 0U;
    }
    while (count < length) {
        uint16_t next = next_index(ring, ring->write);

        ring->storage[ring->write] = data[count++];
        ring->write = next;
    }
    return count;
}

size_t target_uart_ring_consume(target_uart_ring_t *ring, size_t length)
{
    size_t available = 0U;

    if (ring == NULL) {
        return 0U;
    }
    if (ring->write >= ring->read) {
        available = ring->write - ring->read;
    } else {
        available = (size_t)ring->capacity - ring->read + ring->write;
    }
    if (length > available) {
        length = available;
    }
    ring->read = (uint16_t)((ring->read + length) % ring->capacity);
    return length;
}

size_t target_uart_ring_free(const target_uart_ring_t *ring)
{
    if ((ring == NULL) || (ring->capacity < 2U)) {
        return 0U;
    }
    if (ring->write >= ring->read) {
        return (size_t)ring->capacity - 1U -
               (size_t)(ring->write - ring->read);
    }
    return (size_t)(ring->read - ring->write - 1U);
}

uint32_t target_uart_ring_overruns(const target_uart_ring_t *ring)
{
    return ring == NULL ? 0U : ring->overruns;
}
