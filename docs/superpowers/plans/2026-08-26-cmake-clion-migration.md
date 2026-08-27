# CMake/CLion Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make CMake/CLion the single firmware build structure, provide an explicit pyOCD flashing path, and remove all Keil-specific projects, scripts, and documentation.

**Architecture:** A root CMake project defines shared GD32F303 sources and three fixed-address firmware targets: Bootloader, Slot A, and Slot B. A dedicated Arm GCC toolchain file and CMake presets provide reproducible Debug/Release configuration; post-build commands generate ELF, HEX, and BIN artifacts, while existing Python tools generate DWUP and factory HEX from Release outputs. Non-default Release targets invoke a guarded pyOCD PowerShell wrapper so CLion and the terminal use the same flashing command.

**Tech Stack:** CMake 3.21+, GNU Arm Embedded GCC, C11, pyOCD, PowerShell host tests, Python 3 standard library, CLion CMake profiles.

**Spec:** User-approved chat design on 2026-08-26: CMake is the only firmware build entry; Keil files are not retained.

## Global Constraints

- Target MCU remains `GD32F303CCT6` with Cortex-M4 compiler flags and 48 KiB SRAM.
- Bootloader is linked at `0x08000000` (16 KiB); Slot A at `0x08004000` and Slot B at `0x08021000` (116 KiB each).
- Configuration storage `0x0803F000-0x0803FFFF` remains outside firmware images.
- Release version remains `0.9.0` / `FIRMWARE_VERSION_CODE=900U`.
- Keil project files, Keil build/flash scripts, and Keil-only validation are deleted rather than archived.
- No source changes are made to unrelated radio, USB, or DFU behavior.
- pyOCD uses the `stm32f103rc` compatibility target for the GD32F303 flash algorithm; every hardware run requires programming, verification, readback, and reset evidence.

### Task 1: Add the CMake cross-build foundation

**Files:**
- Create: `CMakeLists.txt`
- Create: `cmake/arm-none-eabi.cmake`
- Create: `CMakePresets.json`
- Modify: `.gitignore`
- Test: `tests/cmake_structure_test.py`

**Interfaces:**
- CMake targets: `daplink_bootloader`, `daplink_slot_a`, `daplink_slot_b`, `firmware_release_artifacts`.
- Cache variable: `ARM_GCC_BIN` points to the directory containing `arm-none-eabi-gcc`.
- Presets: `debug` and `release` configure out-of-source builds under `build/cmake/<preset>`.

- [x] Write a Python test that asserts the three firmware targets, linker scripts, fixed addresses, and no Keil references.
- [x] Run the focused test once before implementation and observe the expected missing-file failure.
- [x] Implement the toolchain file, source lists, compile definitions/options, linker selection, and `POST_BUILD` HEX/BIN conversion with `arm-none-eabi-objcopy`.
- [x] Add Release custom commands invoking `tools/build_dwup.py` and `tools/build_factory_hex.py` using Slot A/B and Bootloader BIN/HEX outputs.
- [x] Re-run the focused test and confirm both presets configure successfully.

### Task 2: Make existing GCC scripts delegate to CMake

**Files:**
- Modify: `scripts/build_gcc_targets.ps1`
- Modify: `scripts/build_gcc.ps1`
- Modify: `scripts/verify_release.ps1`
- Test: `tests/cmake_structure_test.py`

**Interfaces:**
- `scripts/build_gcc.ps1 -Configuration Debug|Release` remains a compatibility wrapper around `cmake --build --preset <preset>`.
- `scripts/verify_release.ps1` consumes CMake Release output paths without duplicating compiler/source lists.

- [x] Extend the test to assert PowerShell wrappers do not contain `arm-none-eabi-gcc` source compilation loops.
- [x] Replace duplicated PowerShell compilation with configure/build calls and explicit toolchain error messages.
- [x] Run Debug and Release builds and compare artifact names/locations with the current release contract.

### Task 3: Remove Keil implementation and update documentation

**Files:**
- Delete: `firmware/project.uvprojx`
- Delete: `firmware/project.uvoptx`
- Delete: `firmware/project.uvguix.YSCha`
- Delete: `firmware/keil_targets.json`
- Delete: `scripts/build_keil.ps1`
- Delete: `scripts/flash_factory.ps1`
- Delete: `tools/keil_support.py`
- Delete: `tests/flash_factory_test.py`
- Modify: `README.md`
- Modify: `docs/development_release_manual.md`
- Modify: `docs/hardware_manual.md`
- Modify: `docs/README.md`

**Interfaces:**
- Documentation build commands use `cmake --preset debug`, `cmake --preset release`, and `cmake --build --preset <preset>`.
- Documentation states CMake produces firmware artifacts; hardware flashing is an external probe operation and is not falsely claimed as software verification.

- [x] Remove Keil commands, FLM references, and STM32CubeProgrammer failure notes from active operational instructions.
- [x] Document CLion opening the repository root and selecting the `debug`/`release` CMake profile.
- [x] Remove obsolete Keil-only test references and verify no active file mentions `UV4`, `uvprojx`, `GD32F30x_HD.FLM`, or `STM32_Programmer`.

### Task 4: Run migration gates and clean generated state

**Files:**
- Modify: `docs/superpowers/plans/2026-08-26-cmake-clion-migration.md`

- [x] Run `python -m unittest discover -s tests -p '*_test.py' -v`.
- [x] Run `cmake --preset debug` and `cmake --build --preset debug`.
- [x] Run `cmake --preset release` and `cmake --build --preset release`.
- [x] Run `powershell -ExecutionPolicy Bypass -File scripts/verify_release.ps1` with the CMake-only interface.
- [x] Run `git diff --check` and `rg` checks for forbidden Keil references.
- [x] Record that physical SWD flashing remains a separate hardware gate; do not report it as completed from CMake builds.

### Task 5: Add pyOCD flashing targets for CLion and the terminal

**Files:**
- Create: `scripts/flash_pyocd.ps1`
- Create: `tests/pyocd_flash_script_test.py`
- Modify: `CMakeLists.txt`
- Modify: `tests/cmake_structure_test.py`
- Modify: `README.md`
- Modify: `docs/development_release_manual.md`
- Modify: `docs/hardware_manual.md`

**Interfaces:**
- `scripts/flash_pyocd.ps1 -Artifact <release HEX/ELF>` invokes `pyocd load` with `--no-wait`, `--target`, `--frequency`, `--connect`, and `--erase`.
- `-WhatIf` prints the resolved command without opening a probe or writing Flash.
- Release CMake targets `flash_factory`, `flash_bootloader`, `flash_slot_a`, and `flash_slot_b` remain non-default and accept `PYOCD_TARGET`, `PYOCD_PROBE`, `PYOCD_FREQUENCY`, `PYOCD_CONNECT`, and `PYOCD_ERASE` cache variables.

- [x] Add artifact allow-listing and dry-run coverage before invoking pyOCD.
- [x] Expose non-default CMake flash targets using the host Windows PowerShell executable.
- [x] Document CLion profile variables, pyOCD compatibility-target limitations, and the separate hardware acceptance gate.
- [x] Run the full software verification suite and inspect generated CMake commands without executing a destructive flash.
