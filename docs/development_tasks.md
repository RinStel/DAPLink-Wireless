# 开发任务

## CMSIS-DAP v2 审查修复

- [x] Fix USB OUT request/abort buffer identification.
- [x] Accept CMSIS-DAP commands carried in padded Bulk packets.
- [x] Make SWD WAIT retry execution abortable and bounded.
- [x] Replace independent idle channel scans with deterministic rendezvous.
- [x] Return the standard `ID_DAP_Invalid` response for unknown commands.
- [x] Implement `DAP_SWD_Sequence`.
- [x] Implement Transfer Match Value and Match Mask semantics.
- [x] Document Timestamp as unsupported while the capability bit remains clear.
- [x] Add a focused host-side test for SWD tunnel protocol framing.

## 硬件验证

- [ ] Run Arm CMSIS-DAP Validation against a real Cortex-M target.
- [ ] Verify Transfer Abort latency during a target that continuously returns WAIT.
- [ ] Verify two radios rendezvous after starting on different blocked channels.

## 发布加固

- [x] Centralize firmware version and USB device release number.
- [x] Add an independent watchdog and expose the previous reset cause.
- [x] Clamp pathological SWD clock requests to a safe minimum.
- [x] Generate release BIN and SHA-256 manifest with flash/RAM gates.
- [ ] Validate watchdog recovery on hardware.
- [x] Add `STATUS.TXT` and remount the disk after every apply result.
- [x] Add debounced short/long button configuration with atomic persistence.
- [x] Restart RX after CRC, sync-word and RX timeout IRQs.
- [x] Treat TX timeout as a radio failure and recover automatically.
- [x] Bind accepted traffic to the current peer session.
- [x] Drop stale slave-side replies when the wireless host restarts.
- [x] Add host-side CMSIS-DAP command, padding, abort and timeout tests.
- [x] Add radio protocol and deterministic hopping tests.
- [x] Add a unified release verification and packaging workflow.
- [x] Add stack, memory, hash and dependency-path release gates.
- [x] Add a pyOCD hardware smoke test and formal acceptance procedure.
- [x] Align ResetTarget, Disconnect, and Match Mismatch with Arm semantics.
- [x] Bound wireless SWD retry time and reject oversized SWD sequences.
- [x] Deliver TransferAbort to the wireless slave during target WAIT retries.
- [x] Bound each SWD request below the CMSIS-DAP and radio deadlines.
- [x] Hold USB disconnected long enough for configuration-disk remount.
- [x] Run GCC static analysis on pure protocol and configuration modules.
- [x] Validate USB composite descriptors, WCID and PMA allocation on host.
- [x] Simulate Flash erase/program/commit failures and CRC recovery.
- [x] Add a reproducible source-tree fingerprint to the release manifest.
- [x] Reject malformed CDC line-coding control requests before buffer setup.
- [x] Manage CMSIS-DAP as a pinned Git submodule.
- [x] Lock immutable GD32 V3.0.3 vendor snapshots by tree hash.
- [x] Remove project behavior from modified vendor USB structures and callbacks.
- [x] Guard the vendor USB standard-request dispatch table.
- [x] Reject invalid USB standard-request interface and endpoint indices.
- [x] Validate CDC control request direction, recipient, value and length.
- [x] Keep GCC and Keil outputs outside `firmware/` and enforce source-tree cleanliness before and after release builds.
- [ ] Complete every item in [the release checklist](release_checklist.md).
