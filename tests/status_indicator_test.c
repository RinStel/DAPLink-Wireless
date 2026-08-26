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
           1U);
}

int main(void)
{
    status_indicator_t indicator;

    status_indicator_init(&indicator, true);
    assert_leds(status_indicator_update(&indicator, false, true, true, 0U),
                true, false, false);
    assert_leds(status_indicator_update(&indicator, false, true, true, 349U),
                true, false, false);
    assert_leds(status_indicator_update(&indicator, false, true, true, 350U),
                false, false, false);
    assert_leds(status_indicator_update(&indicator, false, true, true, 700U),
                true, false, false);

    status_indicator_init(&indicator, false);
    assert_leds(status_indicator_update(&indicator, false, true, true, 0U),
                false, false, true);
    assert_leds(status_indicator_update(&indicator, true, true, true, 10U),
                true, false, false);
    assert_leds(status_indicator_update(&indicator, false, true, true, 20U),
                true, false, false);
    assert_leds(status_indicator_update(&indicator, false, true, true, 1019U),
                true, false, false);
    assert_leds(status_indicator_update(&indicator, false, true, true, 1020U),
                false, false, true);

    status_indicator_init(&indicator, false);
    assert_leds(status_indicator_update(&indicator, true, false, true, 0U),
                true, false, false);
    assert_leds(status_indicator_update(&indicator, false, false, true, 100U),
                true, false, false);
    assert_leds(status_indicator_update(&indicator, true, false, true, 1099U),
                true, false, false);
    assert_leds(status_indicator_update(&indicator, false, false, true, 1100U),
                true, false, false);
    assert_leds(status_indicator_update(&indicator, false, false, true, 2099U),
                true, false, false);
    assert_leds(status_indicator_update(&indicator, false, false, true, 2100U),
                false, true, false);

    return 0;
}
