#include "frequency_hopping.h"

#include <stddef.h>
#include <string.h>

#define HOP_FIRST_FREQUENCY_HZ 2405000000UL
#define HOP_CHANNEL_SPACING_HZ     5000000UL
#define HOP_BAD_CHANNEL_PENALTY          3U
#define HOP_PENALTY_DECAY_SUCCESSES     32U

static uint32_t mix32(uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7FEB352DU;
    value ^= value >> 15;
    value *= 0x846CA68BU;
    value ^= value >> 16;
    return value;
}

void frequency_hopping_init(frequency_hopping_t *state,
                            uint32_t network_id)
{
    if (state == NULL) {
        return;
    }
    state->seed = mix32(network_id ^ 0x484F5053U);
    memset(state->penalty, 0, sizeof(state->penalty));
    state->decay_counter = 0U;
}

uint8_t frequency_hopping_rendezvous(
    const frequency_hopping_t *state)
{
    if (state == NULL) {
        return 0U;
    }
    return (uint8_t)(state->seed &
                     (FREQUENCY_HOPPING_CHANNEL_COUNT - 1U));
}

uint8_t frequency_hopping_select(const frequency_hopping_t *state,
                                 uint32_t token, uint8_t attempt,
                                 uint8_t excluded_channel)
{
    uint8_t fallback = 0U;
    uint8_t index;
    uint8_t start;
    uint8_t stride;

    if (state == NULL) {
        return 0U;
    }
    start = (uint8_t)(mix32(state->seed ^ token) &
                      (FREQUENCY_HOPPING_CHANNEL_COUNT - 1U));
    stride = (uint8_t)(((state->seed >> 8) | 1U) &
                       (FREQUENCY_HOPPING_CHANNEL_COUNT - 1U));
    if (stride == 0U) {
        stride = 1U;
    }
    start = (uint8_t)((start + attempt * stride) &
                      (FREQUENCY_HOPPING_CHANNEL_COUNT - 1U));
    fallback = start;

    for (index = 0U; index < FREQUENCY_HOPPING_CHANNEL_COUNT;
         ++index) {
        uint8_t channel =
            (uint8_t)((start + index * stride) &
                      (FREQUENCY_HOPPING_CHANNEL_COUNT - 1U));

        if ((channel != excluded_channel) &&
            (state->penalty[channel] < HOP_BAD_CHANNEL_PENALTY)) {
            return channel;
        }
        if (channel != excluded_channel) {
            fallback = channel;
        }
    }
    return fallback;
}

uint32_t frequency_hopping_frequency_hz(uint8_t channel)
{
    if (!frequency_hopping_channel_valid(channel)) {
        return 0U;
    }
    return HOP_FIRST_FREQUENCY_HZ +
           (uint32_t)channel * HOP_CHANNEL_SPACING_HZ;
}

bool frequency_hopping_channel_valid(uint8_t channel)
{
    return channel < FREQUENCY_HOPPING_CHANNEL_COUNT;
}

void frequency_hopping_record_success(frequency_hopping_t *state,
                                      uint8_t channel)
{
    uint8_t index;

    if ((state == NULL) ||
        !frequency_hopping_channel_valid(channel)) {
        return;
    }
    if (state->penalty[channel] != 0U) {
        --state->penalty[channel];
    }
    if (++state->decay_counter < HOP_PENALTY_DECAY_SUCCESSES) {
        return;
    }
    state->decay_counter = 0U;
    for (index = 0U; index < FREQUENCY_HOPPING_CHANNEL_COUNT;
         ++index) {
        if (state->penalty[index] != 0U) {
            --state->penalty[index];
        }
    }
}

void frequency_hopping_record_failure(frequency_hopping_t *state,
                                      uint8_t channel)
{
    if ((state != NULL) &&
        frequency_hopping_channel_valid(channel) &&
        (state->penalty[channel] != UINT8_MAX)) {
        ++state->penalty[channel];
    }
}
