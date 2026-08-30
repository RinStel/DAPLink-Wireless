import struct
import sys

sys.path.insert(0, "tools")
from radio_throughput_test import dap_exchange  # noqa: E402
from pyocd.probe.pydapaccess import DAPAccess  # noqa: E402

probes = DAPAccess.get_connected_devices()
probe = probes[0]
probe.open()
r = dap_exchange(probe, bytes((0x81, 0x01, 2)))
v = struct.unpack_from("<15I", r, 4)
print("cdc_init_count:", v[6])
print("cdc_out_count :", v[7])
print("cdc_last_used :", v[8])
print("cdc_last_armed:", v[9])
print("cdc_read_count:", v[10])
print("ep3_at_init   : 0x%04X" % v[11])
print("ep3_last      : 0x%04X" % v[12])
probe.close()
