import os
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts" / "flash_pyocd.ps1"


def powershell_executable():
    candidate = os.environ.get("COMSPEC", "")
    powershell = Path(candidate).with_name("WindowsPowerShell").joinpath(
        "v1.0", "powershell.exe") if candidate else Path(
        r"C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe")
    if powershell.exists():
        return str(powershell)
    return "powershell"


class PyOcdFlashScriptTest(unittest.TestCase):
    def run_script(self, *arguments):
        return subprocess.run(
            [powershell_executable(), "-NoProfile", "-ExecutionPolicy", "Bypass",
             "-File", str(SCRIPT), *arguments],
            cwd=ROOT, capture_output=True, text=True, check=False)

    def test_script_declares_safe_pyocd_options(self):
        script = SCRIPT.read_text(encoding="utf-8")
        for token in ("Artifact", "Target", "Probe", "Frequency", "Connect",
                      "Erase", "WhatIf", "--no-wait", "--erase", "sector"):
            self.assertIn(token, script)
        self.assertIn("daplink_factory", script)
        self.assertIn("daplink_slot_a", script)
        self.assertIn("daplink_slot_b", script)

    def test_what_if_prints_non_destructive_command(self):
        with tempfile.TemporaryDirectory() as temp:
            artifact = Path(temp) / "daplink_factory.hex"
            artifact.write_text(":00000001FF\n", encoding="ascii")
            result = self.run_script(
                "-Artifact", str(artifact), "-Probe", "probe-123", "-WhatIf")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("load", result.stdout)
        self.assertIn("--connect under-reset", result.stdout)
        self.assertIn("--erase sector", result.stdout)
        self.assertIn("--probe probe-123", result.stdout)
        self.assertIn("daplink_factory.hex", result.stdout)

    def test_rejects_artifact_outside_release_contract(self):
        with tempfile.TemporaryDirectory() as temp:
            artifact = Path(temp) / "unexpected.hex"
            artifact.write_text(":00000001FF\n", encoding="ascii")
            result = self.run_script("-Artifact", str(artifact), "-WhatIf")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("supported firmware artifact", result.stderr + result.stdout)


if __name__ == "__main__":
    unittest.main()
