#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "radio_window.h"

int main(void)
{
    radio_window_t window;
    uint8_t payload[4];
    uint8_t output[4];
    uint8_t length;
    uint32_t sequence[4];
    uint32_t ack_next;
    uint32_t bitmap;
    uint8_t index;

    radio_window_init(&window, 100U, 100U);
    for (index = 0U; index < RADIO_WINDOW_SIZE; ++index) {
        payload[0] = index;
        assert(radio_window_tx_push(
            &window, payload, 1U, &sequence[index]));
    }
    assert(radio_window_tx_free(&window) == 0U);
    payload[0] = 0xFFU;
    assert(!radio_window_tx_push(&window, payload, 1U, &sequence[0]));

    assert(radio_window_rx_accept(&window, 102U, (uint8_t[]){2U}, 1U));
    assert(radio_window_rx_accept(&window, 100U, (uint8_t[]){0U}, 1U));
    assert(radio_window_rx_accept(&window, 101U, (uint8_t[]){1U}, 1U));
    assert(radio_window_rx_accept(&window, 103U, (uint8_t[]){3U}, 1U));
    assert(!radio_window_rx_accept(&window, 102U, (uint8_t[]){2U}, 1U));
    radio_window_rx_ack(&window, &ack_next, &bitmap);
    assert(ack_next == 100U);
    assert((bitmap & 0x0FU) == 0x0FU);

    assert(radio_window_rx_take(&window, output, &length));
    assert(length == 1U && output[0] == 0U);
    assert(radio_window_rx_take(&window, output, &length));
    assert(length == 1U && output[0] == 1U);
    assert(radio_window_rx_take(&window, output, &length));
    assert(length == 1U && output[0] == 2U);
    assert(radio_window_rx_take(&window, output, &length));
    assert(length == 1U && output[0] == 3U);
    assert(!radio_window_rx_take(&window, output, &length));

    radio_window_tx_ack(&window, 102U, 0x01U);
    assert(radio_window_tx_active(&window) == 1U);
    radio_window_tx_ack(&window, 104U, 0U);
    assert(radio_window_tx_active(&window) == 0U);
    assert(radio_window_tx_free(&window) == RADIO_WINDOW_SIZE);

    assert(radio_window_tx_push(&window, payload, 1U, &sequence[0]));
    assert(radio_window_tx_due(&window, 100U, 10U, &sequence[0]));
    radio_window_tx_mark_sent(&window, sequence[0], 100U);
    assert(!radio_window_tx_due(&window, 109U, 10U, &sequence[0]));
    assert(radio_window_tx_due(&window, 110U, 10U, &sequence[0]));
    return 0;
}
