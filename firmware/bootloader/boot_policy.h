#ifndef BOOT_POLICY_H
#define BOOT_POLICY_H

#include <stdbool.h>

#include "boot_state.h"

typedef enum {
    BOOT_ACTION_DFU_NORMAL = 0U,
    BOOT_ACTION_DFU_RECOVERY,
    BOOT_ACTION_START_A,
    BOOT_ACTION_START_B,
    BOOT_ACTION_ROLLBACK_TO_A,
    BOOT_ACTION_ROLLBACK_TO_B
} boot_action_t;

typedef struct {
    bool key_recovery_requested;
    bool mailbox_dfu_requested;
    bool state_valid;
    bool confirmed_image_valid;
    bool pending_image_valid;
    bool other_image_valid;
    uint32_t attempts_used;
    boot_state_t state;
} boot_policy_input_t;

typedef struct {
    boot_action_t action;
} boot_decision_t;

boot_decision_t boot_policy_decide(const boot_policy_input_t *input);

#endif
