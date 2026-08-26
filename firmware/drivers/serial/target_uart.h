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
#ifndef TARGET_UART_H
#define TARGET_UART_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    TARGET_UART_PARITY_NONE = 0,
    TARGET_UART_PARITY_ODD,
    TARGET_UART_PARITY_EVEN
} target_uart_parity_t;

typedef struct {
    uint32_t baud_rate; /* bit/s */
    uint8_t data_bits;
    uint8_t stop_bits;
    target_uart_parity_t parity;
} target_uart_config_t;

/* 无 DMA 依赖的环形缓冲原语。硬件 ISR 可以用 push，主循环用 read；
 * dma_publish 用于把外设的生产位置一次性提交到环中。 */
typedef struct {
    uint8_t *storage;
    uint16_t capacity;
    volatile uint16_t read;
    volatile uint16_t write;
    volatile uint32_t overruns;
} target_uart_ring_t;

void target_uart_ring_init(target_uart_ring_t *ring, uint8_t *storage,
                           uint16_t capacity);
bool target_uart_ring_push(target_uart_ring_t *ring, uint8_t value);
uint16_t target_uart_ring_dma_publish(target_uart_ring_t *ring,
                                      uint16_t producer);
size_t target_uart_ring_read(target_uart_ring_t *ring, uint8_t *data,
                             size_t capacity);
size_t target_uart_ring_write(target_uart_ring_t *ring,
                              const uint8_t *data, size_t length);
size_t target_uart_ring_consume(target_uart_ring_t *ring, size_t length);
size_t target_uart_ring_free(const target_uart_ring_t *ring);
uint32_t target_uart_ring_overruns(const target_uart_ring_t *ring);

/* process 保留服务层调度接口；DMA 和中断已完成字节搬运，不要求轮询外设。 */
bool target_uart_init(const target_uart_config_t *config);
bool target_uart_configure(const target_uart_config_t *config);
void target_uart_process(void);
/* read/write 复制字节；短写表示 TX 环已满。 */
size_t target_uart_read(uint8_t *data, size_t capacity);
size_t target_uart_write(const uint8_t *data, size_t length);
size_t target_uart_tx_free(void);
const target_uart_config_t *target_uart_config(void);
uint32_t target_uart_rx_overruns(void);

#endif
