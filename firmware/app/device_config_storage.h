#ifndef DEVICE_CONFIG_STORAGE_H
#define DEVICE_CONFIG_STORAGE_H

#include <stdbool.h>

#include "device_config.h"

bool device_config_storage_load(device_config_t *config);
bool device_config_storage_save(const device_config_t *config);
bool device_config_storage_matches(const device_config_t *config);

#endif
