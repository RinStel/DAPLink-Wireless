import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class CMakeStructureTest(unittest.TestCase):
    def test_cmake_presets_define_debug_and_release(self):
        presets = json.loads((ROOT / "CMakePresets.json").read_text())
        names = {item["name"] for item in presets["configurePresets"]}
        self.assertTrue({"debug", "release"}.issubset(names))

    def test_cmake_declares_fixed_address_firmware_targets(self):
        cmake = (ROOT / "CMakeLists.txt").read_text()
        for target in ("daplink_bootloader", "daplink_slot_a", "daplink_slot_b"):
            self.assertIn(f"add_firmware_target({target}", cmake)
        self.assertIn("gd32f303xC_bootloader.ld", cmake)
        self.assertIn("gd32f303xC_slot_a.ld", cmake)
        self.assertIn("gd32f303xC_slot_b.ld", cmake)
        self.assertIn("--gap-fill 0x00", cmake)
        self.assertNotIn("uvprojx", cmake.lower())

    def test_cmake_exposes_non_default_pyocd_flash_targets(self):
        cmake = (ROOT / "CMakeLists.txt").read_text()
        for target in ("flash_factory", "flash_bootloader", "flash_slot_a",
                       "flash_slot_b"):
            self.assertIn(f"add_pyocd_flash_target({target}", cmake)
        self.assertIn("flash_pyocd.ps1", cmake)
        self.assertIn("CMAKE_HOST_WIN32", cmake)
        self.assertIn("USES_TERMINAL", cmake)

    def test_gcc_wrapper_delegates_to_cmake(self):
        script = (ROOT / "scripts" / "build_gcc_targets.ps1").read_text()
        self.assertIn("cmake", script.lower())
        self.assertNotIn("arm-none-eabi-gcc", script)
        self.assertNotIn("Build-Target", script)

    def test_active_tree_has_no_keil_build_references(self):
        roots = [ROOT / name for name in ("README.md", "docs", "scripts", "tools", "tests", "firmware")]
        forbidden = ("STM32_Programmer", "CubeProgrammer", "GD32F30x_HD.FLM",
                     "build_keil", "flash_factory.ps1", ".uvprojx", ".uvoptx")
        offenders = []
        for root in roots:
            paths = [root] if root.is_file() else root.rglob("*")
            for path in paths:
                if (not path.is_file() or path == Path(__file__) or
                        "superpowers" in path.parts):
                    continue
                try:
                    text = path.read_text(encoding="utf-8")
                except UnicodeDecodeError:
                    continue
                if any(token in text for token in forbidden):
                    offenders.append(str(path.relative_to(ROOT)))
        self.assertEqual(offenders, [])


if __name__ == "__main__":
    unittest.main()
