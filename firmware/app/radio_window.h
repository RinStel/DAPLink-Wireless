#ifndef RADIO_WINDOW_H
#define RADIO_WINDOW_H

#include <stdbool.h>
#include <stdint.h>

#define RADIO_WINDOW_SIZE 4U
#define RADIO_WINDOW_MAX_PAYLOAD 110U

typedef struct {
    bool active;
    bool sent;
    uint32_t sequence;
    uint32_t last_sent_ms;
    uint8_t length;
    uint8_t payload[RADIO_WINDOW_MAX_PAYLOAD];
} radio_window_slot_t;

typedef struct {
    uint32_t tx_next;
    uint32_t rx_next;
    uint32_t rx_bitmap;
    radio_window_slot_t tx[RADIO_WINDOW_SIZE];
    radio_window_slot_t rx[RADIO_WINDOW_SIZE];
} radio_window_t;

void radio_window_init(radio_window_t *window, uint32_t tx_sequence,
                       uint32_t rx_sequence);
bool radio_window_tx_push(radio_window_t *window, const uint8_t *payload,
                          uint8_t length, uint32_t *sequence);
void radio_window_tx_ack(radio_window_t *window, uint32_t ack_next,
                         uint32_t bitmap);
uint8_t radio_window_tx_active(const radio_window_t *window);
uint8_t radio_window_tx_free(const radio_window_t *window);
bool radio_window_tx_due(const radio_window_t *window, uint32_t now_ms,
                         uint32_t timeout_ms, uint32_t *sequence);
bool radio_window_tx_mark_sent(radio_window_t *window, uint32_t sequence,
                               uint32_t now_ms);

bool radio_window_rx_accept(radio_window_t *window, uint32_t sequence,
                            const uint8_t *payload, uint8_t length);
void radio_window_rx_ack(const radio_window_t *window, uint32_t *ack_next,
                        uint32_t *bitmap);
bool radio_window_rx_take(radio_window_t *window, uint8_t *payload,
                          uint8_t *length);

#endif
