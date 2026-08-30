# 交接文档：CDC DATA 通路停摆调查 + 射频吞吐路线（2026-08-30 深夜）

**给下一个 Agent 的自包含交接。** 上一会话完成了问题审计、交替流水线重写
（5.3 → 10.6 kB/s）和 3 个提交；随后实现 CDC 回显吞吐测试时撞上一个真实的
USB CDC bug，调查进行到一半。本文档记录全部现场状态。

## 1. 仓库与版本状态

### 已提交（main，未推送）

| 提交 | 内容 |
| --- | --- |
| `302c842` | docs: 1.0.44-48 调优测量数据 + DAPLink_X033 参考评审 |
| `85773fb` | wip: 前会话暂存的中间态（CMSIS 命令 FIFO 等，1.0.44-48） |
| `247d83b` | feat(radio): 交替 SWD 流水线 + 8 深窗口 + 死代码清理（1.0.49-1.0.52） |

### 未提交的工作树（本次主题，勿丢失）

- **CDC 回显吞吐基准功能**（1.0.53）：
  - `RADIO_FRAME_LOOPBACK` 帧类型（枚举末尾追加；旧固件静默丢弃，兼容）
  - 从机回显：`serial_bridge.c` DATA 分支在 `s_loopback` 时跳过 UART、
    原样推回 `s_data_tx_window`（窗口满丢弃，由测试脚本流内序号统计丢失）
  - 主机开关：vendor 命令 `0x82`（`DAP_VENDOR_LOOPBACK`，请求 [0x82, 0/1]）
    → `serial_bridge_loopback_set()` → 可靠控制帧下发
  - `tools/radio_throughput_test.py`：pyocd 发 0x82 + pyserial 流式写 +
    后台读线程；流 = 自同步 u32 递增计数器（4 字节序号，丢失即跳号）
  - `tools/read_ep3.py`：读诊断页 2 的 CDC/EP3 计数器
- **CDC 停摆调查插桩**（1.0.54/1.0.55）：
  - `dap_diagnostics.c/.h`：cdc_init/out/read 计数器 + EP3 寄存器采样，
    输出到诊断页 2 的 values[6..12]（page2 原有 values[0..5] 不变）
  - `cdc_acm_transport.c`：三处计数调用 + `usbd_lld_regs.h` 引入
- 版本：`firmware_version.h` = **1.0.55**（为绕过 updater 版本门槛逐次递增）

### 双板当前固件

| 板 | 卷符 | 版本 | 变体 |
| --- | --- | --- | --- |
| 主机（USB 接 PC，探针序列号 `656C6C750015`） | E: | **1.0.55 diag** | 带 CDC/EP3 插桩 |
| 从机 | D: | 1.0.54 diag | 带 CDC 计数器（无 EP3 采样） |

两板线兼容（都 ≥1.0.53，支持 LOOPBACK 帧）。

### 测试套件状态

主机 31 项 + Python 25 项全绿（含 LOOPBACK 帧/vendor 0x82 的新测试）。
Release 与 diag（`build/gcc/diag-1051`，CMSIS_DAP_DIAGNOSTICS=ON）构建零警告。

## 2. 活跃 bug：CDC DATA 通路写停摆（未解决）

### 症状

PC → 主机板 CDC（COM8）写任意大小（512B/4096B）都超时，与回显开关无关。
烧录（SWD 路径）完全正常，速度 8.9-10.6 kB/s。

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
NAK。问题不在我们的传输层逻辑（ring/arming 数学已复查），在更底层。

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
IN/OUT 共用寄存器，IN 方向 setup 会把 OUT 的 RX_STAT 清成 DISABLED，反之
亦然。cdc_init 顺序（先 IN 后 OUT 再 arm）侥幸保住 RX=VALID，但
**EP3 TX 恒 DISABLED（0x3023 的 TX_STA=00）→ 主机 CDC IN 方向也是死的**
——即使 OUT 通了，回显数据也回不到 PC。修复方向：改用
`USBD_EPxCS(ep_num) = (USBD_EPxCS(ep_num) & ~EPCS_MASK 方向位) | ...`
式保留写入，或在 composite init 全部端点 setup 完成后统一按方向
arm。注意这是 vendor 库文件（vendor/ 目录，需确认项目对 vendor 修改的
策略，或用 cdc_acm_transport 层绕过）。

### 下一个 Agent 的调查清单（按优先级）

1. **从 PC 侧读复合设备描述符**（pyusb：`usb.core.find(idVendor=0x28E9,
   idProduct=0x1290)` → dump configuration descriptor 的每个 interface 的
   bInterfaceNumber/bEndpointAddress/wMaxPacketSize）。验证 usbser 绑定的
   CDC DATA 接口（应/interface 2）声明的 OUT 端点确实是 0x03、IN 是 0x83。
   如果描述符把别的端点给了 usbser，一切症状立刻解释得通（写进了
   未武装的端点）。
2. 若描述符正确：GDB/SWO 挂主机板（`pyocd gdbserver` 或 OpenOCD + 另一
   ST-Link），在 `cdc_data_out` 入口下断点，PC 写一次，看中断是否进来、
   `USBD_INTF` 的 EP3 OUT 位是否置位。区分"硬件不递包"和"回调没被调用"。
3. 对比 MSC（EP2 OUT 正常收包：config disk 工作正常）与 CDC（EP3 OUT）的
   `usbd_ep_setup` 参数差异——唯一差异是 PMA 地址（176 vs 304）与类型
   （两者都是 BULK）。检查 `EP_BUF_SNG` 单缓冲模式在 EP3 上的 PMA 地址
   304..367 是否真的可写（可写一段 magic 到 btable_ep[3].rx_addr 指向的
   PMA 并回读）。
4. 修复伴生的 TX DISABLED bug 时不要只改 cdc_init 顺序——usbd_ep_setup
   的整写覆盖对 IN/OUT 交错布局是系统性风险（composite 里 EP 编号
   0x83/0x03 共用寄存器是设计事实）。

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
- CDC 回显天花板若 ≥40 kB/s → 证明 DATA 窗口架构本身够用，阶段 3
  （SWD 迁入 radio_window）+ 上述两项可达标；若 <20 kB/s → DATA 窗口
  也需要重设计（窗口 4×110B 太浅）。

### 跑通后的使用方法

```
python tools/radio_throughput_test.py --port COM8 --seconds 10
```

（COM8 自动探测也可；需要主机板 1.0.53+ 且从机 1.0.53+，脚本自动开关
回显。丢失率按流内 u32 序号跳号统计。）

## 4. 环境与工具备忘（踩过的坑）

- **测试跑 pwsh 不跑 powershell**：powershell 5.1 在 Git Bash 下输出流
  异常且 GBK 编码炸 Python 子进程。`scripts/test_host.ps1` 用
  `pwsh -NoProfile -ExecutionPolicy Bypass -File scripts/test_host.ps1`。
- **cmake 直接调用**：`"G:/Software/ST/STM32CubeCLT_1.22.0/CMake/bin/
  cmake.exe"`（pwsh 引号会吃参数；bash 加引号安全）。工具链/版本缓存：
  `firmware_version.h` 的版本号在 **configure 时**读入 CMake 缓存，改完
  版本必须重跑 cmake configure 再 build，否则 dwup 版本不变、updater
  报 "package version is not newer than device"。
- **updater 版本门槛**：每次给同一块板重刷都要递增
  `firmware_app/firmware_version.h`（PATCH/BCD/VERSION_CODE 三处）并
  重新 configure。诊断迭代期间版本已推到 1.0.55。
- **DFU 更新流程**：updater --volume <盘符> 会写 ENTER_DFU → 设备切 DFU
  → dfu-util 下载。坑：
  - 多板同时在线且都在 DFU 时 updater 拒绝（"multiple matching"），
    可按 USB path 刷：`dfu-util --device 28e9:1291 --path <path> -a 0 -D
    <image.bin>`（dfu-util 在 `G:/Software/Tools/dfu-util-0.11/`）。
  - dfu-util 下载后偶发不触发 leave，设备卡 DFU：先试 `dfu-util -e`，
    失败则**人工断电重启**（bootloader 会按 boot policy 启动已写入的
    新镜像）。
- **盘符漂移**：D:/E: 在重枚举后可能互换，识别板子必须读
  CONFIG.TXT 的 `MODE=`（WIRELESS_HOST/WIRELESS_SLAVE），不要信盘符。
- **pyOCD 基准命令**（用户的原始场景）：
  `pyocd flash --erase chip --base-address 0x08000000 -t stm32f103c8
  test_64k.bin`。首条 "T bit in XPSR" 警告是良性的（目标上跑的是
  test_64k.bin 的顺序字节图案，非有效代码）；SWD 时钟**不要超过 2 MHz**
  （4 MHz 实测重传率 10 倍恶化，目标接线信号完整性问题）。
- **诊断接口**：vendor 0x81 页 0/1/2（diag 构建）；vendor 0x80 状态
  （release 也有，含 retries/recoveries/radio_ready，无侵入判链路健康）。
  `python tools/dap_diagnostics.py reset|dump`。

## 5. 建议的接手顺序

1. 从 PC dump 复合设备描述符验证端点分配（5 分钟，PC 侧无风险）。
2. 按第 2 节清单继续，修复 CDC 停摆 + TX DISABLED 伴生 bug。
3. 跑通 `radio_throughput_test.py`，把天花板写进 plan 文档第 7 节。
4. 按第 3 节决策 PACKET_SIZE 512 / 238B 合并的实施顺序。
5. 将工作树的 LOOPBACK + 插桩改动整理提交（功能验证后；插桩可保留在
   diag 构建里）。
6. 目标：64 KiB 烧录 48 kB/s；每一步实测数据记入
   `docs/measurements/` 与 plan 文档。
