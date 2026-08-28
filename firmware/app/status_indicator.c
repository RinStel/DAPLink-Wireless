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
#define INITIALIZATION_FAILURE_BLINK_MS 200U
#define RUNTIME_ERROR_BLINK_MS 500U
#define RUNTIME_ERROR_RECOVERY_MS 1000U
#define IDLE_BLINK_MS 1000U
#define COMMUNICATION_BLINK_MS 450U
#define PROGRAMMING_BLINK_MS 150U

static bool blink_on(uint32_t now_ms, uint32_t interval_ms)
{
    return ((now_ms / interval_ms) % 2U) == 0U;
}

static status_indicator_leds_t healthy_leds(status_indicator_mode_t mode,
                                            status_indicator_activity_t activity,
                                            uint32_t now_ms)
{
    status_indicator_leds_t leds = {false, false, false};
    uint32_t interval_ms;
    bool on;

    switch (activity) {
    case STATUS_INDICATOR_ACTIVITY_PROGRAMMING:
        interval_ms = PROGRAMMING_BLINK_MS;
        break;
    case STATUS_INDICATOR_ACTIVITY_COMMUNICATION:
        interval_ms = COMMUNICATION_BLINK_MS;
        break;
    case STATUS_INDICATOR_ACTIVITY_NONE:
    default:
        interval_ms = IDLE_BLINK_MS;
        break;
    }
    on = blink_on(now_ms, interval_ms);

    switch (mode) {
    case STATUS_INDICATOR_MODE_WIRELESS_HOST:
        leds.blue = on;
        break;
    case STATUS_INDICATOR_MODE_WIRELESS_SLAVE:
        leds.green = on;
        break;
    case STATUS_INDICATOR_MODE_WIRED:
    default:
        /* 绿蓝同时点亮，表示青色。 */
        leds.green = on;
        leds.blue = on;
        break;
    }
    return leds;
}

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
                                                status_indicator_mode_t mode,
                                                status_indicator_activity_t activity,
                                                uint32_t now_ms)
{
    status_indicator_leds_t leds = {false, false, false};

    if (indicator->state == STATUS_INDICATOR_INITIALIZATION_FAILURE) {
        leds.red = blink_on(now_ms, INITIALIZATION_FAILURE_BLINK_MS);
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
        leds.red = blink_on(now_ms, RUNTIME_ERROR_BLINK_MS);
    } else {
        leds = healthy_leds(mode, activity, now_ms);
    }

    return leds;
}
