"""Build a .dwup package from the two linked slot binaries."""

from __future__ import annotations

import argparse
from pathlib import Path
import re

try:
    from .dwup_format import build_package, write_dwup
except ImportError:  # pragma: no cover - direct script invocation
    from dwup_format import build_package, write_dwup


def _version_from_header(path: Path) -> int:
    match = re.search(r"FIRMWARE_VERSION_CODE\s+(\d+)U", path.read_text())
    if not match:
        raise ValueError("FIRMWARE_VERSION_CODE not found")
    return int(match.group(1))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--slot-a", required=True, type=Path)
    parser.add_argument("--slot-b", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--version-code", type=int)
    parser.add_argument("--source-id", default="")
    parser.add_argument("--version-header", type=Path)
    args = parser.parse_args()
    version = args.version_code
    if version is None:
        if args.version_header is None:
            raise SystemExit("--version-code or --version-header is required")
        version = _version_from_header(args.version_header)
    package = build_package(
        version,
        (0x08004000, args.slot_a.read_bytes()),
        (0x08021000, args.slot_b.read_bytes()),
        source_id=args.source_id,
    )
    write_dwup(args.output, package)
    print(f"Built: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
