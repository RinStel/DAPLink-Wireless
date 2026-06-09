#include <assert.h>
#include <string.h>

#include "frequency_hopping.h"
#include "radio_protocol.h"

int main(void)
{
    uint8_t frame[RADIO_PROTOCOL_FRAME_SIZE];
    uint8_t maximum_payload[RADIO_PROTOCOL_PAYLOAD_SIZE];
    const uint8_t payload[] = {1U, 2U, 3U, 4U};
    radio_frame_view_t view;
    radio_frame_key_t first;
    radio_frame_key_t second;
    frequency_hopping_t hopping;
    uint8_t length;
    uint8_t channel;
    uint8_t index;

    length = radio_protocol_build(frame, RADIO_FRAME_DATA,
                                  0x12345678U, 0xAABBCCDDU,
                                  7U, payload, sizeof(payload));
    assert(length == RADIO_PROTOCOL_HEADER_SIZE + sizeof(payload));
    assert(radio_protocol_parse(frame, length, 0x12345678U, &view));
    assert(view.type == RADIO_FRAME_DATA);
    assert(view.session == 0xAABBCCDDU);
    assert(view.sequence == 7U);
    assert(view.payload_length == sizeof(payload));
    assert(memcmp(view.payload, payload, sizeof(payload)) == 0);

    radio_protocol_key_get(&view, &first);
    second = first;
    assert(radio_protocol_key_equal(&first, &second));
    ++second.sequence;
    assert(!radio_protocol_key_equal(&first, &second));

    frame[2] ^= 1U;
    assert(!radio_protocol_parse(frame, length, 0x12345678U, &view));
    frame[2] ^= 1U;
    frame[16] = 5U;
    assert(!radio_protocol_parse(frame, length, 0x12345678U, &view));

    memset(maximum_payload, 0xA5, sizeof(maximum_payload));
    length = radio_protocol_build(
        frame, RADIO_FRAME_SWD_RESPONSE, 0x12345678U, 1U, 2U,
        maximum_payload, sizeof(maximum_payload));
    assert(length == RADIO_PROTOCOL_FRAME_SIZE);
    for (index = 0U; index < length; ++index) {
        assert(!radio_protocol_parse(frame, index, 0x12345678U, &view));
    }
    assert(radio_protocol_parse(frame, length, 0x12345678U, &view));
    assert(view.payload_length == RADIO_PROTOCOL_PAYLOAD_SIZE);
    assert(radio_protocol_build(
               frame, RADIO_FRAME_DATA, 0U, 0U, 0U, NULL, 1U) == 0U);
    assert(!radio_protocol_parse(NULL, length, 0x12345678U, &view));

    frequency_hopping_init(&hopping, 0x12345678U);
    channel = frequency_hopping_rendezvous(&hopping);
    assert(frequency_hopping_channel_valid(channel));
    assert(frequency_hopping_frequency_hz(channel) >= 2405000000UL);
    assert(frequency_hopping_select(
               &hopping, 1U, 0U, channel) != channel);
    return 0;
}
