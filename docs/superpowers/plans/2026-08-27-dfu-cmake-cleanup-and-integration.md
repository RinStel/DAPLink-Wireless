# DFU/CMake Cleanup and Integration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task with verification checkpoints.

**Goal:** 将 USB DFU、CMake/GCC 和 pyOCD 工作树整理为可发布工程，删除过时入口，补齐必要注释，并安全合并到 `main`。

**Architecture:** 保留 CMake Presets 作为唯一构建编排，保留 `scripts/flash_pyocd.ps1` 作为 SWD 烧录入口和 `tools/daplink_updater.py` 作为在线 DFU 入口。测试按发布契约、主机工具和硬件相关边界保留；仅删除旧 Keil 工程、重复包装脚本或无消费者的临时流程。

**Tech Stack:** CMake/Ninja、GCC Arm 交叉编译、Python unittest、PowerShell、pyOCD、标准 USB DFU 1.1。

**Spec:** `docs/superpowers/specs/2026-08-26-usb-dfu-design.md`

## Global Constraints

- Bootloader 固定为 `0x08000000`–`0x08003FFF`，DFU 传输块大小必须等于 64 字节镜像头大小。
- 应用槽、Boot 状态和配置区不得被 Bootloader 烧录脚本覆盖。
- DFU 必须检查芯片型号、槽、地址、长度、向量表、头部 CRC32 和镜像 CRC32。
- 正常模式拒绝不严格递增版本；恢复模式允许同版本或降级。
- `PC13=蓝`、`PC14=红`、`PC15=绿`；所有 LED 通道低电平有效。
- 不执行 `git reset --hard`、`git clean` 或无授权删除；保留用户已有的非本任务改动。

### Task 1: 审计并划定清理边界

**Files:**
- Inspect: `git status`, `CMakeLists.txt`, `CMakePresets.json`, `scripts/`, `firmware/project.uv*`, `tests/`, `docs/`
- Modify: none

- [x] **Step 1: 建立文件和引用清单**

运行 `rg --files`、`rg -n "Keil|uvproj|package_release|release_common|BOOT_DFU_TRANSFER_SIZE|TODO|DEBUG"`，记录每个候选文件的消费者。

- [x] **Step 2: 确认当前分支和主线差异**

运行 `git diff --stat main...HEAD`、`git diff --name-status`，区分已提交功能与未提交工作树改动。

- [x] **Step 3: 保留/删除判定**

保留 CMake、GCC、pyOCD、DFU 工具和发布门禁；只有确认无引用且属于旧 Keil/临时调试的文件才进入删除清单。

### Task 2: 收敛代码注释和构建入口

**Files:**
- Modify: `firmware/bootloader/dfu_device.h`, `firmware/bootloader/dfu_device.c`, `firmware/bootloader/main.c`, `firmware/bsp/board_pins.h`, `scripts/build_gcc.ps1`, `scripts/verify_release.ps1`
- Delete only after Task 1 proves unused: legacy Keil project files and duplicate packaging wrappers
- Test: `tests/dfu_device_test.c`, `tests/dfu_usb_descriptor_test.c`, `tests/startup_sequence_test.ps1`

- [x] **Step 1: 为协议边界补充短注释**

说明 64 字节 block 0、`dfuMANIFEST_WAIT_RESET`、Boot 状态复制和 LED 映射的原因；注释不得重复设计文档。

- [x] **Step 2: 删除已证明无消费者的旧入口**

删除前执行引用审计；不得删除 CMake Preset、GCC 构建包装器、pyOCD 脚本或在线 DFU 工具。

- [x] **Step 3: 运行针对性测试**

运行 `powershell -ExecutionPolicy Bypass -File .\scripts\test_host.ps1 -Name all`，确认删除和注释整理没有改变行为。

### Task 3: 整理文档和发布说明

**Files:**
- Modify: `README.md`, `docs/README.md`, `docs/development_release_manual.md`, `docs/hardware_manual.md`, `docs/project_manual.md`, `docs/schematic_u5_connections.md`, `docs/wireless_manual.md`, `CHANGELOG.md`

- [x] **Step 1: 统一版本、LED 和 DFU 流程**

所有文档使用固件版本 `1.0.0`，明确 `ENTER_DFU=1`、恢复按键、`dfu-util`/`daplink_updater.py` 命令和 64 字节传输契约。

- [x] **Step 2: 删除过时的 Keil/临时验证表述**

保留未完成的硬件验收项，但将软件门禁与实板验收分开描述。

- [x] **Step 3: 执行格式与术语检查**

运行 `git diff --check`，并用 `rg` 检查旧版本号、旧 LED 映射和已删除入口残留。

### Task 4: 发布门禁和分边界提交

**Files:**
- Inspect: all changed files

- [x] **Step 1: 运行完整门禁**

运行 `powershell -ExecutionPolicy Bypass -File .\scripts\verify_release.ps1`，记录测试数量、Debug/Release 构建和布局检查结果。

- [x] **Step 2: 检查产物和差异**

运行 `git status --short`、`git diff --check`、`git diff --stat`，确认没有构建垃圾或未解释的新文件。

- [x] **Step 3: 分边界提交**

按“DFU/启动代码”“CMake/脚本/测试”“文档清理”建立本地提交，提交前使用显式路径 `git add`，不提交用户私有配置或临时产物。

### Task 5: 合并到主线并验证

**Files:**
- Modify: conflicts reported by `git merge main` or `git merge feature/usb-dfu`

- [ ] **Step 1: 记录分支基线并提交工作树**

在 `feature/usb-dfu` 上完成 Task 4 后确认工作树干净，再切换到主仓库根目录的 `main`。

- [ ] **Step 2: 合并并处理冲突**

运行 `git merge feature/usb-dfu`；冲突按 CMake/DFU 设计解决，优先保留主线的新版本信息和本分支完整 DFU 契约。每个冲突解决后运行 `git diff --check`。

- [ ] **Step 3: 在合并结果运行完整门禁**

运行 `powershell -ExecutionPolicy Bypass -File .\scripts\verify_release.ps1`。若失败，保留分支和合并状态并修复，不删除 worktree。

- [ ] **Step 4: 确认合并结果和分支状态**

运行 `git status --short`、`git log --oneline --decorate -5` 和 `git branch --contains feature/usb-dfu`，报告合并提交、验证结果和未完成的硬件验收。
