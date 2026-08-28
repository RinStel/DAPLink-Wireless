from pathlib import Path
import unittest


class SerialBridgeTurnaroundTest(unittest.TestCase):
    def test_tx_waits_before_radio_start(self):
        source = (Path(__file__).resolve().parents[1] /
                  "firmware/app/serial_bridge.c").read_text(encoding="utf-8")
        start = source.index("static bool frame_transmit")
        end = source.index("static bool data_frame_transmit", start)
        function = source[start:end]
        self.assertIn("BRIDGE_TX_TURNAROUND_DELAY_US 200U", source)
        self.assertIn("kind == TX_ACK", function)
        self.assertLess(function.index("board_delay_us"),
                        function.index("sx128x_start_tx"))

    def test_turnaround_guard_is_not_millisecond_scale(self):
        """ACK 转向保护必须是微秒级：毫秒级阻塞会吃掉烧录吞吐。"""
        source = (Path(__file__).resolve().parents[1] /
                  "firmware/app/serial_bridge.c").read_text(encoding="utf-8")
        start = source.index("static bool frame_transmit")
        end = source.index("static bool data_frame_transmit", start)
        self.assertNotIn("board_delay_ms", source[start:end])


if __name__ == "__main__":
    unittest.main()
