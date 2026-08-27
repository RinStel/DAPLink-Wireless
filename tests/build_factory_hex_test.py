import tempfile
import unittest
from pathlib import Path
import struct
import zlib

from tools.build_factory_hex import (
    BOOT_STATE_BASE,
    BOOT_STATE_COMMIT,
    BOOT_STATE_RECORD,
    build_initial_boot_state_record,
    merge_hex,
    read_hex,
)


def ihex(address, payload):
    body = bytes([len(payload)]) + address.to_bytes(2, "big") + b"\0" + payload
    return ":" + (body + bytes([(-sum(body)) & 0xFF])).hex().upper()


class FactoryHexTest(unittest.TestCase):
    def test_merge_and_reject_overlap(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            a = root / "a.hex"
            b = root / "b.hex"
            out = root / "factory.hex"
            a.write_text(ihex(0x0000, b"AB") + "\n:00000001FF\n")
            b.write_text(ihex(0x0010, b"CD") + "\n:00000001FF\n")
            merge_hex([a, b], out)
            self.assertEqual(read_hex(out)[0x10], ord("C"))
            b.write_text(ihex(0x0001, b"Z") + "\n:00000001FF\n")
            with self.assertRaises(ValueError):
                merge_hex([a, b], out)

    def test_rejects_bad_checksum_and_config(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            bad = root / "bad.hex"
            bad.write_text(":020000040800F1\n:01000000AAF\n:00000001FF\n")
            with self.assertRaises(ValueError):
                read_hex(bad)
            config = root / "config.hex"
            config.write_text(":020000040803EF\n" + ihex(0xF000, b"X") +
                              "\n:00000001FF\n")
            with self.assertRaises(ValueError):
                read_hex(config)

    def test_factory_state_record_matches_firmware_layout(self):
        image = b"factory-image"
        record = build_initial_boot_state_record(image, 800)
        self.assertEqual(len(record), 64)
        fields = BOOT_STATE_RECORD.unpack(record)
        self.assertEqual(fields[14], BOOT_STATE_COMMIT)
        self.assertEqual(fields[10], len(image))
        self.assertEqual(fields[11], zlib.crc32(image) & 0xFFFFFFFF)
        normalized = bytearray(record)
        normalized[36:40] = b"\0" * 4
        normalized[40:44] = b"\0" * 4
        normalized[32:36] = b"\xFF" * 4
        self.assertEqual(zlib.crc32(normalized) & 0xFFFFFFFF, fields[13])

        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            a = root / "a.hex"
            out = root / "factory.hex"
            a.write_text(ihex(0x0000, b"AB") + "\n:00000001FF\n")
            image_path = root / "slot.bin"
            image_path.write_bytes(image)
            merge_hex([a], out, state_image=image_path,
                      state_version_code=800)
            merged = read_hex(out)
            self.assertEqual(bytes(merged[BOOT_STATE_BASE + i]
                                   for i in range(64)), record)


if __name__ == "__main__":
    unittest.main()
