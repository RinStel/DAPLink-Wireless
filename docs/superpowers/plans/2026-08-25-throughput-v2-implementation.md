# Throughput and Wireless Protocol v2 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the throughput-v2 design so wired SWD, UART/USB, wireless SWD/DATA, jump control, and SX1281 command paths no longer use the identified avoidable bottlenecks.

**Architecture:** Keep the single target SWD owner and the existing CMSIS-DAP core, but make SWD execution cooperative. Replace the v1 radio frame parser and stop-and-wait bridge with a v2-only 17-byte-header protocol, a 4-slot DATA window, cumulative ACK bitmap, compressed SWD block frames, and ACK-carried channel changes. Preserve MSC with 16-byte bulk endpoints while giving CDC and CMSIS-DAP 64-byte endpoints inside the 512-byte PMA.

**Tech Stack:** C11 firmware, GD32F30x standard peripheral library, GD32 USB device library, Arm GCC, PowerShell host-test scripts, GCC host regression tests.

**Spec:** `docs/superpowers/specs/2026-08-25-throughput-v2-design.md`

## Global Constraints

- Wireless `version` must be exactly `2U`; reject `version != 2U` and retain no v1 path.
- v2 application payload is exactly limited by `127 - 17 = 110` bytes.
- `DATA` uses four transmit slots per direction and cumulative `ack_next + bitmap` ACKs.
- `SWD_BLOCK` is one active target-SWD transaction and carries at most 21 ordinary transfers.
- MSC remains present with 16-byte bulk endpoints; CDC data and CMSIS-DAP endpoints are 64 bytes.
- Preserve response order, `DAP_TransferAbort`, SWD signal phase, and unrelated dirty work.
- Do not modify `vendor/` or `Third-Party/`; do not commit or push.
- Software tests/builds do not count as physical SWCLK, UART, radio, or target-integrity acceptance.

---

### Task 1: Freeze and test the v2 protocol primitives

**Files:**
- Modify: `firmware/app/radio_protocol.h`
- Modify: `firmware/app/radio_protocol.c`
- Modify: `tests/radio_protocol_test.c`
- Modify: `scripts/build_gcc.ps1`

**Interfaces:**
- `RADIO_PROTOCOL_VERSION` becomes `2U`.
- `RADIO_PROTOCOL_PAYLOAD_SIZE` becomes `110U`.
- Add `RADIO_PROTOCOL_ACK_PAYLOAD_SIZE` and v2 ACK encode/decode helpers with exact `ack_next`, bitmap, flags, channel, and metrics fields.
- `radio_protocol_parse()` rejects every version other than `2U`.

- [x] **Step 1: Add failing v2 tests.**

  Extend the host radio test with a 110-byte build/parse case, a 111-byte rejection, a v1 frame rejection, and ACK round-trip assertions for `ack_next=0x10203040`, `bitmap=0x0000000D`, flags, channel, and all seven metric bytes.

- [x] **Step 2: Run the focused test and capture RED.**

  Run `.scripts\test_host.ps1 -Name radio-protocol`.
  Expected: the test fails because the current constants accept v1/64-byte frames and have no ACK helpers.

- [x] **Step 3: Implement only the protocol constants, parser, and ACK helpers.**

  Keep the 17-byte header layout and existing network/session/sequence encoding. Make builders reject payloads above 110 and parsers reject `frame[2] != 2U` before reading payload fields.

- [x] **Step 4: Run the focused test and capture GREEN.**

  Run `.scripts\test_host.ps1 -Name radio-protocol` and require zero failures.

### Task 2: Add compressed SWD block v2 codecs

**Files:**
- Modify: `firmware/app/swd_tunnel.h`
- Modify: `firmware/app/swd_tunnel.c`
- Modify: `firmware/app/serial_bridge.h`
- Modify: `firmware/app/serial_bridge.c`
- Modify: `tests/swd_tunnel_protocol_test.c`

**Interfaces:**
- Add `swd_tunnel_encode_block()` and `swd_tunnel_decode_block()` for `[transaction_id,count,request[count],write_data...]`.
- Add `SWD_TUNNEL_MAX_BLOCK_TRANSFERS 21U`; block encoding is the only SWD transfer path.
- Add `serial_bridge_swd_block_request()` that emits one `RADIO_FRAME_SWD_BLOCK`.

- [x] **Step 1: Add failing codec tests.**

  Test mixed read/write requests, 21-transfer boundary, rejection of 22 transfers, truncated write data, and response read-data ordering.

- [x] **Step 2: Run `.scripts\test_host.ps1 -Name swd-tunnel` and capture RED.**

- [x] **Step 3: Implement the compact request/response codecs.**

  Encode each request byte once; append only write values. Decode response data only for read transfers and reject count/length mismatches before touching the output array.

- [x] **Step 4: Update CMSIS-DAP wireless mapping and run GREEN.**

  Make the wireless host path use one block frame for each CMSIS-DAP block while wired mode continues using the local tunnel. Run `.scripts\test_host.ps1 -Name cmsis-dap` and `.scripts\test_host.ps1 -Name swd-tunnel`.

### Task 3: Replace radio stop-and-wait DATA with a four-slot v2 window

**Files:**
- Modify: `firmware/app/serial_bridge.c`
- Modify: `firmware/app/serial_bridge.h`
- Modify: `firmware/app/radio_protocol.h`
- Modify: `tests/radio_protocol_test.c`
- Create: `tests/serial_bridge_window_test.c`
- Modify: `scripts/test_host.ps1`

**Interfaces:**
- Add a bounded `RADIO_DATA_WINDOW_SIZE 4U` sender/receiver model.
- Add `serial_bridge_window_push()`, `serial_bridge_window_ack()`, `serial_bridge_window_receive()`, and `serial_bridge_window_take()` pure helpers for host tests.
- Control frames retain one reliable control slot; DATA frames use four slots and cumulative ACKs.

- [x] **Step 1: Write failing window tests.**

  Cover four outstanding DATA frames, fifth-frame rejection, out-of-order receive with bitmap ACK, cumulative release, duplicate suppression, timeout retransmission, and control-frame priority.

- [x] **Step 2: Run `.scripts\test_host.ps1 -Name serial-bridge-window` and capture RED.**

- [x] **Step 3: Implement the pure window helpers.**

  Use modular 32-bit sequence comparisons, never allocate dynamically, and keep all slot storage bounded to four frames per direction.

- [x] **Step 4: Replace `s_pending_frame` DATA handling.**

  Keep a separate control pending slot and DATA slot array. ACK processing must release all sequences below `ack_next` and bitmap-marked slots; retransmit only live slots. Run focused window and radio tests.

### Task 4: Make SWD execution cooperative and optimize the bit-bang hot path

**Files:**
- Modify: `firmware/drivers/swd/target_swd.h`
- Modify: `firmware/drivers/swd/target_swd.c`
- Modify: `firmware/app/swd_tunnel.c`
- Modify: `tests/swd_tunnel_protocol_test.c`
- Create: `tests/target_swd_async_test.c`
- Modify: `scripts/build_gcc.ps1`

**Interfaces:**
- Add `target_swd_transfer_begin()`, `target_swd_transfer_poll()`, `target_swd_transfer_cancel()`, and `target_swd_poll_result_t`.
- `swd_tunnel_process()` advances one transfer or WAIT step per call and exposes completion only after the whole block is encoded.

- [x] **Step 1: Add async-state regression coverage in the SWD tunnel test.**

  Test one-step progression, WAIT budget expiry, cancellation before the next data phase, and response order for a mixed block.

- [x] **Step 2: Run `.scripts\test_host.ps1 -Name swd-tunnel` and capture RED.**

- [x] **Step 3: Implement the bounded transfer state machine.**

  Preserve turnaround, idle cycles, parity, and the existing 250 ms transfer budget. Keep a thin synchronous wrapper only for initialization paths and host stubs.

- [x] **Step 4: Inline and compile the SWD hot path.**

  Make clock/GPIO helpers `static inline`, read DWT/GPIO registers directly, and add a source-specific `-O3` compile flag in `build_gcc.ps1` for `target_swd.c` only.

- [x] **Step 5: Run SWD tunnel and full host tests.**

### Task 5: Replace UART polling with DMA/interrupt-backed rings

**Files:**
- Modify: `firmware/drivers/serial/target_uart.c`
- Modify: `firmware/drivers/serial/target_uart.h`
- Modify: `firmware/app/serial_service.c`
- Create: `tests/target_uart_ring_test.c`
- Modify: `scripts/test_host.ps1`
- Modify: `firmware/app/main.c`

**Interfaces:**
- Add ring helpers for DMA write-position snapshots and TX descriptor submission.
- Add `USART0_IRQHandler` and DMA channel handlers without touching vendor startup files.
  RX uses circular DMA, TX uses non-circular segmented DMA, and the ring helpers remain
  the business-layer boundary.

- [x] **Step 1: Add host ring tests.**

  Test DMA wrap, unread-byte preservation, RX overrun count, TX segmentation, and a zero-length submission.

- [x] **Step 2: Run `.scripts\test_host.ps1 -Name target-uart-ring` and capture RED.**

- [x] **Step 3: Configure USART0 RX circular DMA and TX segmented DMA.**

  Configure DMA0 Channel 5 for RX and Channel 4 for TX. Enable USART IDLE and DMA
  half/full/error events, publish bytes through bounded rings, and remove business-byte
  polling from `target_uart_process()`.

- [x] **Step 4: Run the ring test and all serial-related host tests.**

### Task 6: Repack USB PMA while retaining MSC

**Files:**
- Modify: `firmware/usb/usbd_conf.h`
- Modify: `firmware/usb/usb_composite.c`
- Modify: `firmware/usb/cdc_acm_transport.c`
- Modify: `firmware/usb/cdc_acm_transport.h`
- Modify: `tests/usb_composite_descriptor_test.c`
- Modify: `scripts/test_host.ps1`

**Interfaces:**
- `MSC_DATA_PACKET_SIZE` becomes `16U` while `MSC_MEDIA_PACKET_SIZE` remains `512U`.
- `CDC_ACM_DATA_PACKET_SIZE` becomes `64U`; CDC software buffers remain independent of PMA buffers.
- DAP endpoints remain 64 bytes.

- [x] **Step 1: Add descriptor/PMA assertions.**

  Assert MSC endpoint size 16, CDC data endpoint size 64, DAP endpoint size 64, and no endpoint buffer overlap within 512 bytes.

- [x] **Step 2: Run `.scripts\test_host.ps1 -Name usb-descriptor` and capture RED.**

- [x] **Step 3: Repack endpoint addresses and update CDC transfer lengths.**

  Use contiguous 16-byte MSC regions, two 64-byte CDC regions, the 8-byte CDC notification region, and two 64-byte DAP regions. Keep MSC logical block handling at 512 bytes.

- [x] **Step 4: Run descriptor, USB transport, and full host tests.**

### Task 7: Reduce SX1281 command and hop-control overhead

**Files:**
- Modify: `firmware/drivers/radio/sx128x.c`
- Modify: `firmware/drivers/radio/sx128x.h`
- Modify: `firmware/app/serial_bridge.c`
- Modify: `firmware/app/frequency_hopping.c`
- Modify: `firmware/app/frequency_hopping.h`
- Modify: `tests/sx128x_driver_test.c`
- Modify: `tests/radio_protocol_test.c`

**Interfaces:**
- Cache packet params/profile/IRQ mask and expose command-cache hit counters to host tests.
- ACK carries `ACK_FLAG_HOP_VALID` and `next_channel`; the bridge uses `BRIDGE_HOP_INTERVAL 32U` and no per-hop switch/confirm exchange for DATA.

- [x] **Step 1: Add SX1281 command-cache tests.**

  Assert repeated `start_tx/start_rx` with unchanged params does not issue duplicate packet-param commands and that DIO1-low processing does not issue IRQ SPI transactions.

- [x] **Step 2: Run `.scripts\test_host.ps1 -Name sx1281` and capture RED.**

- [x] **Step 3: Implement cached packet params, no redundant TX standby, and DIO1 event gating.**

  Preserve BUSY checks, FIFO reads, packet status, and error handling. Clear only the IRQ mask needed for the completed event.

- [x] **Step 4: Piggyback channel changes in cumulative ACKs and run focused tests.**

### Task 8: Protocol/documentation/release gates

**Files:**
- Modify: `docs/wireless_manual.md`
- Modify: `docs/project_manual.md`
- Modify: `docs/development_release_manual.md`
- Modify: `docs/superpowers/plans/2026-08-25-wired-dap-speed-optimization.md`
- Modify: `scripts/build_gcc.ps1`
- Modify: `scripts/verify_release.ps1`

- [x] **Step 1: Update v2 wire documentation and remove v1 operational descriptions.**

- [x] **Step 2: Update Release manifest checks to require `radio_protocol: 2`.**

- [x] **Step 3: Run CSTE-ZH lint, all host tests, Debug/Release builds, `verify_release.ps1 -SkipKeil`, and `git diff --check`.**

- [x] **Step 4: Record hardware gates as pending until a probe and target are available.**
