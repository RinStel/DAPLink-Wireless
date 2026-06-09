# DAPLink-Wireless

基于两块相同硬件构成的无线 CMSIS-DAP v2 调试器。硬件版本为
`v0.5`，主控为 GD32F303CCT6，无线模块为基于 SX1281 的
E28-2G4M20SX。

## 功能

- 有线模式：USB CMSIS-DAP v2、CDC 串口与目标板直接连接。
- 无线主机：将 USB CMSIS-DAP v2 和 CDC 串口请求发送至无线从机。
- 无线从机：执行本地 SWD 操作，并与目标 UART 双向透传。
- GFSK/FLRC 动态速率、自适应链路和确定性跳频。
- 16 位字母/数字同步码隔离不同设备组。
- USB 虚拟磁盘通过 `CONFIG.TXT` 修改配置，并原子写入 Flash。
- 独立看门狗、复位原因和链路诊断统计。

项目仅提供 CMSIS-DAP v2 Bulk 接口，不再兼容 CMSIS-DAP v1 HID。

## 构建与验证

克隆后先初始化 CMSIS-DAP submodule：

```powershell
git submodule update --init --recursive
```

GD32F30x V3.0.3 保留为 `vendor/` 下的厂商快照，不使用 submodule。
`dependencies.lock.json` 锁定 submodule 提交和厂商快照哈希，构建前会自动
检查依赖是否缺失或被修改。

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\verify_release.ps1
```

该命令依次运行 Git 索引与源码树洁净性检查、主机侧协议测试、
GCC Debug/Release 严格构建、产物哈希和栈占用检查。安装 Keil 后还会构建
`firmware/project.uvprojx`，所有 GCC/Keil 输出均写入 `build/`。
GitHub Actions 在 `windows-2022` 上使用 Arm GNU 13.3.Rel1 执行同一软件
门禁并上传固件产物；Keil 零警告构建仍由本地发布流程负责。

生成发布候选包：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\package_release.ps1
```

构建输出位于 `build/gcc`，发布包位于 `dist`。

## 配置

设备枚举出的虚拟磁盘包含 `CONFIG.TXT` 和 `STATUS.TXT`。配置项包括设备
模式、同步码、调制方式、空中速率及串口参数。短按按键切换空中速率，长按
两秒切换设备模式；SWD 事务进行中禁止修改配置。

具体协议、配置格式和实机验证步骤参见[项目文档](docs/README.md)。

## 发布状态

当前版本为 `0.8.0-rc.2` 工程发布候选。默认 GD32 示例 VID/PID 尚未替换，
项目许可证尚未确定，CMSIS-DAP 官方 Validation 和无线长稳测试也需要真实
硬件完成。因此当前产物不能作为公开量产版本分发，完整门禁见
`docs/release_checklist.md`。

## 许可证

项目自身代码目前尚未授予开源许可证，保留全部权利。未经版权所有者明确
许可，不得复制、修改或分发。`vendor/` 和 `Third-Party/` 中的依赖分别遵循
其自身许可证，详见 `THIRD_PARTY_NOTICES.md`。
