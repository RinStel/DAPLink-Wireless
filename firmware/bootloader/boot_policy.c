#include "boot_policy.h"

boot_decision_t boot_policy_decide(const boot_policy_input_t *input)
{
    boot_decision_t decision = {BOOT_ACTION_DFU_RECOVERY};
    firmware_slot_t confirmed;
    firmware_slot_t other;

    if (input == NULL) {
        return decision;
    }
    if (input->key_recovery_requested) {
        decision.action = BOOT_ACTION_DFU_RECOVERY;
        return decision;
    }
    if (input->mailbox_dfu_requested) {
        decision.action = BOOT_ACTION_DFU_NORMAL;
        return decision;
    }
    if (!input->state_valid) {
        return decision;
    }

    confirmed = input->state.confirmed_slot;
    other = confirmed == FIRMWARE_SLOT_A ? FIRMWARE_SLOT_B : FIRMWARE_SLOT_A;
    if (input->state.phase == BOOT_PHASE_PENDING_TEST) {
        if (input->pending_image_valid && (input->attempts_used < 3U)) {
            decision.action = input->state.pending_slot == FIRMWARE_SLOT_A
                              ? BOOT_ACTION_START_A : BOOT_ACTION_START_B;
            return decision;
        }
        decision.action = confirmed == FIRMWARE_SLOT_A
                          ? BOOT_ACTION_ROLLBACK_TO_A
                          : BOOT_ACTION_ROLLBACK_TO_B;
        return decision;
    }
    if (input->confirmed_image_valid) {
        decision.action = confirmed == FIRMWARE_SLOT_A
                          ? BOOT_ACTION_START_A : BOOT_ACTION_START_B;
    } else if (input->other_image_valid) {
        decision.action = other == FIRMWARE_SLOT_A
                          ? BOOT_ACTION_START_A : BOOT_ACTION_START_B;
    }
    return decision;
}
