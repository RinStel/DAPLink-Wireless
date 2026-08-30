# 交接文档：CDC DATA 通路停摆调查 + 射频吞吐路线（2026-08-30 深夜）

**给下一个 Agent 的自包含交接。** 上一会话完成了问题审计、交替流水线重写
（5.3 → 10.6 kB/s）、全部提交整理，并实现 CDC 回显吞吐测试时撞上一个真实的
USB CDC bug，调查进行到一半。本文档记录全部现场状态，所有内容已提交。

## 1. 仓库与版本状态

### 提交历史（main，未推送，工作树干净）

| 提交 | 内容 |
| --- | --- |
| `e727067` | fix(tests): CDC 传输诊断改走 DAP_DIAG 宏 + 测试包含路径 |
| `431d40d` | feat(usb): CDC 回显吞吐基准 + 端点停摆插桩（1.0.53-1.0.55） |
| `247d83b` | feat(radio): 交替 SWD 流水线 + 8 深窗口 + 死代码清理（1.0.49-1.0.52） |
| `85773fb` | wip: 前会话暂存的中间态（CMSIS 命令 FIFO 等，1.0.44-48） |
| `302c842` | docs: 1.0.44-48 调优测量数据 + DAPLink_X033 参考评审 |

`247d83b` 是主体：SWD 请求 ACK 取消 + 指数退避重传 + 8 深交替流水线
（serial_bridge 请求队列 / swd_bridge_service 期望-响应 FIFO / cmsis_dap
槽位状态机，每槽独立 transfer 表）+ SWD_BURST 全链路删除 +
`serial_bridge_scheduler.h` 伪配置层删除。测试 31+25 全绿。

### 双板当前固件（均为 diag 构建，线兼容）

| 板 | 卷符 | 版本 | 内容 |
| --- | --- | --- | --- |
| 主机（USB 接 PC，探针序列号 `656C6C750015`） | E: | **1.0.55 diag** | CDC 计数器 + EP3 寄存器采样 |
| 从机 | D: | 1.0.54 diag | CDC 计数器 |

两板都 ≥1.0.53，支持 LOOPBACK 帧。烧录基准 8.86-10.62 kB/s
（GFSK2M + 1MHz SWD + 深度 8；单次波动 ±20%，看重传率）。

### 未提交内容

无——工作树干净。插桩保留在 diag 构建里（`#if CMSIS_DAP_DIAGNOSTICS_ENABLE`
+ DAP_DIAG 宏，release 构建零成本）。

## 2. 活跃 bug：CDC DATA 通路写停摆（未解决，调查到一半）

### 症状

PC → 主机板 CDC（COM8）写任意大小（512B/4096B）都超时，与回显开关无关。
烧录（SWD 路径）完全正常。

### 已采集的证据（全部指向同一结论）

1. vendor 0x80 状态在停摆期间**健康且完全静止**：retries=0、recoveries
   不变、无重传梯子、radio ready、error=False → 射频链路根本没被扰动。
2. 诊断页 2：`cdc_init_count=1`（CDC 类初始化只跑一次）、
   **`cdc_out_count=0`（CDC OUT 回调开机以来从未触发）**、`cdc_read_count=0`
   → 固件从未收到任何 CDC OUT 包。
3. **EP3 寄存器实测 `USBD_EPxCS(3) = 0x3023`**（init 时与运行时相同）：
   RX_STA=0b11=**VALID（已武装）**、TX_STA=00=DISABLED、类型=BULK、编号=3。
4. 回显开/关行为一致；pyOCD 完全关闭后写仍停摆（排除 libusb 占用冲突）。

### 结论

端点寄存器层面 EP3 RX = VALID，但硬件从不交付包、回调从不触发、PC 侧
NAK。问题不在传输层逻辑（ring/arming 数学已复查），在更底层。

### 已排查排除

- pyOCD/libusb 占用整个设备（关闭后复测同样停摆）
- LOOPBACK 功能交互（ON/OFF 都停摆）
- 链路重传/抖动（状态静止）
- 端点未武装（寄存器 VALID）
- PMA 溢出（`usbd_conf.h` 布局 504 ≤ 512，有编译期检查）
- ring 回绕数学（已复查，均正确）

### 发现的伴生 bug（待修）

`usbd_lld_core.c` 的 `usbd_ep_setup` 用
`USBD_EPxCS(ep_num) = ep_type | ep_num` 整写 32 位 EP 寄存器——同编号的
IN/OUT 共用寄存器（CDC 是 0x83/0x03），IN 方向 setup 会把 OUT 的 RX_STAT
清成 DISABLED，反之亦然。cdc_init 顺序（先 IN 后 OUT 再 arm）侥幸保住
RX=VALID，但 **EP3 TX 恒 DISABLED（0x3023 的 TX_STA=00）→ 主机 CDC IN
方向也是死的**——即使 OUT 通了，回显数据也回不到 PC。修复方向：
`usbd_ep_setup` 改为保留另一方向 STAT 位的写入，或在 composite init 全部
端点 setup 完成后统一按方向 arm。注意这是 vendor 库文件
（vendor/ 目录——需确认项目对 vendor 修改的策略，或在
`cdc_acm_transport.c` 的 cdc_init 末尾用正确顺序补偿）。

### 下一个 Agent 的调查清单（按优先级）

1. **从 PC 侧 dump 复合设备描述符**（pyusb：`usb.core.find(
   idVendor=0x28E9, idProduct=0x1290)` → dump 每个 interface 的
   bInterfaceNumber/bEndpointAddress/wMaxPacketSize）。验证 usbser 绑定的
   CDC DATA 接口（interface 2）声明的 OUT 端点确实是 0x03、IN 是 0x83。
   如果描述符把别的端点给了 usbser，一切症状立刻解释得通（写进了未武装
   的端点）。这是 5 分钟就能做的最高优先级检查。
2. 若描述符正确：GDB 挂主机板（`pyocd gdbserver`，或另接 ST-Link 用
   OpenOCD），在 `cdc_data_out` 入口下断点，PC 写一次，看中断是否进来、
   `USBD_INTF` 的 EP3 OUT 位是否置位。区分"硬件不递包"和"回调没被调用"。
3. 对比 MSC（EP2 OUT 正常收包：config disk 工作正常）与 CDC（EP3 OUT）
   的 `usbd_ep_setup` 参数差异——唯一差异是 PMA 地址（176 vs 304）。
   可写 magic 到 btable_ep[3].rx_addr 指向的 PMA（304）并回读，验证该段
   PMA 可用。
4. 修复伴生的 TX DISABLED bug 时不要只改 cdc_init 顺序——整写覆盖对
   IN/OUT 交错布局是系统性风险。

## 3. 射频吞吐天花板路线（本 bug 的下游目标）

CDC 回显测试是为了回答：**当前 DATA 窗口架构的射频往返天花板是多少**。
这决定 48 kB/s 目标（现 8.9-10.6）的可行性分配：

- 已知两侧互相咬死：pyOCD 每命令串行成本 1.5-4 ms（`cmsis_dap.
  deferred_transfers` 默认关，见 `cmsis_dap_probe.py:280`）× 射频单块
  ~1.55 ms（GFSK2M/1MHz：请求帧 0.43 + SWD 执行 0.8 + 响应帧 0.17 + 转向）。
- 诊断页实测四组对照见
  `docs/plan/2026-08-30-problem-audit-and-fix-plan.md` 第 7 节。
- 下一杠杆（两侧都要动）：
  1. **CMSIS-DAP PACKET_SIZE 512**（OUT 重组 + IN 分段，命令数 ÷4-8）
  2. **GFSK 载荷 238B + 双 USB 包合并 32 写块**（数据段帧数 ÷2；FLRC
     硬件 127 上限需 profile 感知保护）
- CDC 回显天花板若 ≥40 kB/s → 证明 DATA 窗口架构本身够用，SWD 迁入
  radio_window + 上述两项可达标；若 <20 kB/s → DATA 窗口也需要重设计
  （窗口 4×110B 太浅）。

### 跑通后的使用方法

```
python tools/radio_throughput_test.py --port COM8 --seconds 10
```

（COM8 可自动探测；需要主机板与从机都 ≥1.0.53，脚本自动开关回显。
丢失率按流内 u32 序号跳号统计。）

## 4. 环境与工具备忘（踩过的坑）

- **测试跑 pwsh 不跑 powershell**：powershell 5.1 在 Git Bash 下输出流
  异常且 GBK 编码炸 Python 子进程。`scripts/test_host.ps1` 用
  `pwsh -NoProfile -ExecutionPolicy Bypass -File scripts/test_host.ps1`。
- **cmake 直接调用**：`"G:/Software/ST/STM32CubeCLT_1.22.0/CMake/bin/
  cmake.exe"`（pwsh 引号会吃参数；bash 加引号安全）。工具链/版本缓存：
  `firmware/app/firmware_version.h` 的版本号在 **configure 时**读入
  CMake 缓存，改完版本必须重跑 cmake configure 再 build，否则 dwup
  版本不变、updater 报 "package version is not newer than device"。
- **updater 版本门槛**：每次给同一块板重刷都要递增
  `firmware_version.h`（PATCH/BCD/VERSION_CODE 三处）并重新 configure。
  诊断迭代期间版本已推到 1.0.55。
- **DFU 更新流程**：updater --volume <盘符> 会写 ENTER_DFU → 设备切 DFU
  → dfu-util 下载。坑：
  - 多板同时在线且都在 DFU 时 updater 拒绝（"multiple matching"），
    可按 USB path 刷：`dfu-util --device 28e9:1291 --path <path> -a 0 -D
    <image.bin>`（dfu-util 在 `G:/Software/Tools/dfu-util-0.11/`）。
  - dfu-util 下载后偶发不触发 leave，设备卡 DFU：先试 `dfu-util -e`，
    失败则**人工断电重启**（bootloader 会按 boot policy 启动已写入的
    新镜像），或走一次完整 updater DFU 下载触发 leave。
- **盘符漂移**：D:/E: 在重枚举后可能互换，识别板子必须读
  CONFIG.TXT 的 `MODE=`（WIRELESS_HOST/WIRELESS_SLAVE），不要信盘符。
- **pyOCD 基准命令**（用户的原始场景）：
  `pyocd flash --erase chip --base-address 0x08000000 -t stm32f103c8
  test_64k.bin`。首条 "T bit in XPSR" 警告是良性的（目标上跑的是
  test_64k.bin 的顺序字节图案，非有效代码）；SWD 时钟**不要超过 2 MHz**
  （4 MHz 实测重传率 10 倍恶化，目标接线信号完整性问题）。
- **诊断接口**：vendor 0x81 页 0/1/2（diag 构建；页 2 values[6..12] 为
  本次新增 CDC/EP3 插桩）；vendor 0x80 状态（release 也有，含
  retries/recoveries/radio_ready，无侵入判链路健康）。
  `python tools/dap_diagnostics.py reset|dump`。
- **pyocd 与 pyserial 可同时使用**：探针走 libusb 接口，CDC 走 usbser，
  互不冲突（已验证不是 CDC 停摆的原因）。

## 5. 建议的接手顺序

1. 从 PC dump 复合设备描述符验证端点分配（5 分钟，PC 侧无风险）。
2. 按第 2 节清单继续，修复 CDC 停摆 + TX DISABLED 伴生 bug。
3. 跑通 `radio_throughput_test.py`，把天花板写进 plan 文档第 7 节。
4. 按第 3 节决策 PACKET_SIZE 512 / 238B 合并的实施顺序。
5. 目标：64 KiB 烧录 48 kB/s；每一步实测数据记入
   `docs/measurements/` 与 plan 文档，改动按仓库的 conventional commit
   风格提交（feat/fix/docs，正文说明动机与证据）。
