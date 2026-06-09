#ifndef LINK_ADAPTATION_H
#define LINK_ADAPTATION_H

#include <stdbool.h>
#include <stdint.h>

#include "sx128x.h"

typedef struct {
    bool initialized;
    int16_t rssi_ewma_dbm_x2;
    uint16_t success_count;
    uint16_t failure_count;
    uint8_t upgrade_votes;
    uint8_t downgrade_votes;
    uint32_t last_change_ms;
    sx128x_profile_t current_profile;
} link_adaptation_t;

void link_adaptation_init(link_adaptation_t *state,
                          sx128x_profile_t initial_profile,
                          uint32_t now_ms);
void link_adaptation_record_success(link_adaptation_t *state,
                                    int16_t rssi_dbm_x2);
void link_adaptation_record_failure(link_adaptation_t *state);
sx128x_profile_t link_adaptation_recommend(link_adaptation_t *state,
                                           uint32_t now_ms);
void link_adaptation_profile_changed(link_adaptation_t *state,
                                     sx128x_profile_t profile,
                                     uint32_t now_ms);

#endif
