# 硬件手册：原理图、板级安全与验收

## 适用范围与事实来源

本文合并原硬件审查和硬件验收内容。U5 的完整网络以
[U5 原理图连接记录](schematic_u5_connections.md)为事实基线；U5 原理图连接记录来自
EasyEDA 工程 `无线调试器`、`Board_V1.0`、原理图页面 `主控与射频`，读取日期为
2026-08-18。

原项目文档使用硬件版本 `v0.5`，而当前 EasyEDA 记录为 `Board_V1.0`、页面版本
`V1.0`。两者是否为同一硬件迭代为 `待确认：`，本文不擅自合并这两个版本标识。

## 核心器件

| 功能 | 器件或参数 |
| --- | --- |
| MCU | `GD32F303CCT6`，256 KiB Flash，48 KiB SRAM |
| 无线 | BOM 为 `E28-2G4M20SX`；模块资料标明核心为 SX1281 |
| USB | USB-C，USB FS Device，外部 1.5 kOhm D+ 上拉 |
| 主电源 | `SY8089AAAC` 降压至 `3V3_REG` |
| 电源选择 | `TPS2116DRLR`，在 `3V3_REG` 与 `TGT_3V3` 间选择 |
| 目标供电 | 两路 `SY6280AAAC`，输出 5 V 和 3.3 V |

## U5 原理图基线与当前代码

完整 48 个引脚、跨页连接、电阻编号和 EasyEDA 元件标识符不在本文重复维护，
请以[U5 原理图连接记录](schematic_u5_connections.md)为准。下表只列出会直接
影响固件外设配置的差异。当前代码值来自 `firmware/bsp/board_pins.h`；原理图
值来自 U5 直接网络。

| 功能 | 当前代码 GPIO | 原理图 GPIO/网络 | 状态 |
| --- | --- | --- | --- |
| RGB 红灯 | `PC14` | `PC14` / `STAT_LED_R` | 原理图与实板单色测试一致 |
| RGB 绿灯 | `PC15` | `PC15` / `STAT_LED_G` | 原理图与实板单色测试一致 |
| RGB 蓝灯 | `PC13` | `PC13` / `STAT_LED_B` | 原理图与实板单色测试一致 |
| 配置按键 | `PB9` | `PB8` / `MCU_KEYA`、`PB9` / `MCU_KEYB` | 当前只读取一个按键；`SW1` 功能为 `待确认：` |
| USB D+ 上拉 | `PA8` | `PA8` / `USB_DP_PU` | 一致 |
| USB 供电有效检测 | `PA0` 输入 | `PA0` / `USB_AUTO_EN` | 一致；外部高有效输入，固件通过 `board_usb_power_present()` 读取 |
| 目标供电控制 | 无独立 MCU 输出 | `PA0` / `USB_AUTO_EN`，两个目标电源开关共用 | 一致；由硬件 USB 供电有效信号控制 |
| 射频 NSS/SPI | `PA4/PA5/PA6/PA7` | `PA4/PA5/PA6/PA7` | 一致 |
| 射频 RX/TX 使能 | `PA1`、`PA2` | `PA1` / `RF_RX_EN`、`PA2` / `RF_TX_EN` | 一致 |
| 射频复位 | `PA3` | `PA3` / `RF_NRESET` | 一致 |
| 射频 BUSY | `PB1` | `PB1` / `RF_BUSY` | 一致 |
| 射频 DIO1 | `PB5` | `PB5` / `RF_DIO1` | 一致 |
| 射频 DIO2/DIO3 | 当前 HAL 只使用 DIO1 | `PB0` / `RF_DIO2`、`PB10` / `RF_DIO3` | 是否需固件处理为 `待确认：` |
| 目标 SWCLK | `PB13` | `PB13` / `TGT_SWCLK_C` | 一致 |
| 目标 SWDIO | `PB12` | `PB12` / `TGT_SWDIO_C` | 一致 |
| 目标 NRST | `PB15` | `PB15` / `TGT_NRST_C` | 一致 |
| 目标 BOOT | `PB14` | `PB14` / `TGT_BOOT_C` | 一致；当前仍只配置为输入 |
| 目标 USART TX/RX | `PA9/PA10` | `PA9/PA10` / `TGT_TXD_C/TGT_RXD` | GPIO 一致，TX 侧经过串联电阻 |

U5 GPIO 已按原理图同步到 `board_pins.h`、板级初始化和 USB 配置；
`SW1`、`RF_DIO2/DIO3` 以及目标 BOOT 的产品行为仍未扩展。

## 投板与上电前风险

以下项目必须在真实硬件和最新 EasyEDA 设计中复核：

1. `LED1` 的极性、符号引脚定义、封装方向和实际连接。旧审查记录中的 `K`/`A`
   连接按常规定义可能造成反向偏置，不能只根据符号名称判断。
2. 2026-08-27 实板低电平单色测试确认 `PC13=蓝`、`PC14=红`、`PC15=绿`。
   更新后的 EasyEDA 网络名 `STAT_LED_B/R/G` 与实际发光颜色一致。旧审查记录使用了
   `R18/R26` 和 470 Ohm 的描述，而当前 U5 直接网络连接的是 `R27/R28/R29`；
   电阻值和各通道电流仍为 `待确认：`。
3. USB 必须得到准确 48 MHz 时钟。8 MHz HXTAL 到 120 MHz 系统时钟的配置不能
   只通过 CPU 运行验证。
4. `USB_AUTO_EN` 同时连接 `U_TGT_SW1.EN`、`U_TGT_SW2.EN` 和 `Q2` 栅极。
   EasyEDA 元件资料标明 `SY6280AAAC.EN` 为高电平有效；PA0 必须保持输入，
   固件不得把 `USB_AUTO_EN` 当作目标供电输出使用。首次上电仍必须实测 USB 供电有效与目标
   两路输出的对应关系。
5. `TGT_3V3` 既可作为本板 `TPS2116` 输入，又可由本板 `SY6280AAAC` 输出。
   外部目标已供电时不得直接打开目标 3.3 V 输出；仅靠软件防互灌不充分。
6. `D3` 从 `TGT_5V` 指向 `5V_SYS`，可能允许目标 5 V 给本板供电。必须核算
   肖特基压降、USB 反灌路径和 `TPS2116` 工作状态。
7. `TGT_NRST_C`、`TGT_BOOT_C` 以及目标 SWD 网络经过串联电阻。电阻方向、目标端
   输入电容、有效电平和高速 SWJ 时序兼容性必须结合实物确认。
8. E28 模块的 TX_EN/RX_EN 时序、BUSY 极性、SPI 最高频率和复位时间必须以模块
   官方手册为准。

## 板级安全态

下面是当前代码在 `board_init()` 中表达的启动意图，不是对当前 EasyEDA GPIO
映射已经正确的声明：

- `BOARD_RF_TX_EN = 0`
- `BOARD_RF_RX_EN = 0`
- `BOARD_RF_NSS = 1`
- `BOARD_RF_NRESET = 0`，初始化完成后释放
- `BOARD_USB_PULLUP = 0`，USB 协议栈就绪后连接
- `BOARD_USB_AUTO_EN` 配置为浮空输入，固件通过 `board_usb_power_present()` 读取
- 目标 `NRST` 与 `BOOT` 默认高阻输入

目标供电没有独立 MCU 输出接口，由 `USB_AUTO_EN` 硬件网络控制；固件不得写入 PA0。

## 首次上电流程

1. 不连接目标板，不焊接或不启用射频模块；限流电源设为 5 V/100 mA。
2. 测量 `5V_SYS`、`3V3_REG`、`3V3_MAIN` 和 `VDDA`，检查纹波和异常发热。
3. 通过 MCU 下载接口 `H2` 烧录最小固件；`H1` 是目标/外部接口，不应与 MCU
   下载接口混用。确认 8 MHz HXTAL 和 120 MHz 系统时钟。
4. 只测试 RGB LED 和按键，不打开目标供电。
5. 检查 USB D+/D- 静态阻抗，之后再启用软件上拉和 USB 枚举。
6. 单独验证两路目标电源的空载、限流和短路保护。
7. 最后焊接或启用射频模块，先读取状态寄存器，再进行发射测试。

## 硬件冒烟与发布验收

### USB DFU 实板验收补充

首次部署使用 pyOCD，按 Bootloader、Slot A、Factory State 分区执行 sector erase、
Program 和 Verify，不写 Slot B 或 `0x0803F000-0x0803FFFF` 配置页。禁止 full-chip
erase。当前 pyOCD 使用 `stm32f103rc` 兼容目标；这不是 GD32 型号认证，必须记录
实际回读结果。

```powershell
pyocd list --probes
powershell -ExecutionPolicy Bypass -File .\scripts\flash_pyocd.ps1 `
    -Artifact .\build\gcc\release\daplink_factory.hex `
    -Probe <unique-id> -Target stm32f103rc -Connect under-reset `
    -Frequency 1M -Erase sector
```

CLion Release profile 的 `flash_factory`、`flash_bootloader`、`flash_slot_a` 和
`flash_slot_b` 是非默认烧录目标；普通 Build 不会触发 Flash 写入。没有 `NRST` 连接
时，将 `-Connect under-reset` 改为 `-Connect halt`，并降低频率后重新验证。
连接应用时把
`CONFIG.TXT` 写成精确的 `ENTER_DFU=1` 并安全弹出，记录 `28E9:1290` 断开、
`28E9:1291` DFU 枚举、蓝灯等待/写入、青色校验、绿色约 500 ms 和自动复位。拔线或
掉电覆盖擦除、写入、Manifest 三个阶段，确认旧槽仍可启动；烧录不确认的候选镜像，
确认三次复位后红蓝交替并回退。最后按住恢复键上电，确认无有效应用时仍能进入
恢复 DFU，并验证恢复模式可降级但不能绕过型号、地址、长度、向量表或 CRC32 检查。

上述项目必须记录板卡序列号、工具版本、VID/PID、LED 实际颜色和 Flash 地址；没有
真实 USB、掉电、按键和 LED 记录时，只能标记为软件完成，不能标记为硬件发布通过。

### 准备

- 两块无线调试器，分别配置为 `WIRELESS_HOST` 和 `WIRELESS_SLAVE`，同步码一致。
- 一块已知正常的 Cortex-M 目标板。
- 主机安装 pyOCD、OpenOCD 和 Arm CMSIS-DAP Validation。
- 首次测试关闭调试器目标供电，由目标板独立供电并共地。

### pyOCD 冒烟

连接无线主机到 PC、无线从机到目标板，然后运行：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\hardware_smoke.ps1 `
    -Target <pyocd-target-name>
```

脚本以 100 kHz SWD 连接，读取 Cortex-M CPUID，复位目标后再次读取。多探针
环境使用 `-Probe <unique-id>`。

### 发布签核

每项记录固件 SHA-256、两台设备序列号、目标 MCU、工具版本和结果：

1. 有线模式完成连接、下载、复位、断点、单步及内存读写。
2. 无线模式重复相同步骤，并覆盖全部 GFSK/FLRC profile。
3. 使用 pyOCD、OpenOCD 和 Arm Validation 分别验证。
4. 目标持续返回 WAIT 时发送 Abort，记录最坏响应时间。
5. 屏蔽当前频道，确认两台设备在不同起始频道下重新会合。
6. 通信过程中切换速率，确认无重复写、无错误复位、无会话串台。
7. 并发运行 SWD 下载和 CDC 串口流量至少一小时。
8. 连续运行 24 小时，记录重试、恢复、超时、无效帧和看门狗计数。
9. 配置写入时断电，确认保留上一份或完整的新配置。
10. Windows、Linux、macOS 分别验证 USB 枚举、休眠恢复和安全弹出配置盘。

全部原始日志应随正式版本标签归档。失败必须形成可复现问题，不能只标记为
“偶发”。

## 待确认项汇总

- `Board_V1.0`/`V1.0` 与项目硬件版本 `v0.5` 的对应关系。
- `SW1`、`SW2` 的产品功能分工。
- `RF_DIO2`、`RF_DIO3` 是否需要固件处理。
- 目标接口 `TGT_SWDIO_C`、`TGT_SWCLK_C`、`TGT_NRST_C`、`TGT_BOOT_C` 的串联电阻方向和有效电平。
- 目标 BOOT 是否需要固件主动驱动；当前代码只将目标 BOOT 配置为高阻输入。
