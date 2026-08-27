"""Fixed, little-endian DAPLink Wireless update package format."""

from __future__ import annotations

from dataclasses import dataclass
import struct
from pathlib import Path
from typing import Mapping
import zlib

MAGIC = b"DWUP"
FORMAT_VERSION = 1
MCU_ID = 0x303CC
SLOT_ADDRESSES = {"A": 0x08004000, "B": 0x08021000}
PACKAGE_HEADER = struct.Struct("<4sHHIIII")
IMAGE_ENTRY = struct.Struct("<B3xIIII32s")
HEADER_SIZE = PACKAGE_HEADER.size
ENTRY_SIZE = IMAGE_ENTRY.size


@dataclass(frozen=True)
class ImageEntry:
    slot: str
    load_address: int
    payload: bytes
    crc32: int
    source_id: bytes
    offset: int = 0

    @property
    def length(self) -> int:
        return len(self.payload)


@dataclass(frozen=True)
class DwupPackage:
    version_code: int
    images: Mapping[str, ImageEntry]
    mcu_id: int = MCU_ID
    format_version: int = FORMAT_VERSION


def _crc(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def _source_bytes(source_id: str | bytes) -> bytes:
    if isinstance(source_id, str):
        source_id = source_id.encode("ascii")
    if len(source_id) > 32:
        raise ValueError("source_id must be at most 32 bytes")
    return source_id.ljust(32, b"\0")


def build_package(
    version_code: int,
    slot_a: tuple[int, bytes],
    slot_b: tuple[int, bytes],
    *,
    source_id: str | bytes = b"",
    mcu_id: int = MCU_ID,
) -> bytes:
    """Build a package containing exactly one A and one B image."""
    if not 0 <= version_code <= 0xFFFFFFFF:
        raise ValueError("version_code out of range")
    source = _source_bytes(source_id)
    items = [("A", slot_a), ("B", slot_b)]
    entries: list[ImageEntry] = []
    table_size = HEADER_SIZE + 2 * ENTRY_SIZE
    offset = table_size
    for slot, (address, payload) in items:
        if not isinstance(payload, bytes) or not payload:
            raise ValueError(f"{slot} payload must be non-empty bytes")
        if address != SLOT_ADDRESSES[slot]:
            raise ValueError(f"{slot} load address mismatch")
        entries.append(ImageEntry(slot, address, payload, _crc(payload), source, offset))
        offset += len(payload)
    entry_bytes = b"".join(
        IMAGE_ENTRY.pack(ord(item.slot), item.load_address, item.offset,
                         item.length, item.crc32, item.source_id)
        for item in entries
    )
    body = b"".join(item.payload for item in entries)
    header_without_crc = PACKAGE_HEADER.pack(
        MAGIC, FORMAT_VERSION, table_size, mcu_id, version_code,
        len(entries), 0
    )
    package_crc = _crc(header_without_crc + entry_bytes + body)
    header = PACKAGE_HEADER.pack(
        MAGIC, FORMAT_VERSION, table_size, mcu_id, version_code,
        len(entries), package_crc
    )
    return header + entry_bytes + body


def decode_package(data: bytes) -> DwupPackage:
    if len(data) < HEADER_SIZE:
        raise ValueError("truncated DWUP header")
    magic, fmt, header_size, mcu_id, version, count, package_crc = PACKAGE_HEADER.unpack_from(data)
    if magic != MAGIC or fmt != FORMAT_VERSION:
        raise ValueError("unknown DWUP format")
    if mcu_id != MCU_ID:
        raise ValueError("MCU identifier mismatch")
    if count != 2 or header_size != HEADER_SIZE + count * ENTRY_SIZE:
        raise ValueError("invalid DWUP entry table")
    if len(data) < header_size:
        raise ValueError("truncated DWUP entry table")
    crc_header = PACKAGE_HEADER.pack(magic, fmt, header_size, mcu_id, version, count, 0)
    if _crc(crc_header + data[HEADER_SIZE:]) != package_crc:
        raise ValueError("DWUP package CRC mismatch")
    entries: dict[str, ImageEntry] = {}
    expected_offset = header_size
    source_id: bytes | None = None
    for index in range(count):
        raw = IMAGE_ENTRY.unpack_from(data, HEADER_SIZE + index * ENTRY_SIZE)
        slot = chr(raw[0])
        address, offset, length, image_crc, source = raw[1:]
        if slot not in SLOT_ADDRESSES or slot in entries:
            raise ValueError("duplicate or unknown DWUP slot")
        if address != SLOT_ADDRESSES[slot] or offset != expected_offset:
            raise ValueError("DWUP image address or offset mismatch")
        end = offset + length
        if length == 0 or end < offset or end > len(data):
            raise ValueError("DWUP image range is invalid")
        payload = data[offset:end]
        if _crc(payload) != image_crc:
            raise ValueError("DWUP image CRC mismatch")
        if source_id is None:
            source_id = source
        elif source != source_id:
            raise ValueError("A/B source identifiers differ")
        entries[slot] = ImageEntry(slot, address, payload, image_crc, source, offset)
        expected_offset = end
    if expected_offset != len(data):
        raise ValueError("trailing DWUP data")
    if set(entries) != {"A", "B"}:
        raise ValueError("DWUP must contain A and B images")
    return DwupPackage(version, entries, mcu_id, fmt)


def write_dwup(path: str | Path, package: bytes | DwupPackage) -> None:
    if isinstance(package, DwupPackage):
        a = package.images["A"]
        b = package.images["B"]
        package = build_package(package.version_code, (a.load_address, a.payload),
                                (b.load_address, b.payload),
                                source_id=a.source_id.rstrip(b"\0"),
                                mcu_id=package.mcu_id)
    decode_package(package)
    Path(path).write_bytes(package)


def read_dwup(path: str | Path) -> DwupPackage:
    return decode_package(Path(path).read_bytes())
