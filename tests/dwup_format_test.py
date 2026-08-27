import unittest

from tools.dwup_format import build_package, decode_package


class DwupFormatTest(unittest.TestCase):
    def test_round_trip(self):
        package = build_package(800, (0x08004000, b"A" * 64),
                                (0x08021000, b"B" * 64), source_id="commit")
        decoded = decode_package(package)
        self.assertEqual(decoded.version_code, 800)
        self.assertEqual(decoded.images["A"].load_address, 0x08004000)
        self.assertEqual(decoded.images["B"].payload, b"B" * 64)

    def test_rejects_corruption_and_trailing_data(self):
        package = build_package(800, (0x08004000, b"A" * 64),
                                (0x08021000, b"B" * 64))
        broken = bytearray(package)
        broken[-1] ^= 1
        with self.assertRaises(ValueError):
            decode_package(bytes(broken))
        with self.assertRaises(ValueError):
            decode_package(package + b"tail")

    def test_rejects_address_and_duplicate_slot(self):
        with self.assertRaises(ValueError):
            build_package(800, (0x08000000, b"A"),
                          (0x08021000, b"B"))
        package = bytearray(build_package(800, (0x08004000, b"A"),
                                           (0x08021000, b"B")))
        package[24 + 48] = ord("A")
        package[0:24] = build_package(800, (0x08004000, b"A"),
                                       (0x08021000, b"B"))[0:24]
        with self.assertRaises(ValueError):
            decode_package(bytes(package))


if __name__ == "__main__":
    unittest.main()
