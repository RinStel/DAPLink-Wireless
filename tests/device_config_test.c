#include <assert.h>
#include <string.h>

#include "device_config.h"

bool device_config_storage_load(device_config_t *config)
{
    (void)config;
    return false;
}

int main(void)
{
    device_config_t invalid;
    const device_config_t *config;

    device_config_init();
    config = device_config_get();
    assert(device_config_is_valid(config));
    assert(strcmp(config->sync_code, "DAPLINKWIRELESS1") == 0);

    invalid = *config;
    invalid.sync_code[0] = '-';
    assert(!device_config_is_valid(&invalid));
    invalid = *config;
    invalid.device_mode = (device_mode_t)99;
    assert(!device_config_is_valid(&invalid));

    device_config_button_cycle_rate();
    assert(device_config_get()->rate_mode == DEVICE_RATE_FIXED);
    device_config_button_cycle_mode();
    assert(device_config_get()->device_mode == DEVICE_MODE_WIRELESS_SLAVE);

    assert(device_config_apply("1234567890ABCDEF", DEVICE_MODE_WIRED,
                               DEVICE_RATE_AUTO,
                               SX128X_PROFILE_GFSK_1M));
    assert(device_config_is_valid(device_config_get()));
    assert(!device_config_apply("bad", DEVICE_MODE_WIRED,
                                DEVICE_RATE_AUTO,
                                SX128X_PROFILE_GFSK_1M));
    return 0;
}
