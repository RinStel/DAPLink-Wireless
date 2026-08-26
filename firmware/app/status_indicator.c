/*
 * DAPLink-Wireless — Wireless CMSIS-DAP v2 debug probe firmware
 * Copyright (C) 2025 RinStel <me@rinx.nz>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */
#include "status_indicator.h"

/* LED 策略独立于板级极性，使状态迁移无需 MCU GPIO 寄存器也可测试。 */
#define INITIALIZATION_FAILURE_BLINK_MS 350U
#define RUNTIME_ERROR_RECOVERY_MS 1000U

void status_indicator_init(status_indicator_t *indicator,
                           bool initialization_failed)
{
    indicator->state = initialization_failed
                           ? STATUS_INDICATOR_INITIALIZATION_FAILURE
                           : STATUS_INDICATOR_HEALTHY;
    indicator->recovery_started_at_ms = 0U;
    indicator->recovery_pending = false;
}

status_indicator_leds_t status_indicator_update(status_indicator_t *indicator,
                                                bool runtime_error,
                                                bool activity,
                                                bool heartbeat_on,
                                                uint32_t now_ms)
{
    status_indicator_leds_t leds = {false, false, false};

    if (indicator->state == STATUS_INDICATOR_INITIALIZATION_FAILURE) {
        leds.red = (now_ms / INITIALIZATION_FAILURE_BLINK_MS) % 2U == 0U;
        return leds;
    }

    if (runtime_error) {
        indicator->state = STATUS_INDICATOR_RUNTIME_ERROR;
        indicator->recovery_pending = false;
    } else if (indicator->state == STATUS_INDICATOR_RUNTIME_ERROR) {
        if (!indicator->recovery_pending) {
            indicator->recovery_started_at_ms = now_ms;
            indicator->recovery_pending = true;
        } else if ((uint32_t)(now_ms - indicator->recovery_started_at_ms) >=
                   RUNTIME_ERROR_RECOVERY_MS) {
            indicator->state = STATUS_INDICATOR_HEALTHY;
            indicator->recovery_pending = false;
        }
    }

    if (indicator->state == STATUS_INDICATOR_RUNTIME_ERROR) {
        leds.red = true;
    } else if (activity) {
        leds.blue = true;
    } else {
        leds.green = heartbeat_on;
    }

    return leds;
}
