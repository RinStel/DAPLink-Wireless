#include <assert.h>
#include <string.h>

#include "boot_policy.h"

static boot_policy_input_t confirmed_input(void)
{
    boot_policy_input_t input;
    memset(&input, 0, sizeof(input));
    input.state_valid = true;
    input.confirmed_image_valid = true;
    input.other_image_valid = false;
    input.state.confirmed_slot = FIRMWARE_SLOT_A;
    input.state.phase = BOOT_PHASE_CONFIRMED;
    return input;
}

int main(void)
{
    boot_policy_input_t input = confirmed_input();

    assert(boot_policy_decide(&input).action == BOOT_ACTION_START_A);
    input.key_recovery_requested = true;
    assert(boot_policy_decide(&input).action == BOOT_ACTION_DFU_RECOVERY);

    input = confirmed_input();
    input.mailbox_dfu_requested = true;
    assert(boot_policy_decide(&input).action == BOOT_ACTION_DFU_NORMAL);

    input = confirmed_input();
    input.state.phase = BOOT_PHASE_PENDING_TEST;
    input.state.pending_slot = FIRMWARE_SLOT_B;
    input.pending_image_valid = true;
    input.attempts_used = 1U;
    assert(boot_policy_decide(&input).action == BOOT_ACTION_START_B);

    input.attempts_used = 3U;
    assert(boot_policy_decide(&input).action == BOOT_ACTION_ROLLBACK_TO_A);

    input.pending_image_valid = false;
    input.attempts_used = 0U;
    assert(boot_policy_decide(&input).action == BOOT_ACTION_ROLLBACK_TO_A);

    input = confirmed_input();
    input.state_valid = false;
    assert(boot_policy_decide(&input).action == BOOT_ACTION_DFU_RECOVERY);

    input = confirmed_input();
    input.confirmed_image_valid = false;
    input.other_image_valid = true;
    assert(boot_policy_decide(&input).action == BOOT_ACTION_START_B);

    return 0;
}
