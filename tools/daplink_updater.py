"""Host-side MSC trigger and standard DFU download helper."""

from __future__ import annotations

from dataclasses import dataclass
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile
import time
from typing import Callable, Sequence
import zlib

try:
    from .dwup_format import DwupPackage, ImageEntry, read_dwup
except ImportError:  # pragma: no cover - direct script invocation
    from dwup_format import DwupPackage, ImageEntry, read_dwup

DFU_VID = 0x28E9
DFU_PID = 0x1291
IMAGE_HEADER = __import__("struct").Struct("<IHHIB3xIIIII28s")
CONFIG_FILE = "CONFIG.TXT"
STATUS_FILE = "STATUS.TXT"


@dataclass(frozen=True)
class DfuInfo:
    vid: int
    pid: int
    inactive_slot: str
    load_address: int
    version_code: int
    mode: str


def set_enter_dfu(config_text: str, enabled: bool) -> str:
    """Replace the one-shot field while preserving all other config lines."""
    lines = config_text.replace("\r\n", "\n").replace("\r", "\n").split("\n")
    result: list[str] = []
    inserted = False
    for line in lines:
        if line.startswith("ENTER_DFU="):
            if inserted:
                continue
            result.append(f"ENTER_DFU={'1' if enabled else '0'}")
            inserted = True
        elif line != "" or result:
            result.append(line)
    if not inserted:
        result.append(f"ENTER_DFU={'1' if enabled else '0'}")
    while result and result[-1] == "":
        result.pop()
    return "\r\n".join(result) + "\r\n"


def discover_msc_volume(candidates: Sequence[str | Path] | None = None) -> Path:
    """Find exactly one DAPLink MSC volume by its configuration files.

    The default Windows scan checks drive roots; tests and non-Windows hosts
    can provide explicit roots.  CONFIG.TXT is the required marker and
    STATUS.TXT, when present, further identifies the DAPLink volume.
    """
    if candidates is None:
        if os.name == "nt":
            candidates = [f"{chr(letter)}:\\\\" for letter in range(ord("A"), ord("Z") + 1)]
        else:
            candidates = ["/media", "/run/media", "/mnt"]
    roots = [Path(item) for item in candidates]
    matches = [root for root in roots
               if (root / CONFIG_FILE).is_file() and
               (not (root / STATUS_FILE).exists() or
                (root / STATUS_FILE).is_file())]
    if not matches:
        raise FileNotFoundError("DAPLink MSC CONFIG.TXT volume not found")
    if len(matches) != 1:
        raise RuntimeError("multiple DAPLink MSC volumes found")
    return matches[0]


def write_enter_dfu(config_path: str | Path) -> Path:
    """Write the one-shot action and flush it before returning."""
    path = Path(config_path)
    updated = set_enter_dfu(path.read_text(), True)
    with path.open("w", encoding="ascii", newline="") as stream:
        stream.write(updated)
        stream.flush()
        try:
            os.fsync(stream.fileno())
        except OSError:
            # Some virtual FAT implementations do not expose a sync handle;
            # close() still completes the host-side write transaction.
            pass
    return path


def wait_for_volume_disappear(volume: str | Path, *, timeout: float = 15.0,
                              poll_interval: float = 0.1,
                              clock: Callable[[], float] = time.monotonic,
                              sleeper: Callable[[float], None] = time.sleep) -> None:
    """Wait until CONFIG.TXT is no longer visible after the reset request."""
    config = Path(volume) / CONFIG_FILE
    deadline = clock() + timeout
    while config.exists():
        if clock() >= deadline:
            raise TimeoutError("MSC volume did not disappear after ENTER_DFU")
        sleeper(poll_interval)


def enter_dfu(volume: str | Path | None = None, *, config_path: str | Path | None = None,
              timeout: float = 15.0, poll_interval: float = 0.1,
              clock: Callable[[], float] = time.monotonic,
              sleeper: Callable[[float], None] = time.sleep) -> Path:
    root = Path(volume) if volume is not None else discover_msc_volume()
    path = Path(config_path) if config_path is not None else root / CONFIG_FILE
    write_enter_dfu(path)
    wait_for_volume_disappear(root, timeout=timeout,
                              poll_interval=poll_interval,
                              clock=clock, sleeper=sleeper)
    return root


def parse_dfu_listing(output: str) -> DfuInfo:
    matches: list[DfuInfo] = []
    for line in output.splitlines():
        match = re.search(r"\[([0-9A-Fa-f]{4}):([0-9A-Fa-f]{4})\]", line)
        if not match:
            continue
        vid, pid = int(match.group(1), 16), int(match.group(2), 16)
        if vid != DFU_VID or pid != DFU_PID:
            continue
        metadata = {}
        for key, value in re.findall(r'(?:^|[;"])\s*([a-z_]+)=([^;"\r\n]+)', line):
            metadata[key] = value.strip().strip('"')
        try:
            slot = metadata["inactive"]
            address = int(metadata["addr"], 0)
            version = int(metadata["version"], 0)
            mode = metadata["mode"]
        except (KeyError, ValueError) as exc:
            raise ValueError("DFU listing lacks inactive slot metadata") from exc
        if slot not in ("A", "B"):
            continue
        matches.append(DfuInfo(vid, pid, slot, address, version, mode))
    if not matches:
        raise ValueError("DFU device not found")
    if len(matches) != 1:
        raise ValueError("multiple matching DFU devices found")
    return matches[0]


def poll_dfu_device(*, dfu_util: str = "dfu-util", timeout: float = 30.0,
                    poll_interval: float = 0.25,
                    runner: Callable[..., object] = subprocess.run,
                    clock: Callable[[], float] = time.monotonic,
                    sleeper: Callable[[float], None] = time.sleep) -> DfuInfo:
    """Poll ``dfu-util --list`` until the Bootloader descriptor is present."""
    deadline = clock() + timeout
    command = [dfu_util, "--device", "28e9:1291", "--list"]
    while True:
        result = runner(command, check=False, shell=False,
                        capture_output=True, text=True)
        output = "\n".join(str(getattr(result, name, "") or "")
                             for name in ("stdout", "stderr"))
        try:
            return parse_dfu_listing(output)
        except ValueError:
            if clock() >= deadline:
                raise TimeoutError("DFU device did not re-enumerate")
            sleeper(poll_interval)


def _image_header(info: DfuInfo, image: ImageEntry, mcu_id: int,
                  version_code: int) -> bytes:
    if image.slot != info.inactive_slot or image.load_address != info.load_address:
        raise ValueError("package image does not match device inactive slot")
    if info.mode == "normal" and image.length == 0:
        raise ValueError("empty image")
    body = IMAGE_HEADER.pack(
        0x44574655, 1, IMAGE_HEADER.size, mcu_id,
        ord(image.slot) - ord("A"), image.load_address, image.length,
        version_code, image.crc32, 0, b"\0" * 28
    )
    header_crc = zlib.crc32(body[:32]) & 0xFFFFFFFF
    return IMAGE_HEADER.pack(
        0x44574655, 1, IMAGE_HEADER.size, mcu_id,
        ord(image.slot) - ord("A"), image.load_address, image.length,
        version_code, image.crc32, header_crc, b"\0" * 28
    )


def make_dfu_payload(package: DwupPackage, info: DfuInfo) -> bytes:
    image = package.images[info.inactive_slot]
    if info.mode == "normal" and package.version_code <= info.version_code:
        raise ValueError("package version is not newer than device")
    return _image_header(info, image, package.mcu_id,
                         package.version_code) + image.payload


def run_dfu_download(payload: bytes, *, dfu_util: str = "dfu-util",
                     runner: Callable[..., object] = subprocess.run) -> None:
    with tempfile.TemporaryDirectory(prefix="daplink-dfu-") as temp:
        path = Path(temp) / "image.bin"
        path.write_bytes(payload)
        command = [dfu_util, "--device", "28e9:1291", "--alt", "0",
                   "--download", str(path)]
        result = runner(command, check=False, shell=False)
        if getattr(result, "returncode", 1) != 0:
            raise RuntimeError(f"dfu-util failed with code {result.returncode}")


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Install a DAPLink-Wireless .dwup package")
    parser.add_argument("package", type=Path)
    parser.add_argument(
        "--volume", type=Path,
        help="MSC volume root; omit when the device is already in DFU")
    parser.add_argument("--dfu-util", default="dfu-util")
    args = parser.parse_args(argv)
    package = read_dwup(args.package)
    if args.volume is not None:
        enter_dfu(args.volume)
    info = poll_dfu_device(dfu_util=args.dfu_util)
    run_dfu_download(make_dfu_payload(package, info),
                     dfu_util=args.dfu_util)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
