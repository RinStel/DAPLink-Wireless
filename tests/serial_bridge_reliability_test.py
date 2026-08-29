from pathlib import Path
import unittest


class SerialBridgeReliabilityTest(unittest.TestCase):
    def test_swd_command_ack_does_not_finish_until_response(self):
        source = (Path(__file__).resolve().parents[1] /
                  "firmware/app/serial_bridge.c").read_text(encoding="utf-8")
        start = source.index("if (s_pending && (sequence == s_pending_sequence))")
        end = source.index("        return;", start)
        ack_handler = source[start:end]
        self.assertIn("pending_type == BRIDGE_FRAME_SWD_COMMAND", ack_handler)
        self.assertIn("pending_type == RADIO_FRAME_SWD_BLOCK", ack_handler)
        self.assertIn("s_pending = false", ack_handler)
        self.assertIn("if (!swd_pending_response)", ack_handler)

    def test_swd_ack_starts_short_response_retry_window(self):
        source = (Path(__file__).resolve().parents[1] /
                  "firmware/app/serial_bridge.c").read_text(encoding="utf-8")
        start = source.index("if (s_pending && (sequence == s_pending_sequence))")
        end = source.index("        return;", start)
        ack_handler = source[start:end]
        self.assertIn("BRIDGE_SWD_RESPONSE_TIMEOUT_MS", source)
        self.assertIn("s_deadline = board_millis() +", ack_handler)
        self.assertIn("BRIDGE_SWD_RESPONSE_TIMEOUT_MS", ack_handler)

    def test_duplicate_swd_block_reuses_cached_response(self):
        source = (Path(__file__).resolve().parents[1] /
                  "firmware/app/serial_bridge.c").read_text(encoding="utf-8")
        start = source.index(
            "} else if (((type == BRIDGE_FRAME_SWD_COMMAND)")
        end = source.index("    valid_rx_mark();", start)
        duplicate_handler = source[start:end]
        self.assertIn("type == RADIO_FRAME_SWD_BLOCK", duplicate_handler)
        self.assertIn("swd_bridge_service_repeat_request();",
                      duplicate_handler)

    def test_compact_ack_is_rejected_in_auto_profile(self):
        source = (Path(__file__).resolve().parents[1] /
                  "firmware/app/serial_bridge.c").read_text(encoding="utf-8")
        start = source.index("if (type == BRIDGE_FRAME_ACK)")
        end = source.index("    if (!remote_session_accept", start)
        ack_parser = source[start:end]
        self.assertIn("RADIO_PROTOCOL_ACK_COMPACT_PAYLOAD_SIZE", ack_parser)
        self.assertIn("DEVICE_RATE_AUTO", ack_parser)

    def test_next_swd_request_releases_previous_response_before_dispatch(self):
        source = (Path(__file__).resolve().parents[1] /
                  "firmware/app/serial_bridge.c").read_text(encoding="utf-8")
        dispatch = source.index("duplicate = radio_protocol_key_equal")
        prefix = source[source.index("if (!remote_session_accept", 0,
                                     dispatch):dispatch]
        self.assertIn(
            "serial_bridge_next_swd_request_confirms_response", prefix)
        self.assertIn("s_pending = false", prefix)
        self.assertIn("s_waiting_ack = false", prefix)

    def test_swd_request_ack_uses_short_retry_window(self):
        source = (Path(__file__).resolve().parents[1] /
                  "firmware/app/serial_bridge.c").read_text(encoding="utf-8")
        start = source.index("if ((completed == TX_RELIABLE) && s_pending)")
        end = source.index("        return;", start)
        tx_done = source[start:end]
        self.assertIn("serial_bridge_reliable_ack_wait_ms", tx_done)
        self.assertNotIn("BRIDGE_SWD_ACK_TIMEOUT_MS", source)


if __name__ == "__main__":
    unittest.main()
