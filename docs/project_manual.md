# 项目手册：产品、固件与 USB 配置

## 项目定位

DAPLink-Wireless 使用两块相同硬件构成无线 CMSIS-DAP v2 调试器。主控为
`GD32F303CCT6`，无线模块为基于 SX1281 的 `E28-2G4M20SX`。项目提供有线、
无线主机和无线从机三种设备模式，并通过 USB 同时提供 CMSIS-DAP v2 Bulk、
CDC ACM 虚拟串口和 MSC 配置磁盘。

当前状态：固件发布候选版本为 `0.8.0-rc.3`，无线链路协议版本为 `v2`，
无线帧头偏移 2 的协议字段为 `02`。`FIRMWARE_VERSION_STRING` 与
`RADIO_PROTOCOL_VERSION` 是独立标识。现有发布文档使用硬件版本 `v0.5`；
EasyEDA 当前读取到的 `Board_V1.0` 与项目文档中的 `v0.5` 对应关系为 `待确认：`。

项目只提供 CMSIS-DAP v2 Bulk 接口，不兼容 CMSIS-DAP v1 HID。

## 快速构建与验证

初始化 CMSIS-DAP submodule：

```powershell
git submodule update --init --recursive
```

GD32F30x V3.0.3 保存在 `vendor/` 下的官方快照中，不使用 submodule。
`dependencies.lock.json` 锁定 submodule 提交和厂商快照哈希。

运行全部主机测试或单项测试：

```powershell
.\scripts\test_host.ps1
.\scripts\test_host.ps1 -Name radio-protocol
```

运行软件发布门禁：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\verify_release.ps1
```

`verify_release.ps1` 检查源码树、依赖、主机测试、GCC Debug/Release 构建、产物哈希、
栈占用和连续 Release 构建的一致性。安装 Keil 后还会构建
`firmware/project.uvprojx`；Keil 零错误、零警告构建仍由本地发布流程负责。

只构建 GCC 固件：

```powershell
.\scripts\build_gcc.ps1 -Configuration Debug
.\scripts\build_gcc.ps1 -Configuration Release
```

生成发布候选包：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\package_release.ps1
```

构建输出位于 `build/gcc`，发布包位于 `dist`。GCC 和 Keil 的中间文件不得写入
`firmware/`。

## 设备模式与数据流

| 模式 | 数据路径 |
| --- | --- |
| `WIRED` | USB CDC 与目标 USART0 直接透传；USB CMSIS-DAP 请求在本板执行 |
| `WIRELESS_HOST` | USB CMSIS-DAP 和 CDC 请求经 SX1281 发送到无线从机 |
| `WIRELESS_SLAVE` | 无线请求在本板执行 SWD，并与目标 USART0 双向透传 |

主机拥有无线链路的控制权，从机不主动修改 profile、频道或串口参数。串口数据
和行参数使用递增序号、ACK、超时重传和重复帧抑制；无线单帧最多承载 110 字节。
`DATA` 使用四槽窗口和累计 ACK，SWD Transfer 使用压缩 `SWD_BLOCK`。

PC 打开 CDC ACM 虚拟串口时发送 `SET_LINE_CODING`。有线模式立即将参数应用到
USART0；无线主机通过可靠控制帧同步给无线从机。因此不使用自动波特率探测。

## 固件模块

固件使用单主循环和非阻塞状态机，不使用 RTOS。

| 层级 | 模块 | 责任 |
| --- | --- | --- |
| 调度 | `main.c` | 初始化并运行 USB、无线和指示灯服务 |
| 调度 | `serial_bridge.c` | 连接无线链路状态机和业务服务，不解析帧字段 |
| 协议 | `radio_protocol.c` | 无线帧编解码、字段校验和重复帧键 |
| 业务 | `serial_service.c` | CDC/UART 参数转换和有线/无线串口透传 |
| 业务 | `swd_bridge_service.c` | SWD 请求生命周期、响应缓存、取消和重复回复 |
| 业务 | `swd_tunnel.c` | SWD 无线负载格式及异步 `SWJ_Pins` |
| 业务 | `cmsis_dap.c` | CMSIS-DAP 命令状态机 |
| 链路 | `link_adaptation.c` | RSSI EWMA、升降速投票和驻留时间 |
| 链路 | `frequency_hopping.c` | 频道表、同步码派生排列和坏频道惩罚 |
| 驱动 | `sx128x.c`、`radio_hal.c` | SX1281 命令及 SPI/GPIO |
| 驱动 | `target_uart.c`、`target_uart_ring.c` | USART0 DMA 环形缓冲区和 TX 分段 |
| 驱动 | `target_swd.c` | SWCLK、SWDIO 和 NRST 时序 |
| 板级 | `board.c` | GPIO、SysTick、DWT 时基和设备 ID |
| USB | `usb_composite.c` | MSC、CDC、CMSIS-DAP v2 组合描述符 |
| USB | `cmsis_dap_usb.c` | CMSIS-DAP v2 Bulk 传输适配 |
| 配置 | `usb_config_disk.c` | 虚拟 FAT12 配置盘 |
| 配置 | `device_config.c`、`device_config_storage.c` | 运行配置和 Flash 双槽持久化 |

## USB 组合设备

USB FS 枚举为 MSC + CDC ACM + CMSIS-DAP 组合设备：

- MSC 暴露 `CONFIG.TXT` 和 `STATUS.TXT`。
- CDC ACM 提供目标串口虚拟端口。
- CMSIS-DAP v2 使用接口 3 的 64 字节 Bulk OUT/IN 端点。
- USB OUT 使用背压，保证只有一个未完成 DAP 命令。
- Windows 8 及以上可通过 Microsoft OS 1.0 WCID 自动为 v2 接口绑定 WinUSB。
- USBFS PMA 为 512 字节；MSC bulk 端点使用 16 字节包，CDC 数据端点和 DAP v2
  端点使用 64 字节包。MSC 配置盘仍然保留，逻辑介质块仍为 512 字节。

量产前必须替换 GD32 示例 VID/PID，并验证 Windows、Linux 和 macOS 的枚举、
休眠恢复和安全弹出行为。

## USB 配置磁盘

设备枚举出的虚拟磁盘为 16 KiB FAT12 设备。`CONFIG.TXT` 示例：

```text
SYNC=DAPLINKWIRELESS1
MODE=WIRELESS_HOST
PROFILE=AUTO
```

配置约束：

- `SYNC` 必须是 16 个 ASCII 字母或数字。
- `MODE` 为 `WIRED`、`WIRELESS_HOST` 或 `WIRELESS_SLAVE`。
- `PROFILE` 为 `AUTO`，或 `GFSK2M`、`GFSK1M`、`GFSK500K`、`FLRC1M3`、`FLRC650K`。

保存后必须使用系统的“弹出”或“安全删除硬件”。固件在对应 SCSI 命令的状态
包发送完成后提交配置，不使用固定超时强制断开仍处于挂载状态的磁盘。校验通过
后，设备断开 USB，交替写入 MCU 最后两个 2 KiB Flash 页；记录包含递增序号、
CRC32 和提交标记。写入失败或格式错误时继续使用上一份有效配置。

`STATUS.TXT` 显示当前固件版本、上次配置应用结果和复位原因。配置成功或失败
后设备都会重新枚举配置盘；失败时 `CONFIG.TXT` 恢复为仍在使用的旧配置。

板载按键通过同一双槽 Flash 事务修改配置：短按依次切换自动速率和固定速率，
长按 2 秒依次切换 `WIRED`、`WIRELESS_HOST`、`WIRELESS_SLAVE`。SWD 事务进行
中禁止修改配置。`SW1`/`SW2` 的最终产品分工见[硬件手册](hardware_manual.md)
中的 `待确认：` 项。

## CMSIS-DAP 能力边界

当前前端已映射 `DAP_Info`、`DAP_Connect`、`DAP_Disconnect`、`DAP_Transfer`、
`DAP_TransferBlock`、`DAP_WriteABORT`、`DAP_SWJ_Clock`、`DAP_SWJ_Sequence`、
`DAP_SWD_Configure`、`DAP_SWD_Sequence` 和 `DAP_ResetTarget`。Transfer 支持
Match Value 与 Match Mask。

JTAG、SWO 和时间戳尚未实现，能力位不会声明这些功能。`DAP_TransferAbort`
使用独立 USB OUT 缓冲作为带外命令；无线 SWD 操作按 transaction ID 过滤，
迟到响应仅被链路 ACK，不会进入后续调试事务。

诊断命令为 `DAP Vendor 0x80`，响应格式版本为 5；完整字节布局见
[无线手册](wireless_manual.md)和源码 `firmware/app/cmsis_dap.c`。

## 相关手册

- [硬件手册](hardware_manual.md)：原理图基线、当前 GPIO 差异、上电与实机验收。
- [无线手册](wireless_manual.md)：协议 v2、窗口 ACK、profile、跳频和射频冒烟测试。
- [开发与发布手册](development_release_manual.md)：测试、依赖、发布门禁和 CMSIS-DAP 验证。
- [U5 原理图连接记录](schematic_u5_connections.md)：EasyEDA 读取的完整网络事实。
