#include <assert.h>
#include <stdbool.h>

#include "serial_bridge_scheduler.h"

int main(void)
{
    /* 可靠请求和 ACK 发送完成后都必须先恢复 RX。尤其不能在 ACK 后直接
     * 回传 SWD 响应，否则对端可能尚未处理 RX_DONE 并重新开启接收。 */
    assert(serial_bridge_resume_rx_after_tx(false));
    assert(serial_bridge_resume_rx_after_tx(true));

    /* 本地或远端 SWD 事务未结束时，不得执行主动扫频或 profile 恢复。 */
    assert(serial_bridge_background_recovery_allowed(false));
    assert(!serial_bridge_background_recovery_allowed(true));

    /* 携带新频道的 ACK 只有在频道实际变化时才安排 TX_DONE 后切换。 */
    assert(!serial_bridge_hop_after_ack(false, 3U, 4U));
    assert(!serial_bridge_hop_after_ack(true, 3U, 3U));
    assert(serial_bridge_hop_after_ack(true, 3U, 4U));

    /* 固定 profile 使用紧凑 ACK，并跳过不参与速率决策的 packet status。 */
    assert(serial_bridge_request_ack_required(RADIO_FRAME_SWD_COMMAND));
    assert(serial_bridge_request_ack_required(RADIO_FRAME_SWD_BLOCK));
    assert(serial_bridge_request_ack_required(RADIO_FRAME_DATA));
    assert(serial_bridge_ack_compact(
        RADIO_FRAME_SWD_BLOCK_RESPONSE, false));
    assert(!serial_bridge_ack_compact(
        RADIO_FRAME_SWD_BLOCK_RESPONSE, true));
    assert(serial_bridge_ack_compact(RADIO_FRAME_DATA, false));
    assert(serial_bridge_packet_status_required(true));
    assert(!serial_bridge_packet_status_required(false));
    assert(serial_bridge_ack_turnaround_delay_us() == 200U);

    /* 下一条 SWD 请求隐式确认上一条响应，避免显式 ACK 丢失阻塞可靠槽。 */
    assert(serial_bridge_next_swd_request_confirms_response(
        RADIO_FRAME_SWD_BLOCK_RESPONSE, RADIO_FRAME_SWD_BLOCK));
    assert(serial_bridge_next_swd_request_confirms_response(
        RADIO_FRAME_SWD_COMMAND_RESPONSE, RADIO_FRAME_SWD_COMMAND));
    assert(!serial_bridge_next_swd_request_confirms_response(
        RADIO_FRAME_SWD_BLOCK, RADIO_FRAME_SWD_BLOCK));
    assert(!serial_bridge_next_swd_request_confirms_response(
        RADIO_FRAME_SWD_BLOCK_RESPONSE, RADIO_FRAME_DATA));

    /* SWD 请求丢包必须在几个正常 FLRC 往返周期内快速重试。 */
    assert(serial_bridge_reliable_ack_wait_ms(
               RADIO_FRAME_SWD_COMMAND, 0x1234U, 0U) >= 12U);
    assert(serial_bridge_reliable_ack_wait_ms(
               RADIO_FRAME_SWD_COMMAND, 0x1234U, 0U) <= 19U);
    assert(serial_bridge_reliable_ack_wait_ms(
               RADIO_FRAME_SWD_BLOCK, 0x1234U, 3U) <= 19U);
    assert(serial_bridge_reliable_ack_wait_ms(
               RADIO_FRAME_PROFILE_SWITCH, 0x1234U, 0U) >= 120U);
    assert(serial_bridge_reliable_ack_wait_ms(
               RADIO_FRAME_PROFILE_SWITCH, 0x1234U, 0U) <= 160U);

    /* 周期跳频只由累计 ACK 实际释放 DATA 槽位推进。SWD ACK 没有 DATA
     * 进展，不得触发固定周期的换频。 */
    assert(!serial_bridge_periodic_hop_progress(0U, 0U));
    assert(!serial_bridge_periodic_hop_progress(2U, 2U));
    assert(serial_bridge_periodic_hop_progress(2U, 1U));

    /* 健康链路空闲后双方都回到并停留在 rendezvous 频道。不得在没有待处理
     * 事务时按各自时基扫描不同候选频道。 */
    assert(serial_bridge_idle_recovery_channel(3U, 9U) == 3U);
    return 0;
}
