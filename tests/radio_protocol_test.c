#include <assert.h>
#include <string.h>

#include "frequency_hopping.h"
#include "radio_protocol.h"

/* 验证精确帧格式、网络隔离和重复检测键。 */
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
    radio_protocol_ack_t ack;
    radio_protocol_ack_t decoded_ack;

    length = radio_protocol_build(frame, RADIO_FRAME_DATA,
                                  0x12345678U, 0xAABBCCDDU,
                                  7U, payload, sizeof(payload));
    assert(length == RADIO_PROTOCOL_HEADER_SIZE + sizeof(payload));
    assert(frame[2] == 2U);
    assert(frame[2] == RADIO_PROTOCOL_VERSION);
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

    frame[2] = 1U;
    assert(!radio_protocol_parse(frame, length, 0x12345678U, &view));
    frame[2] = RADIO_PROTOCOL_VERSION;
    frame[16] = 5U;
    assert(!radio_protocol_parse(frame, length, 0x12345678U, &view));
    frame[16] = (uint8_t)sizeof(payload);
    frame[3] = 0x7FU;
    assert(!radio_protocol_parse(frame, length, 0x12345678U, &view));
    frame[3] = RADIO_FRAME_DATA;

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
               frame, RADIO_FRAME_DATA, 0U, 0U, 0U, maximum_payload,
               (uint8_t)(RADIO_PROTOCOL_PAYLOAD_SIZE + 1U)) == 0U);
    assert(radio_protocol_build(
               frame, RADIO_FRAME_DATA, 0U, 0U, 0U, NULL, 1U) == 0U);
    assert(!radio_protocol_parse(NULL, length, 0x12345678U, &view));

    length = radio_protocol_build(
        frame, RADIO_FRAME_SWD_ABORT, 0x12345678U, 1U, 9U,
        payload, 1U);
    assert(radio_protocol_parse(frame, length, 0x12345678U, &view));
    assert(view.type == RADIO_FRAME_SWD_ABORT);
    assert(view.payload_length == 1U);

    memset(&ack, 0, sizeof(ack));
    ack.ack_next = 0x10203040U;
    ack.bitmap = 0x0000000DU;
    ack.flags = RADIO_PROTOCOL_ACK_FLAG_HOP_VALID;
    ack.next_channel = 17U;
    ack.rssi_dbm_x2 = -123;
    ack.error_status = 2U;
    ack.tx_rx_status = 3U;
    ack.sync_address_status = 4U;
    ack.profile = 1U;
    ack.current_channel = 9U;
    assert(radio_protocol_ack_encode(frame, sizeof(frame), &ack));
    assert(radio_protocol_ack_decode(
               frame, RADIO_PROTOCOL_ACK_PAYLOAD_SIZE, &decoded_ack));
    assert(decoded_ack.ack_next == ack.ack_next);
    assert(decoded_ack.bitmap == ack.bitmap);
    assert(decoded_ack.flags == ack.flags);
    assert(decoded_ack.next_channel == ack.next_channel);
    assert(decoded_ack.rssi_dbm_x2 == ack.rssi_dbm_x2);
    assert(decoded_ack.error_status == ack.error_status);
    assert(decoded_ack.tx_rx_status == ack.tx_rx_status);
    assert(decoded_ack.sync_address_status == ack.sync_address_status);
    assert(decoded_ack.profile == ack.profile);
    assert(decoded_ack.current_channel == ack.current_channel);
    assert(!radio_protocol_ack_decode(
        frame, (uint8_t)(RADIO_PROTOCOL_ACK_PAYLOAD_SIZE - 1U),
        &decoded_ack));

    frequency_hopping_init(&hopping, 0x12345678U);
    channel = frequency_hopping_rendezvous(&hopping);
    assert(frequency_hopping_channel_valid(channel));
    assert(frequency_hopping_frequency_hz(channel) >= 2405000000UL);
    assert(frequency_hopping_select(
               &hopping, 1U, 0U, channel) != channel);
    return 0;
}
