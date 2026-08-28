"""Hardware regression for back-to-back CMSIS-DAP commands over radio."""

import argparse

from pyocd.probe.aggregator import DebugProbeAggregator
from pyocd.probe.debug_probe import DebugProbe
from pyocd.core.session import Session


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--uid", help="CMSIS-DAP unique ID")
    parser.add_argument("--clock", type=int, default=1_000_000)
    args = parser.parse_args()

    probes = DebugProbeAggregator.get_all_connected_probes(args.uid)
    if len(probes) != 1:
        raise RuntimeError(
            f"expected exactly one CMSIS-DAP probe, found {len(probes)}"
        )

    probe = probes[0]
    Session(probe, auto_open=False)
    connected = False
    probe.open()
    try:
        probe.connect(DebugProbe.Protocol.SWD)
        connected = True
        probe.set_clock(args.clock)
    finally:
        if connected:
            probe.disconnect()
        probe.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
