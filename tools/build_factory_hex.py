"""Merge Bootloader, Slot A and optional state records into Intel HEX."""

from __future__ import annotations

import argparse
from pathlib import Path
import struct
import zlib

FLASH_END = 0x08040000
BOOT_STATE_BASE = 0x0803E000
CONFIG_BASE = 0x0803F000
BOOT_STATE_MAGIC = 0x42535441
BOOT_STATE_VERSION = 1
BOOT_STATE_COMMIT = 0x434F4D54
BOOT_STATE_RECORD = struct.Struct("<IHHI4BIIIIIIIII12s")


def build_initial_boot_state_record(image: bytes, version_code: int,
                                    *, slot: int = 0,
                                    generation: int = 1) -> bytes:
    """Return the 64-byte confirmed Slot A journal record used by factory HEX.

    The record layout and CRC normalization mirror firmware/update/boot_state.c:
    the attempt bitmap, CRC field, and commit marker are normalized before one
    continuous CRC is calculated over all 64 bytes.
    """
    if slot not in (0, 1) or not image:
        raise ValueError("factory state requires a non-empty A/B image")
    if not 0 <= version_code <= 0xFFFFFFFF:
        raise ValueError("version code out of range")
    pending_crc = zlib.crc32(image) & 0xFFFFFFFF
    values = (
        BOOT_STATE_MAGIC, BOOT_STATE_VERSION, BOOT_STATE_RECORD.size,
        generation, slot, slot, 0, 0, version_code, version_code,
        len(image), pending_crc, 0xFFFFFFFF, 0, 0, len(image), pending_crc,
        b"\xFF" * 12,
    )
    normalized = bytearray(BOOT_STATE_RECORD.pack(*values))
    record_crc = zlib.crc32(normalized) & 0xFFFFFFFF
    values = values[:13] + (record_crc, BOOT_STATE_COMMIT) + values[15:]
    return BOOT_STATE_RECORD.pack(*values)


def _parse_hex(text: str) -> dict[int, int]:
    memory: dict[int, int] = {}
    upper = 0
    eof = False
    for line_number, line in enumerate(text.splitlines(), 1):
        if not line.startswith(":"):
            raise ValueError(f"line {line_number}: missing ':'")
        try:
            raw = bytes.fromhex(line[1:])
        except ValueError as exc:
            raise ValueError(f"line {line_number}: invalid hex") from exc
        if len(raw) < 5 or sum(raw) & 0xFF:
            raise ValueError(f"line {line_number}: checksum or length error")
        length, offset, record_type = raw[0], int.from_bytes(raw[1:3], "big"), raw[3]
        payload = raw[4:-1]
        if len(payload) != length:
            raise ValueError(f"line {line_number}: byte count mismatch")
        if record_type == 0x00:
            address = (upper << 16) + offset
            for index, value in enumerate(payload):
                absolute = address + index
                if absolute >= FLASH_END:
                    raise ValueError("HEX data exceeds flash")
                if CONFIG_BASE <= absolute < FLASH_END:
                    raise ValueError("factory HEX may not contain configuration page")
                previous = memory.get(absolute)
                if previous is not None and previous != value:
                    raise ValueError("overlapping HEX data differs")
                if previous is not None:
                    raise ValueError("overlapping HEX data")
                memory[absolute] = value
        elif record_type == 0x01:
            eof = True
        elif record_type == 0x04 and length == 2:
            upper = int.from_bytes(payload, "big")
        elif record_type in (0x02, 0x03, 0x05):
            continue
        else:
            raise ValueError(f"unsupported HEX record type: {record_type}")
    if not eof:
        raise ValueError("HEX missing EOF record")
    return memory


def read_hex(path: str | Path) -> dict[int, int]:
    return _parse_hex(Path(path).read_text())


def _record(record_type: int, address: int, payload: bytes) -> str:
    body = bytes([len(payload)]) + address.to_bytes(2, "big") + bytes([record_type]) + payload
    checksum = (-sum(body)) & 0xFF
    return ":" + (body + bytes([checksum])).hex().upper()


def merge_hex(inputs: list[str | Path], output: str | Path,
              *, state_image: str | Path | None = None,
              state_version_code: int | None = None) -> None:
    memory: dict[int, int] = {}
    for path in inputs:
        for address, value in read_hex(path).items():
            if address in memory:
                raise ValueError(f"overlapping HEX address 0x{address:08X}")
            memory[address] = value
    if state_image is not None:
        if state_version_code is None:
            raise ValueError("state_version_code is required with state_image")
        record = build_initial_boot_state_record(
            Path(state_image).read_bytes(), state_version_code)
        for index, value in enumerate(record):
            address = BOOT_STATE_BASE + index
            if address in memory:
                raise ValueError("overlapping factory boot state")
            memory[address] = value
    lines: list[str] = []
    current_upper: int | None = None
    addresses = sorted(memory)
    index = 0
    while index < len(addresses):
        address = addresses[index]
        upper = address >> 16
        if upper != current_upper:
            lines.append(_record(0x04, 0, upper.to_bytes(2, "big")))
            current_upper = upper
        chunk = bytearray([memory[address]])
        index += 1
        while index < len(addresses) and addresses[index] == address + len(chunk) and len(chunk) < 16 and addresses[index] >> 16 == upper:
            chunk.append(memory[addresses[index]])
            index += 1
        lines.append(_record(0x00, address & 0xFFFF, bytes(chunk)))
    lines.append(_record(0x01, 0, b""))
    Path(output).write_text("\n".join(lines) + "\n", newline="")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--state-image", type=Path)
    parser.add_argument("--state-version-code", type=int)
    parser.add_argument("inputs", nargs="+", type=Path)
    args = parser.parse_args()
    merge_hex(args.inputs, args.output,
              state_image=args.state_image,
              state_version_code=args.state_version_code)
    print(f"Built: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
