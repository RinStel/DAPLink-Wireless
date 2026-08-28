# Changelog

## 1.0.5 - 2026-08-28

- 修复无线 ACK 发送期间拒绝后续 `SWD_BLOCK` 请求的问题；后续 CMSIS-DAP 命令可先进入可靠队列，并在 ACK `TX_DONE` 后发送。

## 1.0.4 - 2026-08-28

- 修复 SX1281 从接收切换到发送前未进入 standby 的问题，确保驱动仅在合法状态下更新发送包参数。
- 包参数缓存仅记录最后一次成功写入的当前硬件状态；TX 实际帧长与 RX 最大载荷长度交替时，驱动会重新发送 `SET_PACKET_PARAMS`。
- 修复无线主机在发送上一条 SWD 响应的 ACK 时拒绝下一条 CMSIS-DAP 命令的问题；下一条命令可以先进入可靠队列，并在 ACK `TX_DONE` 后发送。

## 1.0.2 - 2026-08-28

- 整理 `STAT_LED` 状态指示：初始化失败时红灯每 200 ms 翻转，运行错误时每 500 ms 翻转；正常运行按无线主机、无线从机和有线模式分别显示蓝色、绿色和青色，空闲、通信和 SWD 烧录时分别按 1000 ms、450 ms 和 150 ms 翻转。
- 无线从机 USB 改为仅暴露 MSC 配置磁盘，不再暴露 CDC 和 CMSIS-DAP 接口；切换设备模式后保存配置并复位，再按新模式重新枚举 USB。
- 修复 Bootloader 回退时红蓝交替指示、无线启动时短暂显示有线青色、DFU 配置文件编码和无线 ACK 转向等待时间问题。

## 1.0.1 - 2026-08-27

- 修复 SX1281 包参数缓存错误，确保发射后切回接收时重新写入当前接收载荷上限。

## 1.0.0 - 2026-08-27

- 收紧无线 SWD Sequence 的 bit_count 边界，拒绝会因 `uint8_t` 截断而形成的畸形请求，并增加最大载荷回归测试。
- 修复 Windows PowerShell 下 GCC 子工具、主机测试编译器和 Git 全局 excludes 权限警告导致的门禁不稳定问题。
- 将项目文档合并为项目、硬件、无线、开发与发布四本手册，保留 EasyEDA U5 原理图连接记录作为独立事实来源。
- 将固件版本更新为 `1.0.0`。
- 为 Bootloader DFU `28E9:1291` 增加 Microsoft OS 1.0 WCID 描述符，使 Windows 8 及以上系统自动绑定 WinUSB。
- 让 Bootloader DFU 接口报告非活动槽、加载地址、已确认版本和恢复模式，并按更新后的原理图及实板单色测试统一 `PC13/PC14/PC15` 的蓝/红/绿映射。

## 0.8.0-rc.3 - 2026-06-11

- 将空中协议定为 v1，增加可靠的无线 `SWD_ABORT` 控制帧。
- 从机先确认无线 SWD 请求再执行，并在目标 WAIT 重试期间轮询取消请求。
- 将单次 SWD WAIT 限制为 250 ms，整个隧道请求限制为 2500 ms。
- 拒绝非法的 Match Mask/Match Value 读写组合。
- 修复首次射频初始化失败后恢复时缺少新会话广播的问题。
- 为 USB 配置盘重建增加稳定断开窗口，移除失败路径中的重复重连。
- 修复 Linux CI 的厂商快照指纹和独立头文件编译问题。

## 0.8.0-rc.2 - 2026-06-09

- 修正 ResetTarget、Disconnect 和 Match Mismatch 的 CMSIS-DAP 语义。
- 限制异常 WAIT/Match Retry 时长，并封堵无线 SWD Sequence 越界入口。
- 修复畸形无线 SWD 分块响应可能导致的事务数组越界。
- 修复 GFSK 同步字有效 IRQ 可能提前重启接收的问题。
- 增加 USB 描述符、WCID、PMA 和协议截断输入测试。
- 增加配置 Flash 双副本掉电故障模拟与源码树发布指纹。
- 增加第三方许可归属、硬件验收流程和 pyOCD 冒烟脚本。
- 修复 CDC `SET_LINE_CODING` 非法长度导致的控制缓冲区覆盖风险。
- 正确声明 SWD 与独立 USB CDC COM Port 能力，不宣称支持 DAP UART 命令。
- 增加连续 Release 构建的字节级可重复性发布门禁。
- 将 CMSIS-DAP 固定为 Git submodule；GD32 V3.0.3 厂商快照由哈希锁保护。
- 移除对修改版 GD32 USB 库的隐式依赖，CDC、WCID 与 MSC 适配移入项目层。
- 拦截越界 USB 标准请求、非法 recipient/端点，并修复 CDC 单包接收延迟及控制请求校验。
- 将 GCC 工具链适配移至 `firmware/toolchain`，并强制 GCC/Keil 构建产物与 IDE 临时文件不得污染源码树。
- 统一正式项目名称为 `DAPLink-Wireless`，保留 `daplink_wireless` 作为构建产物 basename。
- 增加 GitHub Actions 软件门禁，修复 Windows/Linux 构建脚本可移植性，并将完整 GPLv3 许可证纳入发布包校验。
- 将 9 个重复的主机测试脚本合并为数据化 `test_host.ps1`，并使用 SPDX 精简脚本许可证头。

## 0.8.0-rc.1 - 2026-06-09

- 实现 CMSIS-DAP v2 Bulk、SWD 命令映射、取消与超时处理。
- 实现有线、无线主机和无线从机三种设备模式。
- 实现 SX1281 GFSK/FLRC、RSSI 回传、链路自适应和确定性跳频。
- 实现 CDC 串口参数同步与双向可靠透传。
- 实现 MSC 配置磁盘、Flash 原子配置存储和按键配置。
- 增加看门狗、复位原因、无线诊断计数和状态文件。
- 增加主机侧协议测试、严格 GCC/Keil 构建及发布产物清单。

这些版本均为工程发布候选，不是公开量产版本。
