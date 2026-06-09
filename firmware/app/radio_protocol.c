#include "radio_protocol.h"

#include <stddef.h>
#include <string.h>

#define RADIO_PROTOCOL_MAGIC_0 0x44U
#define RADIO_PROTOCOL_MAGIC_1 0x53U
#define RADIO_PROTOCOL_VERSION 0x03U

static void encode_u32_be(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t)(value >> 24);
    output[1] = (uint8_t)(value >> 16);
    output[2] = (uint8_t)(value >> 8);
    output[3] = (uint8_t)value;
}

static uint32_t decode_u32_be(const uint8_t *input)
{
    return ((uint32_t)input[0] << 24) |
           ((uint32_t)input[1] << 16) |
           ((uint32_t)input[2] << 8) |
           input[3];
}

static uint32_t payload_digest(radio_frame_type_t type,
                               const uint8_t *payload, uint8_t length)
{
    uint32_t hash = (2166136261U ^ (uint8_t)type) * 16777619U;
    uint8_t index;

    for (index = 0U; index < length; ++index) {
        hash = (hash ^ payload[index]) * 16777619U;
    }
    return hash;
}

uint8_t radio_protocol_build(uint8_t *frame, radio_frame_type_t type,
                             uint32_t network_id, uint32_t session,
                             uint32_t sequence, const uint8_t *payload,
                             uint8_t payload_length)
{
    if ((frame == NULL) ||
        (payload_length > RADIO_PROTOCOL_PAYLOAD_SIZE) ||
        ((payload_length != 0U) && (payload == NULL))) {
        return 0U;
    }
    frame[0] = RADIO_PROTOCOL_MAGIC_0;
    frame[1] = RADIO_PROTOCOL_MAGIC_1;
    frame[2] = RADIO_PROTOCOL_VERSION;
    frame[3] = (uint8_t)type;
    encode_u32_be(&frame[4], network_id);
    encode_u32_be(&frame[8], session);
    encode_u32_be(&frame[12], sequence);
    frame[16] = payload_length;
    if (payload_length != 0U) {
        memcpy(&frame[RADIO_PROTOCOL_HEADER_SIZE], payload,
               payload_length);
    }
    return (uint8_t)(RADIO_PROTOCOL_HEADER_SIZE + payload_length);
}

bool radio_protocol_parse(const uint8_t *frame, uint8_t frame_length,
                          uint32_t network_id,
                          radio_frame_view_t *view)
{
    uint8_t payload_length;

    if ((frame == NULL) || (view == NULL) ||
        (frame_length < RADIO_PROTOCOL_HEADER_SIZE) ||
        (frame_length > RADIO_PROTOCOL_FRAME_SIZE) ||
        (frame[0] != RADIO_PROTOCOL_MAGIC_0) ||
        (frame[1] != RADIO_PROTOCOL_MAGIC_1) ||
        (frame[2] != RADIO_PROTOCOL_VERSION) ||
        (decode_u32_be(&frame[4]) != network_id)) {
        return false;
    }
    payload_length = frame[16];
    if (payload_length != frame_length - RADIO_PROTOCOL_HEADER_SIZE) {
        return false;
    }
    view->type = (radio_frame_type_t)frame[3];
    view->session = decode_u32_be(&frame[8]);
    view->sequence = decode_u32_be(&frame[12]);
    view->payload = &frame[RADIO_PROTOCOL_HEADER_SIZE];
    view->payload_length = payload_length;
    return true;
}

void radio_protocol_key_get(const radio_frame_view_t *view,
                            radio_frame_key_t *key)
{
    if ((view == NULL) || (key == NULL)) {
        return;
    }
    key->session = view->session;
    key->sequence = view->sequence;
    key->type = view->type;
    key->payload_length = view->payload_length;
    key->payload_digest =
        payload_digest(view->type, view->payload, view->payload_length);
}

bool radio_protocol_key_equal(const radio_frame_key_t *left,
                              const radio_frame_key_t *right)
{
    return (left != NULL) && (right != NULL) &&
           (left->session == right->session) &&
           (left->sequence == right->sequence) &&
           (left->type == right->type) &&
           (left->payload_length == right->payload_length) &&
           (left->payload_digest == right->payload_digest);
}
