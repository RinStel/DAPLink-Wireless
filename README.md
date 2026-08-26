# DAPLink-Wireless

基于两块相同硬件构成的无线 CMSIS-DAP v2 调试器。主控为 `GD32F303CCT6`，
无线模块为基于 SX1281 的 `E28-2G4M20SX`。

当前固件发布候选版本为 `0.8.0-rc.3`，无线链路协议为 `v2-only`。固件发布版本和
无线协议版本是独立标识；当前 EasyEDA 记录中的 `Board_V1.0` 与项目文档硬件
版本 `v0.5` 的对应关系为 `待确认：`。

## 功能

- 有线模式：USB CMSIS-DAP v2、CDC 串口与目标板直接连接。
- 无线主机：将 USB CMSIS-DAP v2 和 CDC 串口请求发送至无线从机。
- 无线从机：执行本地 SWD 操作，并与目标 UART 双向透传。
- GFSK/FLRC 动态速率、自适应链路和确定性跳频。
- 16 位字母/数字同步码隔离不同设备组。
- USB 虚拟磁盘通过 `CONFIG.TXT` 修改配置，并原子写入 Flash。
- 独立看门狗、复位原因和链路诊断统计。

项目仅提供 CMSIS-DAP v2 Bulk 接口，不兼容 CMSIS-DAP v1 HID。

## 快速开始

初始化 CMSIS-DAP submodule：

```powershell
git submodule update --init --recursive
```

运行主机测试：

```powershell
.\scripts\test_host.ps1 -Name all
```

运行软件发布门禁：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\verify_release.ps1
```

只构建 GCC Debug/Release 固件：

```powershell
.\scripts\build_gcc.ps1 -Configuration Debug
.\scripts\build_gcc.ps1 -Configuration Release
```

生成发布候选包：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\package_release.ps1
```

输出位于 `build/gcc`，发布包位于 `dist`。GD32F30x V3.0.3 以厂商快照形式保留
在 `vendor/`；`dependencies.lock.json` 锁定 submodule 提交和厂商快照哈希。

## 文档

从[项目文档索引](docs/README.md)开始：

- [项目手册](docs/project_manual.md)：产品架构、固件模块和 USB 配置。
- [硬件手册](docs/hardware_manual.md)：原理图基线、GPIO 差异、上电和验收。
- [无线手册](docs/wireless_manual.md)：协议 v2-only、跳频和射频验证。
- [开发与发布手册](docs/development_release_manual.md)：测试、依赖和发布门禁。
- [U5 原理图连接记录](docs/schematic_u5_connections.md)：EasyEDA 网络事实与当前代码差异。
- [变更记录](CHANGELOG.md)
- [第三方声明](THIRD_PARTY_NOTICES.md)

U5 GPIO 已按当前原理图同步到固件；真实板卡上的电平、时序和器件行为仍须按
硬件手册中的上电与验收步骤实测。未由原理图、代码或测试证明的事项继续标记为
`待确认：`。
