# Throughput Follow-up Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans (recommended) or superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove the remaining software-side throughput and data-integrity bottlenecks identified by the 2026-08-25 project audit, then leave hardware performance gates explicit and measurable.

**Architecture:** Preserve the v2 wire format, the single target-SWD owner, the four-slot DATA window, and full-speed USB endpoints. Add backpressure at the CDC and DATA-window boundaries, retry SWD WAIT in the asynchronous executor, remove avoidable CMSIS-DAP tunnel splitting, and optimize the SWD/radio hot paths only after behavior is protected by regressions.

**Tech Stack:** C11 firmware, GD32F30x standard peripheral library, GD32 USB device library, Arm GCC, PowerShell host-test scripts, GCC host regression tests.

**Spec:** `docs/superpowers/specs/2026-08-25-throughput-v2-design.md`

## Global Constraints

- Keep `RADIO_PROTOCOL_VERSION` exactly `2U`; do not add a v1 compatibility path.
- Keep the v2 application payload limit at 110 bytes and the 17-byte v2 header.
- Preserve MSC 16-byte bulk endpoints and CDC/CMSIS-DAP 64-byte endpoints.
- Preserve CMSIS-DAP response ordering, `DAP_TransferAbort`, SWD signal phase, and target ownership.
- Never consume source data unless the destination has accepted it into bounded storage.
- Preserve unrelated dirty work; do not modify `vendor/` or `Third-Party/`.
- Do not commit or push unless the user explicitly requests it.
- Software tests and builds do not count as physical SWCLK, UART, radio, or target-integrity acceptance.

## Optimization Baseline Evidence

- `scripts/test_host.ps1 -Name all`: 15 test groups passed before this follow-up.
- `scripts/verify_release.ps1 -SkipKeil`: passes on the current checkout.
- Baseline Release manifest: Flash `46448` bytes, RAM `29508` bytes, maximum static function stack `432` bytes.
- CDC transport owns one 64-byte RX packet and one 64-byte TX packet, while the service layer can pass 110-byte payloads.
- `wireless_source_process()` consumes source data before checking whether the four-slot TX window accepted it.
- `CMSIS_DAP_TUNNEL_CHUNK_MAX` is 10 while the v2 SWD block codec accepts up to 21 transfers.
- Asynchronous `target_swd_transfer_poll()` returns the first non-OK ACK, including WAIT, without an internal retry state.
- `clock_delay()` reads DWT through the external `board_cycle_count()` function on the 4 MHz SWD hot path.
- The radio DATA/ACK path has no explicit half-duplex turnaround policy; real collision and throughput evidence is still pending.

## Current Software Gate Evidence (2026-08-26)

- `scripts/test_host.ps1 -Name all`: 16 host test groups passed.
- Debug and Release GCC builds passed.
- Release manifest: Flash `47036` bytes, RAM `30108` bytes, maximum static function stack `432` bytes;
  the aligned BIN is `47052` bytes.
- `scripts/verify_release.ps1 -SkipKeil` and `git diff --check` passed.
- These software results do not establish physical USB enumeration, SWCLK, wired programming speed,
  wireless goodput, or UART/CDC integrity.

## Task 1: CDC stream queue and source-safe delivery

**Files:**
- Create: `tests/cdc_acm_transport_test.c`
- Modify: `firmware/usb/cdc_acm_transport.c`
- Modify: `firmware/usb/cdc_acm_transport.h`
- Modify: `firmware/app/serial_service.c`
- Modify: `scripts/test_host.ps1`
- Modify: `scripts/build_gcc.ps1`

**Interfaces:**
- Add a bounded CDC TX software queue behind `cdc_acm_write()`.
- Add `cdc_acm_tx_free()` so service code can reserve destination capacity before consuming UART data.
- Split queued data into 64-byte USB IN packets without exposing radio payload sizes to USB callbacks.

- [x] **Step 1: Write the failing CDC queue regression.**

  Drive the CDC endpoint callbacks with a host USB stub. Assert that a 110-byte write is accepted, emitted as one 64-byte packet followed by one 46-byte packet, and that bytes remain ordered. Assert that a write larger than the bounded queue is rejected without partial acceptance.

- [ ] **Step 2: Run the focused test and capture RED.**

  The pre-fix RED output was not retained in the current checkout. The focused and full GREEN
  results are recorded below.

  Run `& .\\scripts/test_host.ps1 -Name cdc-transport`.
  Expected: the test fails because the current single 64-byte TX buffer rejects 110 bytes.

- [x] **Step 3: Implement the bounded CDC TX queue.**

  Keep the endpoint callback buffer at 64 bytes. Queue the complete accepted application write first, start one USB packet, and advance the queue only after the IN callback. Return zero unless the complete application write fits. Preserve final zero-length-packet behavior for an exact-multiple write.

- [x] **Step 4: Make wired UART-to-CDC consumption capacity-aware.**

  Use `cdc_acm_tx_free()` before calling `target_uart_read()`. Do not remove bytes from the UART ring when the CDC queue cannot accept the complete chunk. Keep wireless host delivery atomic at the radio-frame boundary so a failed enqueue causes retransmission instead of an ACK.

- [x] **Step 5: Run focused and serial regressions.**

  Run `& .\\scripts/test_host.ps1 -Name cdc-transport`, `& .\\scripts/test_host.ps1 -Name target-uart-ring`, and `& .\\scripts/test_host.ps1 -Name all`.

## Task 2: Prevent DATA-window source loss

**Files:**
- Modify: `firmware/app/radio_window.h`
- Modify: `firmware/app/radio_window.c`
- Modify: `firmware/app/serial_bridge.c`
- Modify: `tests/serial_bridge_window_test.c`

**Interfaces:**
- Add `radio_window_tx_free(const radio_window_t *window)` returning the number of unused TX slots.

- [x] **Step 1: Add the failing capacity regression.**

  Fill all four TX slots, assert the free-slot count is zero, release one slot through cumulative ACK, and assert the free-slot count becomes one.

- [ ] **Step 2: Run `& .\\scripts/test_host.ps1 -Name serial-bridge-window` and capture RED.**

  The pre-fix RED output was not retained in the current checkout. The focused and full GREEN
  results are recorded below.

- [x] **Step 3: Implement the capacity helper and source guard.**

  Return from `wireless_source_process()` before `serial_service_source_take()` when no TX slot is available. Keep the existing push-result check as a defensive failure path; the normal source-consumption path must have a slot available first.

- [x] **Step 4: Run focused and full host tests.**

  Run `& .\\scripts/test_host.ps1 -Name serial-bridge-window` and `& .\\scripts/test_host.ps1 -Name all`.

## Task 3: Remove avoidable SWD tunnel splitting

**Files:**
- Modify: `firmware/app/cmsis_dap.c`
- Modify: `tests/cmsis_dap_protocol_test.c`

- [x] **Step 1: Add a failing max-legal-chunk regression.**

  Submit a legal CMSIS-DAP transfer request whose count exceeds 10 but fits the existing packet/response limits. Assert that the bridge receives one chunk containing the full legal count rather than 10 plus a remainder.

- [ ] **Step 2: Run `& .\\scripts/test_host.ps1 -Name cmsis-dap` and capture RED.**

  The pre-fix RED output was not retained in the current checkout. The focused and full GREEN
  results are recorded below.

- [x] **Step 3: Raise the internal chunk ceiling to 16.**

  Keep parser limits authoritative: read responses remain bounded by the 64-byte CMSIS-DAP packet, while the v2 codec remains capable of 21 transfers. Do not change the wire format.

- [x] **Step 4: Run the CMSIS-DAP and full host tests.**

## Task 4: Retry asynchronous SWD WAIT within the existing budget

**Files:**
- Modify: `firmware/app/swd_tunnel.c`
- Modify: `tests/swd_tunnel_protocol_test.c`

- [x] **Step 1: Make the SWD poll stub return WAIT before OK.**

  Add a test mode in the existing target-SWD substitute that returns WAIT for a bounded number of polls, then OK. Assert the operation completes successfully and records every attempted transfer.

- [ ] **Step 2: Run `& .\\scripts/test_host.ps1 -Name swd-tunnel` and capture RED.**

  The pre-fix RED output was not retained in the current checkout. The focused and full GREEN
  results are recorded below.

- [x] **Step 3: Add explicit WAIT retry state.**

  Keep the current request index and data phase active after WAIT, enforce the configured retry count and 250 ms execution budget, and continue honoring cancellation. Finish with WAIT only after the budget or retry limit is exhausted.

- [x] **Step 4: Run SWD tunnel, CMSIS-DAP, and full host tests.**

## Task 5: Optimize measured hot paths

**Files:**
- Modify: `firmware/drivers/swd/target_swd.c`
- Modify: `scripts/build_gcc.ps1`
- Modify: `firmware/drivers/radio/sx128x.c`
- Modify: `tests/sx128x_driver_test.c`

- [x] **Step 1: Add a source/build regression for the SWD DWT access.**

  Verify the Release object no longer has an unresolved `board_cycle_count` symbol after the hot-path change. Keep the existing target-SWD configuration tests unchanged.

- [x] **Step 2: Inline the DWT cycle reads.**

  Read `DWT->CYCCNT` directly in the cycle-critical file, preserving wrap-safe arithmetic and the requested clock ceiling. Rebuild the object with the existing `-O3` source-specific flag.

- [x] **Step 3: Add alternating packet-parameter cache coverage.**

  Extend the SX1281 host substitute to exercise RX-max, DATA-length, ACK-length, and RX-max transitions. Assert that repeated values use the cache while changed values still issue the command.

- [x] **Step 4: Implement a bounded multi-entry packet-parameter cache.**

  Do not change packet format, PHY settings, IRQ semantics, or the 7.5 MHz SPI ceiling. Optimize only redundant `SET_PACKET_PARAMS` transactions and preserve BUSY/error handling.

- [x] **Step 5: Run focused tests, both GCC configurations, and release verification.**

## Task 6: Hardware performance acceptance

**Files:**
- Modify: `docs/wireless_manual.md`
- Modify: `docs/development_release_manual.md`

- [ ] **Step 1: Measure wired SWD.**

  With the same Release artifact and target image, record actual SWCLK frequency/duty cycle and erase/program/verify time at 100 kHz, 1 MHz, 2 MHz, and 4 MHz.

- [ ] **Step 2: Measure wireless DATA and SWD.**

  Use two boards and PRBS payloads at 64/110 bytes, GFSK 2M/1M/500K and FLRC profiles, window 1/4, and immediate/delayed ACK. Record goodput, retransmission rate, CRC errors, timeout IRQs, and profile/channel transitions.

- [ ] **Step 3: Measure UART/CDC integrity.**

  Run 3 Mbps 8N1 PRBS in both directions for at least 10 MB and compare endpoint CRCs. Record UART overrun, radio retry, CDC enqueue failure, and USB reset counters.

- [x] **Step 4: Keep hardware results separate from software gates.**

  Do not mark real-device acceptance complete from host tests, builds, manifests, or protocol substitutes.
