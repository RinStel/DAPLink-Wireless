#!/usr/bin/env python3
"""Read opt-in DAPLink-Wireless throughput diagnostics over CMSIS-DAP v2."""

import argparse
import json
import struct

import usb.core
import usb.util

VID = 0x28E9
PID = 0x1290
EP_OUT = 0x05
EP_IN = 0x85
COMMAND = 0x81


def open_probe(serial):
    matches = []
    for device in usb.core.find(find_all=True, idVendor=VID, idProduct=PID):
        value = usb.util.get_string(device, device.iSerialNumber)
        if serial is None or value == serial:
            matches.append(device)
    if len(matches) != 1:
        raise SystemExit(f"expected one matching probe, found {len(matches)}")
    device = matches[0]
    device.set_configuration()
    return device


def exchange(device, payload):
    device.write(EP_OUT, payload, timeout=2000)
    return bytes(device.read(EP_IN, 64, timeout=2000))


def reset(device):
    response = exchange(device, bytes((COMMAND, 0x00)))
    if response[:3] != bytes((COMMAND, 0x01, 0x00)):
        raise SystemExit(f"diagnostics reset failed: {response.hex()}")


def read_page(device, page):
    response = exchange(device, bytes((COMMAND, 0x01, page)))
    if len(response) != 64 or response[:4] != bytes((COMMAND, 0x01, page, 60)):
        raise SystemExit(f"diagnostics page {page} failed: {response.hex()}")
    return struct.unpack_from("<15I", response, 4)


def dump(device):
    p0, p1, p2 = (read_page(device, page) for page in range(3))
    cycles_per_us = p0[1]
    to_us = lambda cycles: cycles / cycles_per_us if cycles_per_us else None
    result = {
        "version": p0[0], "session_us": to_us(p0[2]),
        "usb_out_count": p0[3], "transfer_count": p0[4],
        "transfer_block_count": p0[5], "queue_count": p0[6],
        "execute_count": p0[7], "transfer_items": p0[8],
        "transfer_histogram": dict(zip(
            ("1", "2", "3-4", "5-8", "9-12", "13-16"), p0[9:15])),
        "rf_tx_frames": p1[0], "rf_tx_bytes": p1[1],
        "rf_retransmits": p1[2],
        "ack_wait_us_sum": to_us(p1[3]), "ack_wait_count": p1[4],
        "ack_wait_us_max": to_us(p1[5]),
        "remote_roundtrip_us_sum": to_us(p1[6]),
        "remote_roundtrip_count": p1[7],
        "remote_roundtrip_us_max": to_us(p1[8]),
        "response_to_usb_in_us_sum": to_us(p1[9]),
        "response_to_usb_in_count": p1[10],
        "response_to_usb_in_us_max": to_us(p1[11]),
        "rx_restore_us_sum": to_us(p2[0]), "rx_restore_count": p2[1],
        "rx_restore_us_max": to_us(p2[2]),
    }
    result["avg_transfer_items"] = (
        p0[8] / (p0[4] + p0[5]) if p0[4] + p0[5] else None)
    result["rf_retransmit_rate"] = p1[2] / p1[0] if p1[0] else None
    for prefix, total, count in (
        ("ack_wait", p1[3], p1[4]), ("remote_roundtrip", p1[6], p1[7]),
        ("response_to_usb_in", p1[9], p1[10]), ("rx_restore", p2[0], p2[1])):
        result[f"{prefix}_us_avg"] = to_us(total / count) if count else None
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("action", choices=("reset", "dump"))
    parser.add_argument("--serial")
    args = parser.parse_args()
    device = open_probe(args.serial)
    try:
        if args.action == "reset":
            reset(device)
            print("diagnostics reset")
        else:
            print(json.dumps(dump(device), indent=2, ensure_ascii=False))
    finally:
        usb.util.dispose_resources(device)


if __name__ == "__main__":
    main()
