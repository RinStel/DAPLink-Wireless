# 硬件发布验收

## 准备

- 两块 v0.5 无线调试器，分别配置为 `WIRELESS_HOST` 和
  `WIRELESS_SLAVE`，同步码一致。
- 一块已知正常的 Cortex-M 目标板。
- 主机安装 pyOCD、OpenOCD、Keil 和 Arm CMSIS-DAP Validation。
- 首次测试关闭调试器的目标供电输出，由目标板独立供电并共地。

## 基础冒烟

连接无线主机到 PC、无线从机到目标板，然后运行：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\hardware_smoke.ps1 `
    -Target <pyocd-target-name>
```

脚本以 100 kHz SWD 连接，读取 Cortex-M CPUID，复位目标后再次读取。多探针
环境使用 `-Probe <unique-id>`。

## 发布签核

每项记录固件 SHA-256、两台设备序列号、目标 MCU、工具版本和结果：

1. 有线模式完成连接、下载、复位、断点、单步及内存读写。
2. 无线模式重复相同步骤，并覆盖全部 GFSK/FLRC profile。
3. 使用 Keil、pyOCD、OpenOCD 和 Arm Validation 分别验证。
4. 目标持续返回 WAIT 时发送 Abort，记录最坏响应时间。
5. 屏蔽当前信道，确认两台设备在不同起始信道下重新会合。
6. 通信过程中切换速率，确认无重复写、无错误复位、无会话串台。
7. 并发运行 SWD 下载和 CDC 串口流量至少一小时。
8. 连续运行 24 小时，记录重试、恢复、超时、无效帧和看门狗计数。
9. 配置写入时断电，确认保留上一份或完整的新配置。
10. Windows、Linux、macOS 分别验证 USB 枚举、休眠恢复和安全弹出配置盘。

全部原始日志应随正式版本标签归档。任何失败都必须形成可复现问题，不能只
在清单中标记为“偶发”。
