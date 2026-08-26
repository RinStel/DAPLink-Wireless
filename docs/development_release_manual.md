# 开发与发布手册：验证、依赖和门禁

## 当前软件状态

已完成的软件阶段：

- P0 板级启动：安全 GPIO、时钟、状态灯、按键、供电控制和看门狗。
- P1 有线调试：USB FS 组合设备、CMSIS-DAP v2 Bulk、CDC 和本地 SWD。
- P2 无线底层：SX1281 GFSK/FLRC、可靠帧、RSSI、重传和链路统计。
- P3 无线 DAP：命令批处理、去重、取消、超时、串口旁路和动态跳频。
- P4 软件产品化：设备模式、同步码、MSC 配置、Flash 存储和发布门禁。

当前固件发布候选版本为 `0.8.0-rc.3`，无线链路协议版本为 `v2`。软件构建和
主机协议测试已自动化；真实硬件验证、USB VID/PID、生产测试和跨平台兼容性仍
属于发布前工作。

目标端执行完整 DAP 命令或批量 SWD 序列，主机端只传输请求和结果，不跨无线
链路传输逐 bit 时序。无线 `DATA` 使用四槽窗口，SWD Transfer 使用压缩块；无线
链路延迟和硬件吞吐仍需真实双板验收。

### USB 枚举启动时序（2026-08-26）

已定位一个会造成“连接电脑后较长时间识别不出 DAP”的软件原因。旧版 `main()` 先
调用 `serial_bridge_init()`；在无线模式下，`serial_bridge_init()` 会同步初始化 SX1281，
USB D+ 上拉因此延迟到无线初始化完成后才建立。当前启动顺序为：`board_init()`、
`usb_config_disk_init()`、`serial_bridge_init()`。这样 USB 可以先进入枚举，随后再完成
无线桥接初始化。

本次修改只证明源码中的启动顺序已调整。仍必须在真实板卡上测量上电到
`VID_28E9&PID_1290` 出现的时间，并分别排除 USB-C CC、电缆、D+/D−、R19 上拉、48 MHz
USB 时钟和供电问题。

## 主机测试与软件门禁

全部主机测试：

```powershell
.\scripts\test_host.ps1 -Name all
```

常用单项测试：

```powershell
.\scripts\test_host.ps1 -Name cmsis-dap
.\scripts\test_host.ps1 -Name radio-protocol
.\scripts\test_host.ps1 -Name swd-tunnel
.\scripts\test_host.ps1 -Name link-adaptation
```

软件发布门禁：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\verify_release.ps1 -SkipKeil
```

发布门禁脚本依次检查 Git 索引和源码树、依赖锁、主机测试、GCC Debug/Release 构建、
Release manifest、产物哈希、源码树指纹和连续 Release 构建的一致性。可用 Keil
时不使用 `-SkipKeil`，以额外验证 `firmware/project.uvprojx` 的 0 Error、0 Warning
结果。

Release manifest 必须同时记录：

- `version = 0.8.0-rc.3`；
- `radio_protocol = 2`；
- `hardware = v0.5`（与 EasyEDA `Board_V1.0` 的对应关系为 `待确认：`）；
- 构建器版本、源码树 SHA-256、源码文件数、Flash/RAM 占用和函数最大栈占用；
- ELF、HEX、BIN 的字节数和 SHA-256。

## 吞吐优化跟进门禁

2026-08-26 的性能审查将软件修复和实机验收分开处理。当前 Release 软件门禁只证明
协议、缓冲边界、构建产物和静态栈限制满足检查，不能证明实际烧录速度或无线吞吐。
USB 枚举启动顺序的源码修复已完成；Windows 实际识别延迟仍属于实机门禁。

软件阶段按以下顺序执行，并在每阶段运行对应失败回归、聚焦测试和全量主机测试：

1. CDC TX 软件队列与 UART-to-CDC 源端容量保护；
2. 无线 DATA 窗口满背压；
3. CMSIS-DAP SWD chunk 合并；
4. 异步 SWD WAIT 重试；
5. SWD DWT 读取和 SX1281 packet-params 热路径优化。

实机阶段必须另外记录：

- SWCLK 100 kHz、1 MHz、2 MHz、4 MHz 的实际频率、占空比和烧录/校验时间；
- 无线 64/110 字节负载、window=1/4、立即/延迟 ACK 下的 goodput、重传率和 CRC 错误；
- UART 3 Mbps 双向 PRBS 的端到端 CRC、DMA/软件环溢出和 USB 重置计数；
- 长 SWD WAIT、Abort、目标读回 CRC 和双板跳频恢复结果。

在未取得硬件证据时，主机测试、GCC 构建和 Release manifest 不得作为 SWCLK、烧录速度、
无线 goodput、UART/CDC 完整性或 USB 跨平台枚举的替代验收。

## CMSIS-DAP v2 验证

### 协议基线

项目以 `Third-Party/CMSIS-DAP` 的 Arm CMSIS-DAP 为协议基线：

- 上游：`https://github.com/ARM-software/CMSIS-DAP.git`
- 固定提交：`6256803b7ac93731ec22e24e0ae8d91df3a7c953`
- 描述：`v2.1.2-1-g6256803`

当前固件已实现 Info、Connect、Disconnect、TransferConfigure、Transfer、
TransferBlock、TransferAbort、WriteABORT、Delay、ResetTarget、SWJ Pins、
SWJ Clock、SWJ Sequence、SWD Configure 和 SWD Sequence。Transfer 支持
Match Value 与 Match Mask。

固件不声明 JTAG、SWO、Atomic Commands 和 Transfer Timestamp。Capabilities
只声明 SWD 和独立 USB CDC COM Port，不声明基于 DAP 命令的 UART Transport。
单次 SWD WAIT 重试预算为 250 ms，整个无线隧道请求预算为 2500 ms，Match Retry
上限为 128；返回值仍使用标准 WAIT 或 MISMATCH 状态。

主机侧 CMSIS-DAP 回归：

```powershell
.\scripts\test_host.ps1 -Name cmsis-dap
```

CMSIS-DAP 主机测试覆盖产品版本和能力查询、填充后的 64 字节 Bulk 请求、无效命令、连接、
普通读、匹配传输、SWD Sequence、Abort、未支持时间戳和超时。

真实设备验证步骤：

1. 将两台设备配置为 `WIRELESS_HOST` 和 `WIRELESS_SLAVE`，同步码一致。
2. 从机连接 Cortex-M 目标，首先使用 100 kHz SWD 时钟。
3. 主机连接 Windows，确认 CMSIS-DAP v2 接口通过 WCID 绑定 WinUSB。
4. 使用 Keil、pyOCD 和 OpenOCD 执行连接、内存读写、下载、复位、断点和单步。
5. 打开 `Third-Party/CMSIS-DAP/Firmware/Validation/MDK5/Validation.uvprojx`，
   运行 Arm 官方测试。

官方 Validation 必须连接真实目标 MCU，不能由本机协议测试替代。

## 开发任务状态

### CMSIS-DAP v2 审查修复

- [x] 修复 USB OUT request/abort 缓冲区识别。
- [x] 接受填充后的 Bulk 包中的 CMSIS-DAP 命令。
- [x] 使 SWD WAIT 重试可取消且有时间边界。
- [x] 使用确定性会合替换独立空闲频道扫描。
- [x] 未知命令返回标准 `ID_DAP_Invalid` 响应。
- [x] 实现 `DAP_SWD_Sequence`。
- [x] 实现 Transfer Match Value 和 Match Mask 语义。
- [x] 在能力位保持清零的同时记录 Timestamp 不支持。
- [x] 增加 SWD 隧道协议帧的主机回归测试。

### 硬件验证

- [ ] 在真实 Cortex-M 目标上运行 Arm CMSIS-DAP Validation。
- [ ] 在目标持续返回 WAIT 时验证 Transfer Abort 延迟。
- [ ] 验证两台无线设备从不同受阻频道启动后重新会合。

### 发布加固

- [x] 集中管理固件版本和 USB device release number。
- [x] 增加独立看门狗并报告上一次复位原因。
- [x] 将异常 SWD 时钟请求限制到安全下限。
- [x] 生成带 Flash/RAM 门限的 BIN 和 SHA-256 manifest。
- [ ] 在硬件上验证看门狗恢复。
- [x] 增加 `STATUS.TXT`，并在每次应用结果后重新挂载磁盘。
- [x] 增加带消抖的短按/长按配置和原子持久化。
- [x] 在 CRC、同步字和 RX timeout IRQ 后重启接收。
- [x] 将 TX timeout 视为射频失败并自动恢复。
- [x] 将已接受流量绑定到当前对端 session。
- [x] 无线主机重启时丢弃从机侧过期响应。
- [x] 增加 CMSIS-DAP 命令、填充、Abort 和超时主机测试。
- [x] 增加无线协议和确定性跳频测试。
- [x] 增加统一的发布验证和打包流程。
- [x] 增加栈、内存、哈希和依赖路径发布门禁。
- [x] 增加 pyOCD 硬件冒烟测试和正式验收流程。
- [x] 对齐 ResetTarget、Disconnect 和 Match Mismatch 的 Arm 语义。
- [x] 限制无线 SWD 重试时间并拒绝过大的 SWD Sequence。
- [x] 在目标 WAIT 重试期间将 TransferAbort 传递到无线从机。
- [x] 将单个 SWD 请求限制在 CMSIS-DAP 和无线协议截止时间以内。
- [x] 配置盘重建时保持足够的 USB 断开窗口。
- [x] 对纯协议和配置模块运行 GCC 静态分析。
- [x] 在主机验证 USB 组合描述符、WCID 和 PMA 分配。
- [x] 模拟 Flash 擦除/写入/提交失败和 CRC 恢复。
- [x] 在发布 manifest 中增加可复现的源码树指纹。
- [x] 在缓冲区设置前拒绝非法 CDC line-coding 控制请求。
- [x] 以固定提交管理 CMSIS-DAP Git submodule。
- [x] 用树哈希锁定不可变的 GD32 V3.0.3 厂商快照。
- [x] 从修改后的厂商 USB 结构和回调中移除项目行为。
- [x] 保护厂商 USB standard-request dispatch table。
- [x] 拒绝非法 USB standard-request interface 和 endpoint 索引。
- [x] 校验 CDC 控制请求的方向、recipient、value 和 length。
- [x] 将 GCC/Keil 输出置于 `firmware/` 外，并在构建前后强制源码树洁净。

## 发布检查清单

### 软件门禁

- [x] CMSIS-DAP v2、无线协议、SX1281 驱动、配置、链路自适应、USB 描述符和磁盘测试通过。
- [x] GCC Debug 与 Release 使用 `-Wall -Wextra -Werror` 构建通过。
- [x] Keil 工程零错误、零警告构建通过。
- [x] Flash、RAM 和单函数静态栈占用设有构建门限。
- [x] 配置 Flash 双副本通过写入中断、提交失败、CRC 损坏和回退模拟。
- [x] ELF、HEX、BIN 及 SHA-256 清单可重复生成。
- [x] 连续两次 Release 构建的 ELF、HEX、BIN 和 manifest 字节一致。
- [x] 固件版本、无线协议版本和硬件版本写入发布清单。
- [x] 发布清单记录构建器版本和可独立复算的源码树 SHA-256 指纹。
- [x] CMSIS-DAP 由固定提交的 Git submodule 管理。
- [x] GD32 V3.0.3 作为厂商快照保存在 `vendor/`，目录树哈希已锁定。
- [x] Git 索引拒绝 PDF、构建产物、错误子模块形态和锁外厂商文件。
- [x] GitHub Actions 使用固定工具链执行无 Keil 软件门禁并保存构建产物。
- [x] 项目适配代码不修改或复制依赖实现。
- [x] 无线 TransferAbort 可在目标 WAIT 重试期间传递到从机。
- [x] 单个 SWD 隧道请求总执行时间低于 DAP 和无线协议超时。
- [x] 配置盘重建时提供稳定的 USB 断开窗口后再枚举。
- [x] 发布包包含 GigaDevice 和 Arm CMSIS-DAP 的第三方许可与归属声明。
- [x] 发布包包含项目 GPLv3 许可证，并校验包内许可证与源码一致。

### 实机门禁

- [ ] 在真实 Cortex-M 目标上通过 Arm CMSIS-DAP Validation。
- [ ] 使用 Keil、pyOCD 和 OpenOCD 验证下载、断点、单步和复位。
- [ ] 验证目标持续返回 WAIT 时的 Transfer Abort 延迟。
- [ ] 验证两台设备从不同受阻频道启动后能够重新会合。
- [ ] 验证 GFSK/FLRC 和全部空中速率的切换与回退。
- [ ] 验证看门狗、掉电写入和异常断链恢复。
- [ ] 完成至少 24 小时无线调试与串口并发压力测试。
- [ ] 完成 Windows、Linux 和 macOS 的 USB 枚举及恢复测试。

### 发布合规

- [ ] 替换 GD32 示例 USB VID/PID，并确认分配和驱动发布方式。
- [x] 项目采用 GNU GPL v3.0 或更高版本，并包含顶层 `LICENSE`。
- [ ] 在正式 Git 仓库中创建带签名或可追溯提交的版本标签。
- [ ] 保存硬件版本、BOM、生产测试和校准记录。

只有全部项目完成后，才能将发布状态从 `release-candidate` 改为 `production`。

## 官方依赖与发布输入

### 工程内依赖

- GD32F30x CMSIS V3.0.3。
- GD32F30x 标准外设库 V3.0.3。
- GD32F30x USB Device 库。
- Arm CMSIS-DAP，固定提交 `6256803b7ac93731ec22e24e0ae8d91df3a7c953`。

GD32F30x V3.0.3 官方发布包快照位于 `vendor/`；项目代码不得修改这些文件。
`dependencies.lock.json` 记录三个参与构建目录的 Git 索引对象树指纹，不受
Windows/Linux checkout 换行格式影响。发布验证会拒绝已修改、未跟踪、增删或
替换的厂商文件。

`GD32F30x_usbfs_library` 不属于项目依赖，也不保留在工作区；固件只使用锁定的
`GD32F30x_usbd_library`。`Third-Party/CMSIS-DAP` 只作为协议基线和官方
Validation 工程，不直接编入固件。

### 外部设计参考

以下资料用于设计参考，不提交到 Git 仓库；开发者需从厂商官方网站取得对应版本：

- GD32F303xx 数据手册 Rev3.3 和用户手册 Rev3.4。
- Semtech SX1280/SX1281 数据手册 V3.3。
- E28-2G4M20S 用户手册 V1.6；`E28-2G4M20SX` 电气和软件兼容性为 `待确认：`。

### 正式发布前仍需提供或确认

- 可合法用于产品发布的 USB VID/PID。
- 一套真实 Cortex-M 目标板，用于 Arm Validation、Keil、pyOCD 和 OpenOCD。
- 两块完整无线调试器硬件，用于跳频、吞吐、掉电和长稳验证。
- 量产所需的生产测试接口、射频法规目标地区和校准要求。
