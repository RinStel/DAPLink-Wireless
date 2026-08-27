import unittest
from pathlib import Path
import subprocess
import sys
import tempfile

from tools.daplink_updater import (
    DfuInfo,
    discover_msc_volume,
    enter_dfu,
    make_dfu_payload,
    parse_dfu_listing,
    poll_dfu_device,
    run_dfu_download,
    set_enter_dfu,
)
from tools.dwup_format import build_package, decode_package


class DaplinkUpdaterTest(unittest.TestCase):
    def test_cli_has_one_update_workflow(self):
        result = subprocess.run(
            [sys.executable, "tools/daplink_updater.py", "--help"],
            cwd=Path(__file__).resolve().parents[1], capture_output=True,
            text=True, check=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("package", result.stdout)
        self.assertIn("--volume", result.stdout)
        self.assertIn("--dfu-util", result.stdout)
        self.assertNotIn("inspect", result.stdout)
        self.assertNotIn("enter-dfu", result.stdout)

    def test_config_and_listing(self):
        self.assertTrue(set_enter_dfu("MODE=WIRED\r\n", True).endswith(
            "ENTER_DFU=1\r\n"))
        info = parse_dfu_listing(
            "Found DFU: [28e9:1291] DAPLink-Wireless;inactive=B;"
            "addr=0x08021000;version=799;mode=normal")
        self.assertEqual(info.inactive_slot, "B")
        self.assertEqual(info.load_address, 0x08021000)

    def test_listing_parses_metadata_inside_name_field(self):
        info = parse_dfu_listing(
            'Found DFU: [28e9:1291] ver=0100, devnum=7, cfg=1, '
            'intf=0, path="1-2.1", alt=0, '
            'name="inactive=B;addr=0x08021000;version=1000;mode=normal", '
            'serial="DFU-1291"')
        self.assertEqual(info, DfuInfo(
            0x28E9, 0x1291, "B", 0x08021000, 1000, "normal"))

    def test_listing_ignores_unrelated_dfu_runtime_interface(self):
        listing = (
            'Found Runtime: [3277:00cc] alt=0, name="APP Mode", '
            'serial="SN0001"\n'
            'Found DFU: [28e9:1291] alt=0, '
            'name="inactive=B;addr=0x08021000;version=1000;mode=recovery", '
            'serial="DFU-1291"\n')
        info = parse_dfu_listing(listing)
        self.assertEqual(info.inactive_slot, "B")
        self.assertEqual(info.mode, "recovery")

    def test_payload_and_version_gate(self):
        package = decode_package(build_package(
            1000, (0x08004000, b"A" * 64), (0x08021000, b"B" * 64)))
        info = DfuInfo(0x28E9, 0x1291, "B", 0x08021000, 999, "normal")
        payload = make_dfu_payload(package, info)
        self.assertEqual(len(payload), 128)
        self.assertEqual(payload[0:4], b"UFWD")
        with self.assertRaises(ValueError):
            make_dfu_payload(package, DfuInfo(
                0x28E9, 0x1291, "B", 0x08021000, 1000, "normal"))

    def test_subprocess_is_unshelled(self):
        calls = []

        class Result:
            returncode = 0

        def runner(args, **kwargs):
            calls.append((args, kwargs))
            return Result()

        run_dfu_download(b"payload", dfu_util="dfu-util", runner=runner)
        self.assertEqual(calls[0][0][0:5],
                         ["dfu-util", "--device", "28e9:1291", "--alt", "0"])
        self.assertFalse(calls[0][1]["shell"])

    def test_volume_trigger_and_reenumeration_poll(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "CONFIG.TXT").write_text("MODE=WIRED\r\n")
            (root / "STATUS.TXT").write_text("OK\r\n")
            self.assertEqual(discover_msc_volume([root]), root)
            ticks = [0.0]
            written = []

            def clock():
                return ticks[0]

            def sleeper(_interval):
                ticks[0] += 1.0
                written.append((root / "CONFIG.TXT").read_bytes())
                (root / "CONFIG.TXT").unlink(missing_ok=True)

            enter_dfu(root, timeout=2.0, poll_interval=0.1,
                      clock=clock, sleeper=sleeper)
            self.assertIn(b"ENTER_DFU=1", written[0])

        outputs = ["no device\n", (
            "Found DFU: [28e9:1291] DAPLink-Wireless;inactive=B;"
            "addr=0x08021000;version=999;mode=normal\n")]
        calls = []

        class Result:
            def __init__(self, text):
                self.returncode = 0
                self.stdout = text
                self.stderr = ""

        def runner(args, **kwargs):
            calls.append((args, kwargs))
            return Result(outputs.pop(0))

        ticks = [0.0]
        info = poll_dfu_device(timeout=3.0, poll_interval=0.1,
                               runner=runner, clock=lambda: ticks[0],
                               sleeper=lambda _value: ticks.__setitem__(0, ticks[0] + 1.0))
        self.assertEqual(info.inactive_slot, "B")
        self.assertEqual(len(calls), 2)
        self.assertEqual(calls[0][0],
                         ["dfu-util", "--device", "28e9:1291", "--list"])
        self.assertFalse(calls[0][1]["shell"])

    def test_rejects_multiple_dfu_devices(self):
        listing = (
            "Found DFU: [28e9:1291] DAPLink-Wireless;inactive=A;"
            "addr=0x08004000;version=1;mode=normal\n"
            "Found DFU: [28e9:1291] DAPLink-Wireless;inactive=B;"
            "addr=0x08021000;version=1;mode=normal\n")
        with self.assertRaises(ValueError):
            parse_dfu_listing(listing)


if __name__ == "__main__":
    unittest.main()
