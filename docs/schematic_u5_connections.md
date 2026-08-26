# U5 原理图连接记录

## 目的

本文记录从 EasyEDA 读取的 `U5`（`GD32F303CCT6`）连接网络，供固件引脚映射校对和验收使用。
本文记录原理图事实，并标出当前固件是否已同步。

## 读取范围

| 项目 | 读取结果 |
| --- | --- |
| EasyEDA 工程 | `无线调试器` |
| Board | `Board_V1.0` |
| 原理图 | `Schematic1` |
| U5 所在页面 | `主控与射频` |
| 原理图页面 UUID | `bea0fa8614fcd8d1` |
| U5 图元 ID | `6644ba3189627ae4` |
| U5 唯一 ID | `gge205` |
| U5 器件型号 | `GD32F303CCT6` |
| 页面标题版本 | `V1.0` |
| 页面标题更新时间 | `2026-08-15` |
| EasyEDA API 读取日期 | `2026-08-18` |

网络名、引脚号和引脚名均保留 EasyEDA 原始拼写。`NC` 表示对应 MCU 引脚带有非连接标识，读取时没有匹配到导线网络。

## U5 全部引脚网络

| Pin | MCU 引脚名 | 原理图网络 | 状态/备注 |
| ---: | --- | --- | --- |
| 1 | `VBAT` | `3V3_MAIN` | 电源 |
| 2 | `PC13-TAMPER-RTC` | `STAT_LED_R` | RGB 红色通道 |
| 3 | `PC14-OSC32IN` | `STAT_LED_G` | RGB 绿色通道 |
| 4 | `PC15-OSC32OUT` | `STAT_LED_B` | RGB 蓝色通道 |
| 5 | `PD0-OSCIN` | `HXTAL_IN` | 8 MHz 外部晶振输入 |
| 6 | `PD1-OSCOUT` | `HXTAL_OUT` | 8 MHz 外部晶振输出 |
| 7 | `NRST` | `MCU_NRST` | MCU 下载/复位接口 |
| 8 | `VSSA` | `GND` | 模拟地 |
| 9 | `VDDA` | `VDDA` | 模拟电源 |
| 10 | `PA0-WKUP` | `USB_AUTO_EN` | 外部 USB 供电有效输入；高电平使能目标电源 |
| 11 | `PA1` | `RF_RX_EN` | 射频接收使能 |
| 12 | `PA2` | `RF_TX_EN` | 射频发射使能 |
| 13 | `PA3` | `RF_NRESET` | 射频复位 |
| 14 | `PA4` | `RF_NSS` | 射频 SPI 片选 |
| 15 | `PA5` | `RF_SCK` | 射频 SPI 时钟 |
| 16 | `PA6` | `RF_MISO` | 射频 SPI 主入从出 |
| 17 | `PA7` | `RF_MOSI` | 射频 SPI 主出从入 |
| 18 | `PB0` | `RF_DIO2` | 射频模块 DIO2 |
| 19 | `PB1` | `RF_BUSY` | 射频模块 BUSY |
| 20 | `PB2` | `BOOT1` | 启动配置网络 |
| 21 | `PB10` | `RF_DIO3` | 射频模块 DIO3 |
| 22 | `PB11` | `NC` | 非连接 |
| 23 | `VSS_1` | `GND` | 电源地 |
| 24 | `VDD_1` | `3V3_MAIN` | 电源 |
| 25 | `PB12` | `TGT_SWDIO_C` | 目标 SWDIO，经串联电阻连接目标接口 |
| 26 | `PB13` | `TGT_SWCLK_C` | 目标 SWCLK，经串联电阻连接目标接口 |
| 27 | `PB14` | `TGT_BOOT_C` | 目标 BOOT，经串联电阻连接目标接口 |
| 28 | `PB15` | `TGT_NRST_C` | 目标 NRST，经串联电阻连接目标接口 |
| 29 | `PA8` | `USB_DP_PU` | USB D+ 外部上拉 |
| 30 | `PA9` | `TGT_TXD_C` | 目标 USART 发送 |
| 31 | `PA10` | `TGT_RXD` | 目标 USART 接收 |
| 32 | `PA11` | `USB_DM` | USB D- |
| 33 | `PA12` | `USB_DP` | USB D+ |
| 34 | `PA13` | `MCU_SWDIO` | MCU 下载接口 SWDIO |
| 35 | `VSS_2` | `GND` | 电源地 |
| 36 | `VDD_2` | `3V3_MAIN` | 电源 |
| 37 | `PA14` | `MCU_SWCLK` | MCU 下载接口 SWCLK |
| 38 | `PA15` | `NC` | 非连接 |
| 39 | `PB3` | `NC` | 非连接 |
| 40 | `PB4` | `NC` | 非连接 |
| 41 | `PB5` | `RF_DIO1` | 射频模块 DIO1 |
| 42 | `PB6` | `TGT_TDI_C` | 目标接口 TDI，不是当前 SWD 时钟 |
| 43 | `PB7` | `TGT_TDO` | 目标接口 TDO，不是当前 SWD 数据 |
| 44 | `BOOT0` | `BOOT0` | MCU 启动配置/下载接口 |
| 45 | `PB8` | `MCU_KEYA` | `SW1` 按键 |
| 46 | `PB9` | `MCU_KEYB` | `SW2` 按键 |
| 47 | `VSS_3` | `GND` | 电源地 |
| 48 | `VDD_3` | `3V3_MAIN` | 电源 |

## 关键网络的跨页连接

下表列出 U5 直接连接至射频、USB、目标接口、按键和下载接口的网络。串联电阻两侧的网络名可能不同；表中保留 U5 直接连接的一侧网络名。

| U5 网络 | U5 Pin | 跨页/外部连接 |
| --- | ---: | --- |
| `USB_AUTO_EN` | 10 | `U_TGT_SW1` Pin 4 `EN`、`U_TGT_SW2` Pin 4 `EN`、`Q2` Pin 1 `G`；两个目标电源开关共用网络，`SY6280AAAC` 的 `EN` 为高电平有效；PA0 仅作输入 |
| `RF_RX_EN` | 11 | `U6` Pin 8 `RX_EN`、`R25` Pin 2 |
| `RF_TX_EN` | 12 | `U6` Pin 9 `TX_EN`、`R21` Pin 2 |
| `RF_NRESET` | 13 | `U6` Pin 11 `NRESET` |
| `RF_NSS` | 14 | `U6` Pin 6 `NSS_CTS`、`R20` Pin 1 |
| `RF_SCK` | 15 | `U6` Pin 5 `SCK_RTSN` |
| `RF_MISO` | 16 | `U6` Pin 3 `MISO_TX` |
| `RF_MOSI` | 17 | `U6` Pin 4 `MOSI_RX` |
| `RF_DIO2` | 18 | `U6` Pin 14 `DIO2` |
| `RF_BUSY` | 19 | `U6` Pin 12 `BUSY` |
| `RF_DIO3` | 21 | `U6` Pin 15 `DIO3` |
| `TGT_SWDIO_C` | 25 | `R16` Pin 1；目标侧网络经过串联电阻引出 |
| `TGT_SWCLK_C` | 26 | `R15` Pin 1；目标侧网络经过串联电阻引出 |
| `TGT_BOOT_C` | 27 | `R2` Pin 2；目标侧网络经过串联电阻引出 |
| `TGT_NRST_C` | 28 | `R1` Pin 1；目标侧网络经过串联电阻引出 |
| `USB_DP_PU` | 29 | `R19` Pin 2 |
| `TGT_TXD_C` | 30 | `R17` Pin 1；目标侧网络经过串联电阻引出 |
| `TGT_RXD` | 31 | 外部接口 `H1` Pin 9、ESD 器件 `D1` Pin 6 |
| `USB_DM` | 32 | `R33` Pin 2（TYPE-C 页面） |
| `USB_DP` | 33 | `R19` Pin 1、`R32` Pin 2（TYPE-C 页面） |
| `MCU_SWDIO` | 34 | 下载接口 `H2` Pin 4 |
| `MCU_SWCLK` | 37 | 下载接口 `H2` Pin 3 |
| `TGT_TDI_C` | 42 | `R18` Pin 1；这是目标接口 TDI 网络 |
| `TGT_TDO` | 43 | 外部接口 `H1` Pin 7、ESD 器件 `D1` Pin 4 |
| `BOOT0` | 44 | 下载接口 `H2` Pin 5、`R22` Pin 1 |
| `MCU_KEYA` | 45 | `SW1` Pin 1、`C24` Pin 2 |
| `MCU_KEYB` | 46 | `SW2` Pin 1、`C25` Pin 2 |
| `HXTAL_IN` | 5 | `X1` Pin 1、`C11` Pin 1 |
| `HXTAL_OUT` | 6 | `X1` Pin 3、`C12` Pin 1 |
| `STAT_LED_R` | 2 | `R27` Pin 1 |
| `STAT_LED_G` | 3 | `R28` Pin 1 |
| `STAT_LED_B` | 4 | `R29` Pin 1 |

## 当前固件映射对照

当前值来自 `firmware/bsp/board_pins.h` 以及调用这些宏的板级代码。这里只比较 MCU GPIO，不比较网络两侧的串联电阻编号。

| 固件功能 | 当前固件 GPIO | 原理图 GPIO/网络 | 对照结果 |
| --- | --- | --- | --- |
| RGB 红灯 | `PC13` | `PC13` / `STAT_LED_R` | 一致 |
| RGB 绿灯 | `PC14` | `PC14` / `STAT_LED_G` | 一致 |
| RGB 蓝灯 | `PC15` | `PC15` / `STAT_LED_B` | 一致 |
| 配置按键 | `PB9` | `PB9` / `MCU_KEYB`，另有 `PB8` / `MCU_KEYA` | 当前只使用 `SW2`；是否支持 `SW1` 待确认 |
| USB D+ 上拉 | `PA8` | `PA8` / `USB_DP_PU` | 一致 |
| USB 供电有效检测 | `PA0` 输入 | `PA0` / `USB_AUTO_EN` | 一致；外部高有效输入，固件只读 |
| 目标供电控制 | 无独立 MCU 输出 | `PA0` / `USB_AUTO_EN` | 一致；两个目标电源开关由硬件公共网络控制 |
| 射频 NSS | `PA4` | `PA4` / `RF_NSS` | 一致 |
| 射频 SCK/MISO/MOSI | `PA5/PA6/PA7` | `PA5/PA6/PA7` / `RF_SCK/RF_MISO/RF_MOSI` | 一致 |
| 射频 RX_EN | `PA1` | `PA1` / `RF_RX_EN` | 一致 |
| 射频 TX_EN | `PA2` | `PA2` / `RF_TX_EN` | 一致 |
| 射频 NRESET | `PA3` | `PA3` / `RF_NRESET` | 一致 |
| 射频 BUSY | `PB1` | `PB1` / `RF_BUSY` | 一致 |
| 射频 DIO1 | `PB5` | `PB5` / `RF_DIO1` | 一致 |
| 目标 SWCLK | `PB13` | `PB13` / `TGT_SWCLK_C` | 一致 |
| 目标 SWDIO | `PB12` | `PB12` / `TGT_SWDIO_C` | 一致 |
| 目标 NRST | `PB15` | `PB15` / `TGT_NRST_C` | 一致 |
| 目标 BOOT | `PB14` | `PB14` / `TGT_BOOT_C` | 一致；当前仍只配置为输入 |
| 目标 USART TX/RX | `PA9/PA10` | `PA9/PA10` / `TGT_TXD_C/TGT_RXD` | GPIO 一致 |

## 待确认项

1. 当前配置按键逻辑只读取 `PB9`。原理图新增或保留了 `PB8/MCU_KEYA` 与 `PB9/MCU_KEYB` 两个按键网络。必须确认 `SW1` 是否需要参与短按/长按配置逻辑。
2. `RF_DIO2`（`PB0`）和 `RF_DIO3`（`PB10`）已连接到 `U6`，但当前 `radio_hal.c` 只使用 `RF_DIO1`。必须确认 DIO2/DIO3 是否由模块内部功能使用，还是固件需要新增处理。
3. `BOARD_TGT_BOOT_*` 当前只在板级初始化中配置为高阻输入，代码没有主动驱动目标 BOOT。必须确认这是预期行为。

## 建议校对顺序

1. 确认 `SW1`、`SW2` 的产品功能分工。
2. 确认目标接口中 `TGT_SWDIO_C/TGT_SWCLK_C/TGT_NRST_C/TGT_BOOT_C` 的串联电阻方向和有效电平。
3. 在真实硬件上验证 `USB_AUTO_EN` 高电平时两个目标电源开关均导通，并验证 `board_usb_power_present()` 的读值。
