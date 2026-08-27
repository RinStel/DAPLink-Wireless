#include "boot_confirm_once.h"

#include <stddef.h>

bool boot_confirm_once(boot_confirm_once_t *guard,
                       firmware_slot_t running_slot,
                       boot_confirm_fn_t confirm)
{
    if ((guard == NULL) || (confirm == NULL)) {
        return false;
    }
    if (guard->attempted) {
        return !guard->runtime_error;
    }
    guard->attempted = true;
    if (!confirm(running_slot)) {
        guard->runtime_error = true;
        return false;
    }
    return true;
}
