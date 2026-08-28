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


if __name__ == "__main__":
    unittest.main()
