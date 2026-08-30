#!/usr/bin/env python3
"""Measure raw wireless throughput over the DAPLink-Wireless CDC bridge.

While loopback mode is enabled the slave echoes every DATA frame back to
the host, so the measured goodput covers a full wireless round trip per
byte. The stream is a self-synchronising sequence of little-endian u32
counters: any radio frame loss shows up as a counter jump.
"""

import argparse
import struct
import sys
import threading
import time

import serial
from serial.tools import list_ports

from pyocd.probe.pydapaccess import DAPAccess

VID = 0x28E9
PID = 0x1290
LOOPBACK = 0x82
WORD = struct.Struct("<I")


def find_cdc_port():
    ports = [p for p in list_ports.comports() if p.vid == VID and p.pid == PID]
    if len(ports) != 1:
        raise SystemExit(f"expected one CDC port, found {ports}")
    return ports[0].device


def dap_exchange(device, payload):
    device._interface.write(list(payload) + [0] * (64 - len(payload)))
    return bytes(device._interface.read())


def set_loopback(device, enable):
    response = dap_exchange(device, bytes((LOOPBACK, 1 if enable else 0)))
    if len(response) < 2 or response[0] != LOOPBACK or response[1] != 0:
        raise SystemExit(f"loopback toggle failed: {response.hex()}")


class Reader(threading.Thread):
    def __init__(self, port, stop_event):
        super().__init__(daemon=True)
        self.port = port
        self.stop_event = stop_event
        self.buf = b""
        self.next_word = 0
        self.words = 0
        self.lost = 0
        self.errors = None

    def run(self):
        try:
            while not (self.stop_event.is_set() and not self.buf):
                data = self.port.read(4096)
                if not data:
                    continue
                self.buf += data
                count = len(self.buf) // WORD.size
                words = struct.unpack_from(f"<{count}I", self.buf)
                self.buf = self.buf[count * WORD.size:]
                for value in words:
                    if value < self.next_word:
                        self.lost += 1  # duplicate
                    else:
                        self.lost += value - self.next_word
                        self.next_word = value + 1
                    self.words += 1
        except (serial.SerialException, OSError) as exc:
            self.errors = str(exc)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", help="host CDC COM port (auto-detected)")
    parser.add_argument("--seconds", type=float, default=10.0)
    parser.add_argument("--chunk", type=int, default=4096,
                        help="stream write size, multiple of 4")
    parser.add_argument("--serial", help="probe serial for the toggle")
    args = parser.parse_args()

    port_name = args.port or find_cdc_port()
    print(f"CDC port: {port_name}")

    probes = [d for d in DAPAccess.get_connected_devices()
              if args.serial is None or d.get_unique_id() == args.serial]
    if len(probes) != 1:
        raise SystemExit(f"expected one probe, found {len(probes)}")
    probe = probes[0]
    probe.open()
    try:
        set_loopback(probe, True)
        port = serial.Serial(port_name, timeout=0.05, write_timeout=2.0)
        port.reset_input_buffer()
        stop_event = threading.Event()
        reader = Reader(port, stop_event)
        reader.start()

        words = args.chunk // WORD.size
        start = time.monotonic()
        sent = 0
        deadline = start + args.seconds
        try:
            while time.monotonic() < deadline:
                payload = WORD.pack(reader.next_word + sent // WORD.size)
                payload = payload * (words - 1)
                # 连续递增的 u32 流：首字 + 递增余部
                payload = (b"".join(
                    WORD.pack(reader.next_word + (sent // WORD.size) + i)
                    for i in range(words)))
                port.write(payload)
                sent += len(payload)
        except serial.SerialTimeoutException as exc:
            print(f"write stalled (window backpressure): {exc}")
        elapsed = time.monotonic() - start

        expected_words = sent // WORD.size
        tail_start = time.monotonic()
        while (time.monotonic() - tail_start < 2.0
               and reader.next_word < expected_words):
            time.sleep(0.1)
        time.sleep(0.2)
        stop_event.set()
        reader.join(timeout=1.0)
        port.close()
    finally:
        set_loopback(probe, False)
        probe.close()

    received = reader.words * WORD.size
    missing = expected_words - reader.next_word
    goodput = received / elapsed / 1024.0 if elapsed else 0.0
    print(f"sent     : {sent} bytes in {elapsed:.2f} s "
          f"({sent / elapsed / 1024.0:.1f} kB/s offered)")
    print(f"echoed   : {received} bytes "
          f"({goodput:.1f} kB/s round-trip goodput)")
    print(f"words    : {reader.next_word}/{expected_words} "
          f"(lost {missing * 4} bytes, "
          f"{missing * 100.0 / max(expected_words, 1):.2f}%)")
    if reader.errors:
        print(f"reader error: {reader.errors}")
    return 0 if missing == 0 and received else 1


if __name__ == "__main__":
    sys.exit(main())
