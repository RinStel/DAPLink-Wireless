#include <assert.h>
#include <stdbool.h>

#include "boot_confirm_once.h"

static unsigned s_calls;
static bool s_result = true;
static bool confirm_stub(firmware_slot_t slot)
{
    assert(slot == FIRMWARE_SLOT_B);
    ++s_calls;
    return s_result;
}

int main(void)
{
    boot_confirm_once_t guard = {false, false};

    assert(boot_confirm_once(&guard, FIRMWARE_SLOT_B, confirm_stub));
    assert(boot_confirm_once(&guard, FIRMWARE_SLOT_B, confirm_stub));
    assert(s_calls == 1U);

    guard = (boot_confirm_once_t){false, false};
    s_result = false;
    assert(!boot_confirm_once(&guard, FIRMWARE_SLOT_B, confirm_stub));
    assert(!boot_confirm_once(&guard, FIRMWARE_SLOT_B, confirm_stub));
    assert(guard.runtime_error);
    assert(s_calls == 2U);
    return 0;
}
