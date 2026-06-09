# SX1281 Frequency Hopping

The radio link uses packet-level adaptive frequency hopping rather than
Bluetooth's 1600-hop/s clocked slots. The existing link is half-duplex and
stop-and-wait, so packet-level switching provides deterministic recovery
without requiring a continuously synchronized radio clock.

## Channel plan

- 16 channels from 2405 MHz through 2480 MHz.
- Channel spacing is 5 MHz, which leaves margin for the widest configured
  SX1281 GFSK bandwidth of 2.4 MHz.
- The configured sync code derives the network ID, channel permutation and
  initial rendezvous channel.
- The channel plan stays inside the SX1281 2400-2483.5 MHz frequency range.

## Normal hopping

The wireless host initiates a channel change after eight acknowledged
business frames:

1. The host sends `HOP_SWITCH` on the current channel.
2. The slave acknowledges on the current channel, then enters the proposed
   channel for a 360 ms trial.
3. The host receives the ACK, switches and sends `HOP_CONFIRM`.
4. The slave acknowledges the confirmation and both sides remain on the new
   channel.
5. Either side rolls back if its part of the confirmation transaction cannot
   complete.

Only the host initiates scheduled changes. This avoids simultaneous,
conflicting channel proposals.

## Blocked-channel recovery

ACKs report the receive channel in addition to RSSI and SX1281 packet status.
An ACK from another channel is not accepted for the pending transaction.

Each failed business-frame attempt penalizes its channel. After 180 ms
without a valid frame, both peers enter the same 500 ms recovery cycle. They
spend the first 300 ms on the sync-code-derived rendezvous channel, then scan
the same deterministic channel sequence with a 35 ms dwell. Retransmissions
use that same recovery function, so sender and receiver do not independently
walk unrelated channel candidates.

Successful exchanges reduce the local channel penalty. All penalties also
decay once per 32 successful transactions so a temporarily occupied channel
is eventually probed again. The current implementation keeps channel quality
locally; it does not yet exchange a Bluetooth-style shared AFH channel map.

## Protocol compatibility

The radio protocol version is `0x03`. Both devices must run matching firmware.
The ACK metrics payload is seven bytes; byte 6 is the receive channel index.
CMSIS-DAP vendor status version 5 includes:

| Offset | Meaning |
| --- | --- |
| 28 | Current radio channel index |
| 29 | Channel switches since boot |
| 35..38 | Radio timeout IRQ count |
| 39..42 | Invalid radio frame count |
| 43..46 | Peer session change count |
