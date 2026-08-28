/*
 * DAPLink-Wireless — Wireless CMSIS-DAP v2 debug probe firmware
 * Copyright (C) 2025 RinStel <me@rinx.nz>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */
#include <assert.h>

#include "status_indicator.h"

/* 使用逻辑 LED 验证状态迁移，不依赖 GPIO 极性。 */
static void assert_leds(status_indicator_leds_t leds,
                        bool red, bool green, bool blue)
{
    assert(leds.red == red);
    assert(leds.green == green);
    assert(leds.blue == blue);
    assert((unsigned int)leds.red + (unsigned int)leds.green +
               (unsigned int)leds.blue <=
           2U);
}

static status_indicator_leds_t update(status_indicator_t *indicator,
                                      status_indicator_mode_t mode,
                                      status_indicator_activity_t activity,
                                      uint32_t now_ms)
{
    return status_indicator_update(indicator, false, mode, activity, now_ms);
}

static status_indicator_leds_t update_error(
    status_indicator_t *indicator, status_indicator_mode_t mode,
    status_indicator_activity_t activity, uint32_t now_ms)
{
    return status_indicator_update(indicator, true, mode, activity, now_ms);
}

int main(void)
{
    status_indicator_t indicator;

    status_indicator_init(&indicator, true);
    assert_leds(update(&indicator, STATUS_INDICATOR_MODE_WIRED,
                       STATUS_INDICATOR_ACTIVITY_NONE, 0U),
                true, false, false);
    assert_leds(update(&indicator, STATUS_INDICATOR_MODE_WIRED,
                       STATUS_INDICATOR_ACTIVITY_NONE, 199U),
                true, false, false);
    assert_leds(update(&indicator, STATUS_INDICATOR_MODE_WIRED,
                       STATUS_INDICATOR_ACTIVITY_NONE, 200U),
                false, false, false);
    assert_leds(update(&indicator, STATUS_INDICATOR_MODE_WIRED,
                       STATUS_INDICATOR_ACTIVITY_NONE, 400U),
                true, false, false);

    status_indicator_init(&indicator, false);
    assert_leds(update(&indicator, STATUS_INDICATOR_MODE_WIRELESS_HOST,
                       STATUS_INDICATOR_ACTIVITY_NONE, 0U),
                false, false, true);
    assert_leds(update(&indicator, STATUS_INDICATOR_MODE_WIRELESS_HOST,
                       STATUS_INDICATOR_ACTIVITY_NONE, 999U),
                false, false, true);
    assert_leds(update(&indicator, STATUS_INDICATOR_MODE_WIRELESS_HOST,
                       STATUS_INDICATOR_ACTIVITY_NONE, 1000U),
                false, false, false);
    assert_leds(update(&indicator, STATUS_INDICATOR_MODE_WIRELESS_SLAVE,
                       STATUS_INDICATOR_ACTIVITY_NONE, 0U),
                false, true, false);
    assert_leds(update(&indicator, STATUS_INDICATOR_MODE_WIRED,
                       STATUS_INDICATOR_ACTIVITY_NONE, 0U),
                false, true, true);
    assert_leds(update(&indicator, STATUS_INDICATOR_MODE_WIRED,
                       STATUS_INDICATOR_ACTIVITY_NONE, 1000U),
                false, false, false);

    assert_leds(update(&indicator, STATUS_INDICATOR_MODE_WIRELESS_HOST,
                       STATUS_INDICATOR_ACTIVITY_COMMUNICATION, 0U),
                false, false, true);
    assert_leds(update(&indicator, STATUS_INDICATOR_MODE_WIRELESS_HOST,
                       STATUS_INDICATOR_ACTIVITY_COMMUNICATION, 449U),
                false, false, true);
    assert_leds(update(&indicator, STATUS_INDICATOR_MODE_WIRELESS_HOST,
                       STATUS_INDICATOR_ACTIVITY_COMMUNICATION, 450U),
                false, false, false);

    assert_leds(update(&indicator, STATUS_INDICATOR_MODE_WIRELESS_HOST,
                       STATUS_INDICATOR_ACTIVITY_PROGRAMMING, 0U),
                false, false, true);
    assert_leds(update(&indicator, STATUS_INDICATOR_MODE_WIRELESS_HOST,
                       STATUS_INDICATOR_ACTIVITY_PROGRAMMING, 149U),
                false, false, true);
    assert_leds(update(&indicator, STATUS_INDICATOR_MODE_WIRELESS_HOST,
                       STATUS_INDICATOR_ACTIVITY_PROGRAMMING, 150U),
                false, false, false);

    status_indicator_init(&indicator, false);
    assert_leds(update_error(&indicator, STATUS_INDICATOR_MODE_WIRELESS_HOST,
                             STATUS_INDICATOR_ACTIVITY_NONE, 0U),
                true, false, false);
    assert_leds(update_error(&indicator, STATUS_INDICATOR_MODE_WIRELESS_HOST,
                             STATUS_INDICATOR_ACTIVITY_NONE, 499U),
                true, false, false);
    assert_leds(update_error(&indicator, STATUS_INDICATOR_MODE_WIRELESS_HOST,
                             STATUS_INDICATOR_ACTIVITY_NONE, 500U),
                false, false, false);
    assert_leds(update_error(&indicator, STATUS_INDICATOR_MODE_WIRELESS_HOST,
                             STATUS_INDICATOR_ACTIVITY_NONE, 1000U),
                true, false, false);
    assert_leds(update(&indicator, STATUS_INDICATOR_MODE_WIRELESS_HOST,
                       STATUS_INDICATOR_ACTIVITY_NONE, 1001U),
                true, false, false);
    assert_leds(update(&indicator, STATUS_INDICATOR_MODE_WIRELESS_HOST,
                       STATUS_INDICATOR_ACTIVITY_NONE, 2000U),
                true, false, false);
    assert_leds(update(&indicator, STATUS_INDICATOR_MODE_WIRELESS_HOST,
                       STATUS_INDICATOR_ACTIVITY_NONE, 2001U),
                false, false, true);

    return 0;
}
