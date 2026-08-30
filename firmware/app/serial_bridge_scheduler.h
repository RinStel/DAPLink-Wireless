#ifndef SERIAL_BRIDGE_SCHEDULER_H
#define SERIAL_BRIDGE_SCHEDULER_H

#include <stdbool.h>
#include <stdint.h>

#include "radio_protocol.h"

#ifndef SERIAL_BRIDGE_ACK_TURNAROUND_DELAY_US
#define SERIAL_BRIDGE_ACK_TURNAROUND_DELAY_US 0U
#endif

/* 实验性事务模式：关闭 SWD 请求确认后，主机直接等待端到端响应。
 * 默认保持请求 ACK，以便保留现有可靠性行为。主从两端必须使用相同值。 */
#ifndef SERIAL_BRIDGE_SWD_REQUEST_ACK_ENABLE
#define SERIAL_BRIDGE_SWD_REQUEST_ACK_ENABLE 1
#endif

static inline bool serial_bridge_swd_request_ack_enabled(void)
{
    return SERIAL_BRIDGE_SWD_REQUEST_ACK_ENABLE != 0;
}

/* 每次 TX_DONE 后都先恢复 RX。ACK 后的 SWD 响应也必须等待对端重新开启
 * 接收，不能直接连续发送。 */
static inline bool serial_bridge_resume_rx_after_tx(bool tx_was_ack)
{
    (void)tx_was_ack;
    return true;
}

/* SWD 事务期间保持双方的频道和 profile 不变。 */
static inline bool serial_bridge_background_recovery_allowed(bool swd_busy)
{
    return !swd_busy;
}

/* 携带新频道的 ACK 发送完成后，发送方必须与接收方切到同一频道。 */
static inline bool serial_bridge_hop_after_ack(bool requested,
                                                uint8_t current_channel,
                                                uint8_t next_channel)
{
    return requested && (next_channel != current_channel);
}

/* 请求 ACK 与执行响应分阶段确认。Keil/RDDI 会连续运行 Flash Algorithm，
 * 不能把“请求未到达”和“目标 SWD 尚未完成”合并成同一个超时。 */
static inline bool serial_bridge_request_ack_required(radio_frame_type_t type)
{
    (void)type;
    return true;
}

/* 固定 profile 下的 SWD 响应 ACK 不携带自适应/跳频指标，可以使用紧凑布局。 */
static inline bool serial_bridge_ack_compact(radio_frame_type_t type,
                                              bool auto_rate)
{
    (void)type;
    return !auto_rate;
}

static inline bool serial_bridge_packet_status_required(bool auto_rate)
{
    return auto_rate;
}

static inline uint32_t serial_bridge_ack_turnaround_delay_us(void)
{
    return SERIAL_BRIDGE_ACK_TURNAROUND_DELAY_US;
}

/* SWD 请求 ACK 只确认无线帧到达，不包含目标执行时间。 */
static inline uint32_t serial_bridge_reliable_ack_wait_ms(
    radio_frame_type_t type, uint32_t device_hash, uint8_t retries)
{
    bool swd_request = (type == RADIO_FRAME_SWD_COMMAND) ||
                       (type == RADIO_FRAME_SWD_BLOCK) ||
                       (type == RADIO_FRAME_SWD_BURST);
    uint32_t jitter_range = swd_request ? 8U : 41U;
    uint32_t base_wait_ms = swd_request ? 12U : 120U;

    return base_wait_ms +
           ((device_hash + (uint32_t)retries * 17U) % jitter_range);
}

/* 下一条 SWD 请求证明上一条响应已被主机接收。 */
static inline bool serial_bridge_next_swd_request_confirms_response(
    radio_frame_type_t pending_type, radio_frame_type_t incoming_type)
{
    bool pending_is_response =
        (pending_type == RADIO_FRAME_SWD_COMMAND_RESPONSE) ||
        (pending_type == RADIO_FRAME_SWD_BLOCK_RESPONSE) ||
        (pending_type == RADIO_FRAME_SWD_BURST_RESPONSE);
    bool incoming_is_request =
        (incoming_type == RADIO_FRAME_SWD_COMMAND) ||
        (incoming_type == RADIO_FRAME_SWD_BLOCK) ||
        (incoming_type == RADIO_FRAME_SWD_BURST);
    return pending_is_response && incoming_is_request;
}

/* 周期跳频只统计累计 ACK 实际释放的 DATA 槽位。 */
static inline bool serial_bridge_periodic_hop_progress(
    uint8_t active_before, uint8_t active_after)
{
    return active_after < active_before;
}

/* 没有待处理事务时不执行自由扫描。双方回到相同的 rendezvous 频道并停留，
 * 避免各自时基偏差导致下一次事务先经历超时恢复。 */
static inline uint8_t serial_bridge_idle_recovery_channel(
    uint8_t rendezvous_channel, uint8_t scan_candidate)
{
    (void)scan_candidate;
    return rendezvous_channel;
}

#endif
