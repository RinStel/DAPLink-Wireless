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
#ifndef CMSIS_DAP_H
#define CMSIS_DAP_H

#include <stdbool.h>
#include <stdint.h>

#define CMSIS_DAP_PACKET_SIZE 64U
/* DAP_Info(0xFE) 对主机公布的请求/响应流水线深度。 */
#define CMSIS_DAP_PACKET_COUNT 8U

/* submit 会复制包；process 推进异步桥接操作。 */
void cmsis_dap_init(void);
bool cmsis_dap_submit(const uint8_t *request, uint8_t length);
uint8_t cmsis_dap_response_pending_count(void);
/* Abort 为带外命令，不生成 CMSIS-DAP 响应包。 */
void cmsis_dap_abort(void);
void cmsis_dap_process(void);
bool cmsis_dap_busy(void);
/* take 复制一个已完成响应，并清除核心 ready 状态。 */
bool cmsis_dap_response_take(uint8_t *response, uint8_t *length);

#endif
