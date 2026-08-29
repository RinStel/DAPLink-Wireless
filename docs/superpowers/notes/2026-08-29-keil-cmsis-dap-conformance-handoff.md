# Keil 编程收尾失败：CMSIS-DAP 对照交接记录

更新时间：2026-08-29  
工作目录：`G:\\Develop\\DAPLink_Wireless`

## 用户当前现象

用户已更新固件，但 Keil 仍在 Programming 接近完成时失败：

```text
Load "G:\\Develop\\STM32\\Project\\F10x Project Template\\Objects\\project.axf"
Erase Done.
Programming Failed!
Error: Flash Download failed  -  "Cortex-M3"
Flash Load finished at 21:10:21
```

pyOCD 仍可成功烧录。该结果只证明部分 CMSIS-DAP/SWD 路径可用，不能证明 Keil 的完整 Flash Algorithm 交互兼容。

## 当前源码状态

- 源码头文件版本为 `1.0.30`；运行中的实机版本尚未在本轮读取确认。
- 当前工作树已有用户/前序任务修改，必须保留：
  - `CHANGELOG.md`
  - `firmware/app/cmsis_dap.c`
  - `firmware/app/firmware_version.h`
  - `tests/cmsis_dap_protocol_test.c`
- 本轮只做了对照和测试，没有修改生产代码。
- Arm 对照实现来自 `vendor/Arm-CMSIS-DAP`，CMSIS-DAP V2.1.2，提交：
  `12636590eec66fae2d1bba4518749426ad5a4595`。

## 已确认的 CMSIS-DAP 差异

### 1. `DAP_Delay` 单位错误：最高优先级

Arm `DAP_Delay` 的参数单位是毫秒，见：

`vendor/Arm-CMSIS-DAP/Firmware/Source/DAP.c:198-208`

当前实现把同一个 16 位参数传给 `board_delay_us()`，见：

`firmware/app/cmsis_dap.c:808-817`

因此 Keil 请求 `DAP_Delay(10)` 时，当前约延时 `10 us`，Arm 语义应为 `10 ms`。这与“Erase Done 后、Programming 收尾阶段失败”相符，可能导致 Flash Algorithm 或目标复位尚未完成就被继续访问。

修复不能简单改为阻塞式 `board_delay_ms()`，因为无线主机在阻塞期间会停止处理射频。建议增加非阻塞延时状态：

1. 接收 `DAP_Delay` 后记录毫秒 deadline。
2. `cmsis_dap_process()` 到 deadline 前不生成响应。
3. 到期后返回 `DAP_OK`。
4. `ExecuteCommands` 中的 Delay 也必须等待完成后再推进下一条子命令。

### 2. Atomic Commands 能力位未声明

当前 `DAP_Info(0xF0)` 返回：

```text
byte0 = 0x01
byte1 = 0x01
```

当前固件已经实现 `DAP_ExecuteCommands (0x7F)`，所以 byte0 至少应为：

```text
0x11 = SWD(bit0) | Atomic Commands(bit4)
```

当前代码：`firmware/app/cmsis_dap.c:324-328`。  
Arm 参考：`vendor/Arm-CMSIS-DAP/Firmware/Source/DAP.c:119-130`。

byte1 的 USB COM 标志为 `0x01`，如果产品继续暴露 CDC 接口则可以保留。

### 3. 其他规范差异

- `DAP_Info` 未显式实现 Device/Board Vendor、Device/Board Name、Timestamp、UART/SWO buffer size 查询。当前目标不是固定设备，且未声明 Timestamp/SWO；优先级低于 Delay 和 Atomic。
- USB OUT 路径正确把 `DAP_TransferAbort (0x07)` 作为带外取消处理；但普通 `cmsis_dap_submit()`/`ExecuteCommands` 路径仍对 `0x07` 返回 `[0x07, 0xFF]`，Arm 普通分发应返回 Invalid 命令。
- 当前 `DAP_WriteABORT` 会根据远端 SWD 结果返回错误；Arm 参考调用 SWD 写入后仍返回 `DAP_OK`。通常不是最后 Programming 阶段的主路径。
- 当前 `DAP_ResetTarget` 固定返回 reset-result=`1`；Arm 默认无设备专用复位序列时返回 `0`。需要确认产品是否把实际 nRESET 脉冲定义为设备专用序列。
- 当前 `SWJ_Clock` 会将请求频率限制为 10 kHz 至 4 MHz，并重新初始化目标 SWD；这是项目适配约束，不是 Arm 通用实现。
- 当前 `DAP_TransferBlock` 受 64 字节 USB 包和本地数组限制，实际最多处理约 14/15 个有效数据项；这对合法 64 字节包通常足够，但不是 Arm 的通用 16 位 count 范围。

## 已对照且暂未发现明显差异的 SWD 收尾路径

`firmware/app/swd_tunnel.c:646-783` 已覆盖：

- 连续 AP read 的 posted-read 流水线；
- AP read 后紧接 DP read/write 时先读取 `DP_RDBUFF`；
- 最后 AP read 的 `DP_RDBUFF` 收尾；
- 最后写入后的 `DP_RDBUFF` 检查；
- WAIT 重试次数和完成数；
- Match Mask 不访问目标寄存器；
- Match Value 的 AP posted 顺序和 mismatch 完成数。

当前 `cmsis_dap.c:853-910` 只把普通读传输数据复制到 USB 响应，不返回 Match Value 数据，符合 Arm 语义。

因此目前不建议再次修改 posted-read 或 SWD bit-bang 收尾代码；应先验证 `DAP_Delay` 和能力位。

## 已执行验证

执行命令：

```powershell
pwsh -NoProfile -File .\\scripts\\test_host.ps1 -Name cmsis-dap
pwsh -NoProfile -File .\\scripts\\test_host.ps1 -Name swd-tunnel
git diff --check
```

结果：

```text
CMSIS-DAP protocol tests passed
SWD tunnel protocol tests passed
```

`git diff --check` 只有 Git 无法读取 `C:\\Users\\YSCha\\.config\\git\\ignore` 的权限警告，没有报告差异格式错误。

现有测试没有覆盖：

- `DAP_Delay` 的毫秒语义；
- 非阻塞 Delay 的等待边界；
- `DAP_Info(0xF0)` 的 Atomic bit；
- Delay 位于 `ExecuteCommands` 中时的异步推进。

## 下一步建议（需要用户确认后实施）

1. 先写失败测试：捕获 Delay 参数，断言 `10` 表示 10 ms；增加 `DAP_Info(0xF0)` 断言 byte0=`0x11`。
2. 实现非阻塞 `DAP_STATE_DELAY`，保持无线主循环可运行，并接入 ExecuteCommands 父命令。
3. 修改能力位为 `0x11`，补齐 focused tests。
4. 运行完整主机测试、Debug/Release GCC 构建、`verify_release.ps1` 和 `git diff --check`。
5. 生成新 Release 产物，先更新无线从机、再更新无线主机；读取两块板实际产品版本。
6. 重新执行同一 Keil Flash Download。
7. 如果仍在最后阶段失败，再加入临时 USB EP5 原始请求/响应捕获，重点确认 Keil 最后一个 `DAP_Delay`、`DAP_Transfer`、`DAP_TransferBlock`、`DAP_ResetTarget` 和 Verify 读取的返回字节。

## 重要边界

- 不要把 pyOCD 成功当作 Keil 验收成功。
- 不要再次强制所有 USB IN 响应为 64 字节；Arm USB 模板发送实际响应长度，当前短响应行为更接近规范。
- 不要在没有 USB 原始序列证据时继续猜测并反复擦除目标。
- 不执行 `git reset --hard`、`git clean`、提交或推送；保留现有脏工作树。

## 本轮实施结果（2026-08-29）

- 按 TDD 新增 `DAP_Delay(10)` 异步边界测试：在 100 ms 提交后，109 ms 不响应，110 ms 返回 `DAP_OK`。
- `firmware/app/cmsis_dap.c` 新增 `DAP_STATE_DELAY`，使用 `board_millis()` 截止时间，避免阻塞无线主循环；该路径也由 `DAP_ExecuteCommands` 的父命令推进机制覆盖。
- `DAP_Info(0xF0)` 能力字节从 `0x01` 改为 `0x11`，声明 SWD 与 Atomic Commands。
- 先运行测试确认能力位断言失败，再实施修复；修复后 `cmsis-dap`、`swd-tunnel` 通过。
- `scripts/verify_release.ps1` 全部通过，Debug/Release GCC 固件、DWUP 和 factory HEX 均重新生成。

硬件边界仍未闭合：本轮未刷写无线主/从机，也未重新执行 Keil 实机 Programming。因此当前结论是“代码与软件门禁通过，Keil 验收待执行”；若仍失败，下一步应抓取 USB EP5 原始请求/响应序列。

## 1.0.32 Atomic A/B 实验

- `FIRMWARE_VERSION_STRING` 已递增到 `1.0.32`，`FIRMWARE_VERSION_CODE=1032`，USB BCD 为 `0x0132`。
- 默认关闭 Atomic Commands 能力广告：`DAP_Info(0xF0)` 返回 `0x01`，但 `0x7F/0x7E` 实现和 `0x81` 追踪接口保留。
- 可通过编译定义 `CMSIS_DAP_ADVERTISE_ATOMIC_COMMANDS=1` 生成 `0x11` 对照镜像。
- Debug 默认保留追踪存储；Release 由 `NDEBUG` 裁剪。

## Keil 实机验收通过（2026-08-29 22:09:36）

无线主机/从机刷入 `1.0.32` 后，使用同一 F10x 工程完成真实 Keil Flash Download：

```text
Load "G:\\Develop\\STM32\\Project\\F10x Project Template\\Objects\\project.axf"
Erase Done.
Programming Done.
Verify OK.
Flash Load finished at 22:09:36
```

结论：关闭 `ATOMIC_COMMANDS` 能力广告后，Keil 回退到普通 CMSIS-DAP 传输路径，编程和校验均成功。当前已确认的根因范围是 Keil 对 `DAP_ExecuteCommands`/`DAP_QueueCommands` 批量路径的兼容性问题；尚未证明批量实现本身的具体错误。`CMSIS_DAP_ADVERTISE_ATOMIC_COMMANDS=1` 的 `0x11` 对照镜像仍可用于后续定位。

继续本任务时，应先读取本交接文档，再从“下一步建议”第 1 项开始。

## 吞吐诊断镜像构建与单板刷写记录（2026-08-29）

- 复用已验证的 `build/gcc/debug` CMake 缓存，配置 `CMSIS_DAP_DIAGNOSTICS=ON`，Debug `daplink_slot_a/b.elf` 构建成功。
- 新建诊断目录时 MinGW `TryCompile` 仍失败；复用既有工具链缓存可正常构建。
- 当前仅枚举到探针 `656C6C750015`，已执行单板刷写 `build/gcc/debug/daplink_slot_a.hex`。
- pyOCD 报告擦除并写入 `49152 bytes / 48 pages`，速率 `4.00 kB/s`；同时报告 `0x08010000` 无内存区域和无效 XPSR 警告。
- 第二块无线板未连接，双板更新、统计采集和吞吐验收均未完成。
