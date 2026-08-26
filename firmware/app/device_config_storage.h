/*
 * DAPLink-Wireless — Wireless CMSIS-DAP v2 debug probe firmware
 * Copyright (C) 2025 RinStel <me@rinx.nz>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#ifndef DEVICE_CONFIG_STORAGE_H
#define DEVICE_CONFIG_STORAGE_H

#include <stdbool.h>

#include "device_config.h"

/* 从双页日志读取最新的有效记录。 */
bool device_config_storage_load(device_config_t *config);
/* 原子保存；复位或掉电留下的不完整记录会被忽略。 */
bool device_config_storage_save(const device_config_t *config);
/* 比较当前配置与最新的持久化记录。 */
bool device_config_storage_matches(const device_config_t *config);

#endif
