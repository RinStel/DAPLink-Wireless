# 项目手册合并与无线协议 v1 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]` / `- [ ]`) syntax for tracking.

**Goal:** 将项目自有文档合并为少量项目手册，删除已归并的旧文档，并将无线帧协议版本从 `4` 调整为 `1`。

**Architecture:** 保留根目录入口、变更记录、第三方声明和 EasyEDA U5 原理图记录；将架构/配置、硬件、无线链路、开发/发布分别归并为四本手册。无线协议只保留一个由 `RADIO_PROTOCOL_VERSION` 驱动的版本常量，构造和解析路径继续共享 `RADIO_PROTOCOL_VERSION`。

**Tech Stack:** C 固件、PowerShell 构建/测试脚本、Markdown、Windows PowerShell、GCC 主机测试。

## Global Constraints

- `FIRMWARE_VERSION_STRING` 保持为 `"0.8.0-rc.3"`。
- `RADIO_PROTOCOL_VERSION` 必须改为 `1U`，协议帧头偏移 2 写入并校验 `01`。
- 不修改 `Third-Party/CMSIS-DAP/**`、`LICENSE`、`vendor/**` 或用户已有的 `docs/schematic_u5_connections.md` 内容。
- 原理图事实、当前代码状态和未决硬件决策必须分开表述；未证实内容使用 `待确认：`。
- 删除旧文档前必须完成内容归并、链接扫描和 Git 差异检查。

---

### Task 1: 为无线协议 v1 写入失败回归测试

**Files:**
- Modify: `tests/radio_protocol_test.c`
- Test: `scripts/test_host.ps1 -Name radio-protocol`

**Interfaces:**
- Consumes: `radio_protocol_build()`、`radio_protocol_parse()` 和 `RADIO_PROTOCOL_VERSION`。
- Produces: 能证明帧头第 2 字节必须为 `1`，且旧版本 `4` 被拒绝的主机回归测试。

- [x] **Step 1: 修改测试期望值**

在第一次构造帧后增加固定协议版本断言，并把非法版本用例改为显式的 `4U`：

```c
assert(frame[2] == 1U);
assert(frame[2] == RADIO_PROTOCOL_VERSION);
assert(radio_protocol_parse(frame, length, 0x12345678U, &view));

frame[2] = 4U;
assert(!radio_protocol_parse(frame, length, 0x12345678U, &view));
frame[2] = RADIO_PROTOCOL_VERSION;
```

保留后续长度、负载、`SWD_ABORT` 和跳频测试。

- [x] **Step 2: 运行测试确认 RED**

Run:

```powershell
.\scripts\test_host.ps1 -Name radio-protocol
```

Expected: 测试因当前 `RADIO_PROTOCOL_VERSION` 为 `4U`，在 `assert(frame[2] == 1U)` 处失败。RED 阶段不修改生产代码。

---

### Task 2: 将生产无线帧版本切换为 v1

**Files:**
- Modify: `firmware/app/radio_protocol.h:26`
- Verify: `firmware/app/radio_protocol.c`
- Test: `scripts/test_host.ps1 -Name radio-protocol`

**Interfaces:**
- Consumes: Task 1 的版本断言和旧版本拒绝用例。
- Produces: 构造帧和解析帧都以 `RADIO_PROTOCOL_VERSION == 1U` 为准。

- [x] **Step 1: 修改唯一协议版本常量**

将：

```c
#define RADIO_PROTOCOL_VERSION       4U
```

改为：

```c
#define RADIO_PROTOCOL_VERSION       1U
```

不在 `radio_protocol.c` 增加第二个版本常量；`radio_protocol.c` 已经通过同一宏写入和验证帧头。

- [x] **Step 2: 运行协议回归测试确认 GREEN**

Run:

```powershell
.\scripts\test_host.ps1 -Name radio-protocol
```

Expected: Radio protocol 测试退出码为 0。

---

### Task 3: 创建四本项目手册并更新导航

**Files:**
- Create: `docs/project_manual.md`
- Create: `docs/hardware_manual.md`
- Create: `docs/wireless_manual.md`
- Create: `docs/development_release_manual.md`
- Modify: `README.md`
- Modify: `docs/README.md`

**Interfaces:**
- Consumes: 现有 13 个待删除文档、`docs/schematic_u5_connections.md`、当前固件源码和脚本中的实际名称。
- Produces: 四本手册及根目录/`docs/` 导航，所有现有有效事实均有唯一归属。

- [x] **Step 1: 编写 `project_manual.md`**

合并产品架构、固件模块、USB 配置盘和开发路线。保留设备模式、串口参数同步、USB MSC/CDC/CMSIS-DAP 组合、PMA/端点约束、`CONFIG.TXT` 格式、Flash 双槽事务、按键行为、VID/PID 限制和当前软件状态。`FIRMWARE_VERSION_STRING` 与无线协议版本分别说明。

- [x] **Step 2: 编写 `hardware_manual.md`**

合并硬件审查和硬件验收，包含核心器件、板级安全态、首次上电、硬件风险、pyOCD 冒烟命令和发布签核项目。将旧的目标 SWD/供电/射频 GPIO 描述改为“当前代码状态”，引用 `schematic_u5_connections.md` 的原理图基线，并保留 `USB_AUTO_EN`、按键、DIO2/DIO3、串联电阻方向等 `待确认：` 项。

- [x] **Step 3: 编写 `wireless_manual.md`**

合并 `radio_protocol_v4.md`、`frequency_hopping.md` 和 `radio_bringup.md`。协议章节标题和帧头偏移 2 统一写作 v1/`01`；保留帧格式、ACK 七字节指标、`SWD_ABORT`、profile 切换、16 频道跳频、恢复时序、RSSI 阈值、SX1281 参数、SPI 引脚和双板冒烟步骤。

- [x] **Step 4: 编写 `development_release_manual.md`**

合并开发任务、CMSIS-DAP 验证、发布检查、厂商依赖。保留已完成/未完成状态、测试命令、真实目标验证要求、Release manifest、依赖锁、CMSIS-DAP 固定提交、厂商快照、许可证和 VID/PID 发布前置条件；将英文任务改为准确中文，但保留代码标识符和命令原文。

- [x] **Step 5: 更新两个文档入口**

让 `README.md` 只保留项目定位、快速构建/测试/打包和手册入口；让 `docs/README.md` 按四本手册、原理图记录、变更记录和第三方声明列出链接，并说明协议版本与固件发布版本的区别。

---

### Task 4: 更新发布包清单、变更记录和项目引用

**Files:**
- Modify: `scripts/package_release.ps1`
- Modify: `CHANGELOG.md`
- Modify: `docs/superpowers/specs/2026-08-18-project-manuals-and-radio-v1-design.md`
- Verify: all project Markdown and PowerShell scripts

**Interfaces:**
- Consumes: Task 2 的 `RADIO_PROTOCOL_VERSION == 1U` 和 Task 3 的新手册路径。
- Produces: 发布包使用新手册，`Unreleased` 记录本次未发布协议调整，历史版本不被重写。

- [x] **Step 1: 替换发布包文档路径**

将 `scripts/package_release.ps1` 中的 `radio_protocol_v4.md` 替换为 `wireless_manual.md`，并把待发布的旧手册路径替换为四本新手册和 `schematic_u5_connections.md`。

- [x] **Step 2: 更新 `CHANGELOG.md` 的 `Unreleased`**

增加两项事实记录：项目文档合并为四本手册；在产品发布前将无线协议帧版本定为 `1`。由于 `0.8.0-rc.3` 尚未公开发布，候选版本条目同步记录协议 v1。

- [x] **Step 3: 扫描旧引用**

Run:

```powershell
rg -n --glob '!Third-Party/**' --glob '!build/**' --glob '!dist/**' 'radio_protocol_v4|cmsis_dap_validation\.md|development_tasks\.md|firmware_modules\.md|frequency_hopping\.md|hardware_acceptance\.md|hardware_review\.md|product_architecture\.md|radio_bringup\.md|release_checklist\.md|roadmap\.md|usb_config_disk\.md|vendor_dependencies\.md|协议 v4|协议v4' .
```

Expected: 只允许设计说明中的删除清单和迁移说明保留旧路径文字；项目入口、发布脚本和手册正文不再链接旧文件或把当前协议写为 v4。

---

### Task 5: 删除已归并旧文档

**Files:**
- Delete: `docs/cmsis_dap_validation.md`
- Delete: `docs/development_tasks.md`
- Delete: `docs/firmware_modules.md`
- Delete: `docs/frequency_hopping.md`
- Delete: `docs/hardware_acceptance.md`
- Delete: `docs/hardware_review.md`
- Delete: `docs/product_architecture.md`
- Delete: `docs/radio_bringup.md`
- Delete: `docs/radio_protocol_v4.md`
- Delete: `docs/release_checklist.md`
- Delete: `docs/roadmap.md`
- Delete: `docs/usb_config_disk.md`
- Delete: `docs/vendor_dependencies.md`

**Interfaces:**
- Consumes: Task 3 的四本手册、Task 4 的引用扫描。
- Produces: 只保留有独立责任的项目文档，不删除原理图记录、变更记录、第三方声明或许可证。

- [x] **Step 1: 复核删除目标**

确认每个目标路径存在，且 `docs/project_manual.md`、`docs/hardware_manual.md`、`docs/wireless_manual.md`、`docs/development_release_manual.md` 已覆盖每个旧文件的有效内容。

- [x] **Step 2: 用补丁删除目标文件**

使用 `apply_patch` 的 `Delete File` 操作删除清单中的 13 个文件，不使用宽泛递归删除。

- [x] **Step 3: 检查文档链接和差异**

Run:

```powershell
git diff --check
rg -n --glob '*.md' '\[[^]]+\]\([^)]*\)' README.md docs CHANGELOG.md THIRD_PARTY_NOTICES.md
git status --short
```

Expected: 没有空白错误；每个项目文档链接指向现存路径；`docs/schematic_u5_connections.md` 的用户内容未被覆盖。

---

### Task 6: 运行术语检查、主机测试和构建验证

**Files:**
- Verify: `README.md`, `CHANGELOG.md`, `THIRD_PARTY_NOTICES.md`, `docs/*.md`, `firmware/app/radio_protocol.h`, `tests/radio_protocol_test.c`, `scripts/package_release.ps1`

**Interfaces:**
- Consumes: 全部前置任务。
- Produces: 文档、协议常量、测试、Release manifest 和构建产物的一致性证据。

- [x] **Step 1: 运行 cste-zh 检查**

先执行：

```powershell
python C:\Users\YSCha\.codex\skills\cste-zh\scripts\cste_lint.py --help
```

根据脚本实际参数，对根目录项目文档和 `docs/` 手册执行检查；上游 `Third-Party/CMSIS-DAP/**` 不纳入项目中文术语检查。

- [x] **Step 2: 运行完整主机测试**

```powershell
.\scripts\test_host.ps1 -Name all
```

Expected: 所有已注册主机测试退出码为 0。

- [x] **Step 3: 运行 GCC Debug/Release 构建**

```powershell
.\scripts\build_gcc.ps1 -Configuration Debug
.\scripts\build_gcc.ps1 -Configuration Release
```

Expected: 构建成功；Release `build/gcc/release/manifest.json` 的 `radio_protocol` 为 `1`，`version` 仍为 `0.8.0-rc.3`。

- [x] **Step 4: 运行发布验证（可用 Keil 时）**

```powershell
.\scripts\verify_release.ps1 -SkipKeil
```

本轮实际使用 `-SkipKeil` 完成发布验证；Keil 构建、USB 主机枚举、真实 CMSIS-DAP 调试和双板无线收发仍未执行。

Expected: 源码树、依赖、主机测试、GCC 构建、Release manifest 和可重复构建检查全部通过；若环境缺少 Arm GCC 或其他工具，记录实际阻塞，不把未运行结果写成通过。

- [x] **Step 5: 最终检查协议和旧文档引用**

```powershell
rg -n --glob '!Third-Party/**' --glob '!build/**' --glob '!dist/**' 'RADIO_PROTOCOL_VERSION\s+1U|协议版本 `01`|无线链路协议 v1|FIRMWARE_VERSION_STRING|radio_protocol_v4|协议 v4|协议v4' .
git diff --stat
git diff --check
```

Expected: 生产代码只定义 `RADIO_PROTOCOL_VERSION 1U`；项目手册描述 v1；`FIRMWARE_VERSION_STRING` 仍为 `0.8.0-rc.3`；旧手册路径只在设计说明/迁移记录中出现。
