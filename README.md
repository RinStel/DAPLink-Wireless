# DAPLink-Wireless

基于两块相同硬件构成的无线 CMSIS-DAP v2 调试器。主控为 `GD32F303CCT6`，无线模块为
基于 SX1281 的 `E28-2G4M20SX`。当前固件版本为 `1.0.0`，无线链路协议为 `v2`。

## 功能

- 有线模式：USB CMSIS-DAP v2、CDC 串口和目标板直连。
- 无线主机：将 CMSIS-DAP 和 CDC 请求发送到无线从机。
- 无线从机：执行本地 SWD，并与目标 UART 双向透传。
- GFSK/FLRC 动态速率、自适应链路和确定性跳频。
- 16 位字母/数字同步码隔离设备组。
- USB MSC 配置盘、独立看门狗、复位原因和链路诊断统计。

## 快速开始

初始化 CMSIS-DAP submodule：

```powershell
git submodule update --init --recursive
```

构建 GCC Release 固件：

```powershell
cmake --preset release
cmake --build --preset release
```

运行主机测试或完整软件发布门禁：

```powershell
.\scripts\test_host.ps1 -Name all
powershell -ExecutionPolicy Bypass -File .\scripts\verify_release.ps1
```

构建输出位于 `build/gcc/release/`。CMake 是唯一固件构建入口；CLion 可直接打开仓库
根目录并选择 `release` CMake profile。

## 部署与升级

- `daplink_factory.hex`：首次通过 SWD 部署，包含 Bootloader、Slot A 和初始 Boot 状态。
- `daplink_wireless.dwup`：已有 Bootloader 时通过 USB DFU 在线升级。
- 应用通过 MSC 配置盘的 `CONFIG.TXT` 字段 `ENTER_DFU=1` 进入 DFU；上电按住 `KEYA`
  是救砖入口。
- `.dwup` 必须交给 `tools/daplink_updater.py`，不得直接交给 `dfu-util --download`。

首次烧录、pyOCD 参数、CLion 烧录目标、USB DFU 命令和实板验收要求见
[开发与发布手册](docs/development_release_manual.md)。

## 文档

- [项目手册](docs/project_manual.md)：产品架构、固件模块和 USB 配置。
- [硬件手册](docs/hardware_manual.md)：原理图基线、GPIO、上电和硬件验收。
- [无线手册](docs/wireless_manual.md)：协议 v2、窗口 ACK、跳频和射频验证。
- [开发与发布手册](docs/development_release_manual.md)：构建、烧录、升级和发布门禁。
- [U5 原理图连接记录](docs/schematic_u5_connections.md)：原理图网络事实和代码对照。
- [变更记录](CHANGELOG.md)
- [第三方声明](THIRD_PARTY_NOTICES.md)

硬件版本对应关系和未确认事项以硬件手册、原理图连接记录中的 `待确认：` 标记为准。
