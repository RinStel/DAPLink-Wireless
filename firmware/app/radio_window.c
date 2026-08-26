#include "radio_window.h"

#include <string.h>

static bool sequence_before(uint32_t left, uint32_t right)
{
    return (int32_t)(left - right) < 0;
}

static radio_window_slot_t *tx_slot_find(radio_window_t *window,
                                         uint32_t sequence)
{
    uint8_t index;

    for (index = 0U; index < RADIO_WINDOW_SIZE; ++index) {
        if (window->tx[index].active &&
            (window->tx[index].sequence == sequence)) {
            return &window->tx[index];
        }
    }
    return NULL;
}

static radio_window_slot_t *free_slot(radio_window_slot_t *slots)
{
    uint8_t index;

    for (index = 0U; index < RADIO_WINDOW_SIZE; ++index) {
        if (!slots[index].active) {
            return &slots[index];
        }
    }
    return NULL;
}

void radio_window_init(radio_window_t *window, uint32_t tx_sequence,
                       uint32_t rx_sequence)
{
    if (window == NULL) {
        return;
    }
    memset(window, 0, sizeof(*window));
    window->tx_next = tx_sequence;
    window->rx_next = rx_sequence;
}

bool radio_window_tx_push(radio_window_t *window, const uint8_t *payload,
                          uint8_t length, uint32_t *sequence)
{
    radio_window_slot_t *slot;

    if ((window == NULL) || (payload == NULL) || (length == 0U) ||
        (length > RADIO_WINDOW_MAX_PAYLOAD) || (sequence == NULL)) {
        return false;
    }
    slot = free_slot(window->tx);
    if (slot == NULL) {
        return false;
    }
    slot->active = true;
    slot->sent = false;
    slot->sequence = window->tx_next++;
    slot->last_sent_ms = 0U;
    slot->length = length;
    memcpy(slot->payload, payload, length);
    *sequence = slot->sequence;
    return true;
}

void radio_window_tx_ack(radio_window_t *window, uint32_t ack_next,
                         uint32_t bitmap)
{
    uint8_t index;

    if (window == NULL) {
        return;
    }
    for (index = 0U; index < RADIO_WINDOW_SIZE; ++index) {
        radio_window_slot_t *slot = &window->tx[index];
        uint32_t offset;

        if (!slot->active) {
            continue;
        }
        if (sequence_before(slot->sequence, ack_next)) {
            slot->active = false;
            continue;
        }
        offset = slot->sequence - ack_next;
        if ((offset < 32U) && ((bitmap & (1UL << offset)) != 0U)) {
            slot->active = false;
        }
    }
}

uint8_t radio_window_tx_active(const radio_window_t *window)
{
    uint8_t count = 0U;
    uint8_t index;

    if (window == NULL) {
        return 0U;
    }
    for (index = 0U; index < RADIO_WINDOW_SIZE; ++index) {
        if (window->tx[index].active) {
            ++count;
        }
    }
    return count;
}

uint8_t radio_window_tx_free(const radio_window_t *window)
{
    uint8_t active = radio_window_tx_active(window);

    return active >= RADIO_WINDOW_SIZE
               ? 0U
               : (uint8_t)(RADIO_WINDOW_SIZE - active);
}

bool radio_window_tx_due(const radio_window_t *window, uint32_t now_ms,
                         uint32_t timeout_ms, uint32_t *sequence)
{
    uint8_t index;

    if ((window == NULL) || (sequence == NULL)) {
        return false;
    }
    for (index = 0U; index < RADIO_WINDOW_SIZE; ++index) {
        const radio_window_slot_t *slot = &window->tx[index];

        if (slot->active &&
            (!slot->sent ||
             ((uint32_t)(now_ms - slot->last_sent_ms) >= timeout_ms))) {
            *sequence = slot->sequence;
            return true;
        }
    }
    return false;
}

bool radio_window_tx_mark_sent(radio_window_t *window, uint32_t sequence,
                               uint32_t now_ms)
{
    radio_window_slot_t *slot;

    if (window == NULL) {
        return false;
    }
    slot = tx_slot_find(window, sequence);
    if (slot == NULL) {
        return false;
    }
    slot->sent = true;
    slot->last_sent_ms = now_ms;
    return true;
}

bool radio_window_rx_accept(radio_window_t *window, uint32_t sequence,
                            const uint8_t *payload, uint8_t length)
{
    uint32_t offset;
    radio_window_slot_t *slot;

    if ((window == NULL) || (payload == NULL) || (length == 0U) ||
        (length > RADIO_WINDOW_MAX_PAYLOAD)) {
        return false;
    }
    if (sequence_before(sequence, window->rx_next)) {
        return false;
    }
    offset = sequence - window->rx_next;
    if (offset >= RADIO_WINDOW_SIZE) {
        return false;
    }
    if ((window->rx_bitmap & (1UL << offset)) != 0U) {
        return false;
    }
    slot = free_slot(window->rx);
    if (slot == NULL) {
        return false;
    }
    slot->active = true;
    slot->sequence = sequence;
    slot->length = length;
    memcpy(slot->payload, payload, length);
    window->rx_bitmap |= 1UL << offset;
    return true;
}

void radio_window_rx_ack(const radio_window_t *window, uint32_t *ack_next,
                         uint32_t *bitmap)
{
    if ((window == NULL) || (ack_next == NULL) || (bitmap == NULL)) {
        return;
    }
    *ack_next = window->rx_next;
    *bitmap = window->rx_bitmap;
}

bool radio_window_rx_take(radio_window_t *window, uint8_t *payload,
                          uint8_t *length)
{
    radio_window_slot_t *slot;

    if ((window == NULL) || (payload == NULL) || (length == NULL) ||
        ((window->rx_bitmap & 1U) == 0U)) {
        return false;
    }
    slot = NULL;
    for (uint8_t index = 0U; index < RADIO_WINDOW_SIZE; ++index) {
        if (window->rx[index].active &&
            (window->rx[index].sequence == window->rx_next)) {
            slot = &window->rx[index];
            break;
        }
    }
    if (slot == NULL) {
        return false;
    }
    memcpy(payload, slot->payload, slot->length);
    *length = slot->length;
    slot->active = false;
    window->rx_next++;
    window->rx_bitmap >>= 1U;
    return true;
}
