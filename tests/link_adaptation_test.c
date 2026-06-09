#include <assert.h>

#include "link_adaptation.h"

static sx128x_profile_t sample_successes(link_adaptation_t *state,
                                        int16_t rssi_dbm_x2,
                                        uint8_t count, uint32_t now_ms)
{
    uint8_t index;
    sx128x_profile_t profile = state->current_profile;

    for (index = 0U; index < count; ++index) {
        link_adaptation_record_success(state, rssi_dbm_x2);
        profile = link_adaptation_recommend(state, now_ms);
    }
    return profile;
}

int main(void)
{
    link_adaptation_t state;
    sx128x_profile_t profile;

    link_adaptation_init(&state, SX128X_PROFILE_GFSK_1M, 0U);
    profile = sample_successes(&state, -90, 4U, 2000U);
    assert(profile == SX128X_PROFILE_GFSK_1M);

    profile = sample_successes(&state, -90, 4U, 3000U);
    assert(profile == SX128X_PROFILE_FLRC_1M3);
    link_adaptation_profile_changed(&state, profile, 3000U);

    profile = sample_successes(&state, -90, 4U, 6003U);
    assert(profile == SX128X_PROFILE_GFSK_2M);
    link_adaptation_profile_changed(&state, profile, 6003U);

    link_adaptation_record_failure(&state);
    link_adaptation_record_failure(&state);
    link_adaptation_record_failure(&state);
    profile = link_adaptation_recommend(&state, 9003U);
    assert(profile == SX128X_PROFILE_FLRC_1M3);

    return 0;
}
