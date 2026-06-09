# CMSIS-DAP v2 验证

## 协议基线

项目以 `Third-Party/CMSIS-DAP` 的 Arm CMSIS-DAP 为协议基线：

- 上游：`https://github.com/ARM-software/CMSIS-DAP.git`
- 提交：`6256803b7ac93731ec22e24e0ae8d91df3a7c953`
- 描述：`v2.1.2-1-g6256803`

当前固件版本为 `0.8.0-rc.2`。已实现 SWD 调试所需的 Info、Connect、
Disconnect、TransferConfigure、Transfer、TransferBlock、TransferAbort、
WriteABORT、Delay、ResetTarget、SWJ Pins、SWJ Clock、SWJ Sequence、
SWD Configure 和 SWD Sequence。Transfer 支持 Match Value 与 Match Mask。

固件不声明 JTAG、SWO、Atomic Commands 和 Transfer Timestamp 能力。
Capabilities 仅声明 SWD 和独立 USB CDC COM Port；不声明基于 DAP 命令的
UART Transport。
为保证无线从机不会被恶意或异常的主机参数长期阻塞，单次 SWD WAIT 重试
设有 1 秒时间预算，Match Retry 上限为 128；返回值仍使用标准 WAIT 或
MISMATCH 状态。

## 主机侧回归

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\test_host.ps1 -Name cmsis-dap
```

该测试覆盖产品版本和能力查询、填充后的 64 字节 Bulk 请求、无效命令、连接、
普通读、匹配传输、SWD Sequence、Abort、未支持时间戳及超时。

## 实机验证

1. 将两台设备配置为 `WIRELESS_HOST` 和 `WIRELESS_SLAVE`，同步码保持一致。
2. 从机连接 Cortex-M 目标，首先使用 100 kHz SWD 时钟。
3. 主机连接 Windows，确认 CMSIS-DAP v2 接口通过 WCID 绑定 WinUSB。
4. 使用 Keil、pyOCD 和 OpenOCD 执行连接、内存读写、下载、复位、断点和单步。
5. 打开 `Third-Party/CMSIS-DAP/Firmware/Validation/MDK5/Validation.uvprojx`
   并运行 Arm 官方测试。

官方 Validation 必须连接真实目标 MCU，不能由本机协议测试替代。
