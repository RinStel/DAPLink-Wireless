# DAPLink-Wireless

基于两块相同硬件构成的无线 CMSIS-DAP v2 调试器。主控为 `GD32F303CCT6`，
无线模块为基于 SX1281 的 `E28-2G4M20SX`。

当前固件版本为 `1.0.0`，无线链路协议为 `v2-only`。当前 EasyEDA 记录中的
`Board_V1.0` 与项目文档硬件
版本 `v0.5` 的对应关系为 `待确认：`。

## 功能

- 有线模式：USB CMSIS-DAP v2、CDC 串口与目标板直接连接。
- 无线主机：将 USB CMSIS-DAP v2 和 CDC 串口请求发送至无线从机。
- 无线从机：执行本地 SWD 操作，并与目标 UART 双向透传。
- GFSK/FLRC 动态速率、自适应链路和确定性跳频。
- 16 位字母/数字同步码隔离不同设备组。
- USB 虚拟磁盘通过 `CONFIG.TXT` 修改配置，并原子写入 Flash。
- 独立看门狗、复位原因和链路诊断统计。

## USB DFU 更新

应用默认以 USB MSC 配置盘提供 `CONFIG.TXT`。写入完整文件中的精确字段
`ENTER_DFU=1`，安全弹出后应用会断开并复位，重新枚举为 DFU `28E9:1291`；
`ENTER_DFU` 不会保存到配置 Flash。上电按住恢复键仍可直接进入恢复 DFU，适合
救砖和允许降级。正常 DFU 只接受严格递增的 `FIRMWARE_VERSION_CODE`，恢复模式
允许同版本或降级，但两者都检查 GD32F303CC、槽地址、长度、向量表和 CRC32。

分目标产物位于 `build/gcc/release/daplink_bootloader`、`daplink_slot_a` 和
`daplink_slot_b`。Release 根目录另外生成 `daplink_wireless.dwup` 和
`daplink_factory.hex`。这些发布产物固定由 GCC/CMake 生成；首次部署使用外部 SWD
工具按分区烧录，不写 Slot B 和
`0x0803F000-0x0803FFFF` 配置页。

当前推荐使用 pyOCD 进行首次 SWD 部署。pyOCD 的 `stm32f103rc` 是 GD32F303 的兼容
目标描述；烧录后必须执行回读和复位检查，不能把目标显示为 STM32 当作芯片身份确认。

```powershell
pyocd list --probes
powershell -ExecutionPolicy Bypass -File .\scripts\flash_pyocd.ps1 `
    -Artifact .\build\gcc\release\daplink_factory.hex `
    -Target stm32f103rc -Connect under-reset -Frequency 1M -Erase sector
```

CLion 的 Release profile 会显示同样的 `flash_factory`、`flash_bootloader`、
`flash_slot_a` 和 `flash_slot_b` 非默认目标。可以在 CMake profile 中设置
`PYOCD_PROBE`、`PYOCD_FREQUENCY`、`PYOCD_CONNECT` 和 `PYOCD_ERASE`，再从 Build
目标列表运行；普通 Build 不会自动烧录。

Windows 8 及以上系统通过 Bootloader 的 Microsoft OS 1.0 WCID 描述符自动为
`28E9:1291` 绑定 WinUSB。正常使用不需要 Zadig 或手动选择驱动。主机只需安装
`dfu-util`，然后运行升级工具：

```powershell
python tools/daplink_updater.py build/gcc/release/daplink_wireless.dwup --volume E:\
```

下载完成后 Bootloader 在最终 `GETSTATUS` 响应后绿灯约 500 ms、自动复位并试运行
新槽；应用只在首次完整启动循环确认一次。试运行连续三次未确认会回退旧槽，LED
状态为：蓝色等待/写入、青色校验、绿色成功、红色错误、红蓝交替回退。等待和错误
状态不设空闲超时，拔插 USB 可重新进入 DFU。

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

发布门禁固定使用 CMake 驱动的 GCC 交叉工具链。

只构建 GCC Debug/Release 固件（CLion 可直接打开仓库根目录并选择同名 CMake
profile）：

```powershell
.\scripts\build_gcc.ps1 -Configuration Debug
.\scripts\build_gcc.ps1 -Configuration Release
```

也可以直接使用 CMake：

```powershell
cmake --preset debug
cmake --build --preset debug
cmake --preset release
cmake --build --preset release
```

输出位于 `build/gcc`。GD32F30x V3.0.3 以厂商快照形式保留在 `vendor/`；
`dependencies.lock.json` 锁定 submodule 提交和厂商快照哈希。

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
