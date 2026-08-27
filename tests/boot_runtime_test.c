#include <assert.h>
#include <stdint.h>

#include "boot_led.h"
#include "boot_policy.h"

int main(void)
{
    boot_policy_input_t input = {0};
    boot_decision_t decision;
    boot_led_rgb_t leds;

    input.state_valid = true;
    input.confirmed_image_valid = true;
    input.state.confirmed_slot = FIRMWARE_SLOT_A;
    input.state.phase = BOOT_PHASE_CONFIRMED;
    decision = boot_policy_decide(&input);
    assert(decision.action == BOOT_ACTION_START_A);

    input.state.phase = BOOT_PHASE_PENDING_TEST;
    input.state.pending_slot = FIRMWARE_SLOT_B;
    input.pending_image_valid = true;
    input.attempts_used = 0U;
    decision = boot_policy_decide(&input);
    assert(decision.action == BOOT_ACTION_START_B);
    leds = boot_led_update(BOOT_LED_TRIAL, 0U);
    assert(leds.green && !leds.red && !leds.blue);

    input.attempts_used = 3U;
    decision = boot_policy_decide(&input);
    assert(decision.action == BOOT_ACTION_ROLLBACK_TO_A);
    leds = boot_led_update(BOOT_LED_ROLLBACK, 0U);
    assert(leds.red != leds.blue);
    return 0;
}
