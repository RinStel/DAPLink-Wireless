#ifndef BOOT_CONFIRM_ONCE_H
#define BOOT_CONFIRM_ONCE_H

#include <stdbool.h>

#include "firmware_layout.h"

typedef bool (*boot_confirm_fn_t)(firmware_slot_t slot);

typedef struct {
    bool attempted;
    bool runtime_error;
} boot_confirm_once_t;

bool boot_confirm_once(boot_confirm_once_t *guard,
                       firmware_slot_t running_slot,
                       boot_confirm_fn_t confirm);

#endif
