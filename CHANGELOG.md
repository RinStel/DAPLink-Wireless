# Changelog

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
