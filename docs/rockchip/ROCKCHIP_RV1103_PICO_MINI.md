# openvela on Rockchip RV1103 / Luckfox Pico Mini

本文件整理本仓 `board/rockchip/` 下 RV1103 移植的结构与结论。原始、更详细的
逐阶段（Stage 0 → Stage 7F）记录在同目录的板级 README：
`board/rockchip/boards/rv1103/luckfox-pico-mini/README.md`（约 38 KB，本文件是
它的中文总览与证据边界说明）。

> **证据口径**：RV1103 的移植采用**逐阶段、每阶段留可回滚镜像**的方式推进。
> 每个阶段都有独立的 `boot-stage*-known-good.img` 与 SHA-256。本文件只把
> 板级 README 明确标注为 hardware-verified 的项目列为已完成。

## 概述

这是 RV1103 的单核 Cortex-A7 移植，面向 **SPI-NAND 版本的 Luckfox Pico Mini**。
它有意**替换应用核上的 Linux**；MCU/Linux AMP 共存推迟到独立系统稳定之后。

首版镜像保留官方 BootROM、DDR blob、SPL/idblock 与 U-Boot，
**只替换 4 MiB `boot` 分区的载荷**——这样 Rockchip 的 DDR 初始化与 SPI-NAND
坏块处理仍由厂商启动链负责。

## 硬件规格

| 项目 | 规格 |
| --- | --- |
| SoC | Rockchip RV1103（RV1103/RV1106 系列） |
| CPU | 单核 Cortex-A7，ARMv7-A |
| 控制台 | UART2 @ `0xff4c0000`，IRQ 59，24 MHz，115,200 baud |
| 中断控制器 | GICv2 distributor/CPU interface @ `0xff1f1000` / `0xff1f2000` |
| 定时器 | ARM architectural timer，24 MHz，physical timer PPI/IRQ 29 与 30 |
| 进入时执行状态 | **ARM Secure state**（厂商 U-Boot 不切 Non-secure） |
| 固件加载/向量地址 | `0x00800000` |
| openvela DDR 窗口 | `0x00800000` – `0x02ffffff`（40 MiB，`CONFIG_RAM_SIZE=41943040`） |
| 保留边界 | `0x03000000`（与官方 TEE 加载地址一致） |
| 存储 | **SPI NAND，128 MiB**（`EF AE 21`） |
| 相机 | SC3336，2-lane MIPI D-PHY，2304×1296 |

**SPI NAND 分区布局**：256 KiB env、256 KiB idblock、512 KiB U-Boot、
4 MiB boot、30 MiB oem、6 MiB userdata、85 MiB rootfs。

**关于进入 Secure state**：厂商 U-Boot 不做 Secure → Non-secure 切换。本移植
保留已验证的**非 TrustZone** NuttX GIC/异常模型，并同时注册两个
physical-timer PPI——这与 32 位 Linux 应对固件相关 Secure/Non-secure 路由的
策略一致。

## 已适配组件与状态

### 芯片层（`chips/rv1103/`）

| 组件 | 文件 | 说明 |
| --- | --- | --- |
| 启动 / 内存映射 | `rv1103_boot.c`、`rv1103_memorymap.c/.h` | 已移植 |
| 中断 | `rv1103_irq.c` + `include/irq.h` | GICv2，已移植 |
| Timer | `rv1103_timer.c` | 24 MHz architectural timer |
| GPIO | `rv1103_gpio.c` + `include/rv1103_gpio.h` | GPIO v2 写掩码寄存器约定 |
| I2C | `rv1103_i2c.c` | 已移植 |
| SPI | `rv1103_spi.c` | 已移植 |
| **DMA (PL330)** | `rv1103_dma.c` + `include/rv1103_dma.h` | 8 通道 / 16 事件 / 64-bit 数据总线 |
| **SFC NAND** | `rv1103_sfc_nand.c` + `include/rv1103_sfc_nand.h` | SPI NAND，片上 ECC |
| SDMMC | `rv1103_sdmmc.c` + `include/rv1103_sdmmc.h` | 可插拔 SD 检查点 |
| SARADC | `rv1103_saradc.c` | 已移植 |
| PWM | `rv1103_pwm.c` | 已移植 |
| **TRNG** | `rv1103_trng.c` | 独立非安全 TRNG v1 |
| **TSADC** | `rv1103_tsadc.c` + `include/rv1103_tsadc.h` | 片上温度传感器 |
| USB / DWC3 | `rv1103_usb.c` + `include/rv1103_usb.h` | PHY + DWC3 外设检查点 |
| USB CDC ACM | `rv1103_usb_cdc.c` | DWC3 CDC ACM 设备 |
| Watchdog | `rv1103_wdt.c` + `include/rv1103_wdt.h` | DesignWare，复位模式 |
| 外设汇总 | `rv1103_periph.c` + `include/rv1103_periph.h` | 板级外设注册 |

### 板级（`boards/rv1103/luckfox-pico-mini/src/`）

| 文件 | 说明 |
| --- | --- |
| `luckfox_boardinit.c` | 板级初始化 |
| `luckfox_gpio.c` | GPIO 封装 |
| `luckfox_peripherals.c` | 外设注册 |
| `luckfox_sc3336.c`（约 63 KB） | SC3336 相机：MIPI D-PHY、CSI-2、RKCIF、V4L2 |
| `luckfox_sdmmc.c` | SDMMC 封装 |

## 构建与产物

```sh
cd quickly-openvela
./nuttx/tools/build.sh \
  vendor/rockchip/boards/rv1103/luckfox-pico-mini/configs/nsh \
  --cmake -j8

# 打包 SPI-NAND 启动镜像
bash vendor/rockchip/boards/rv1103/luckfox-pico-mini/tools/mk-spinand-boot.sh
```

- `configs/nsh/defconfig` 为初始 SPI-NAND bring-up 配置，关键项包括
  `CONFIG_RAM_SIZE=41943040`（40 MiB）、`CONFIG_FS_LITTLEFS=y`、
  `CONFIG_RV1103_SFC_NAND=y`、`CONFIG_RV1103_PL330_DMA=y`、
  `CONFIG_RV1103_SC3336_CHECKPOINT=y`。
- `tools/openvela-spinand.its` 是 FIT 源描述，`tools/mk-spinand-boot.sh`
  生成可写入 `boot` 分区的镜像。

## 烧录与固件打包

首版镜像保留官方 BootROM / DDR blob / SPL(idblock) / U-Boot，
只替换 4 MiB `boot` 分区载荷。这一策略把 Rockchip 的 DDR 初始化与
SPI-NAND 坏块管理留在厂商启动链里，显著降低变砖风险。

每个阶段都归档了可回滚的已知good镜像：

| 阶段镜像 | SHA-256（前 16 位） |
| --- | --- |
| `boot-stage0-timer-known-good.img` | `eabcf439bfa3fb40` |
| `boot-stage1-gpio-known-good.img` | `1e5df6445dd7d0df` |
| `boot-stage1-watchdog-known-good.img` | `c42a1287fd90464d` |
| `boot-stage1-pl330-known-good.img` | `4271b3bfacf25f02` |
| `boot-stage2-tsadc-known-good.img` | `68cdab04339c6d0d` |
| `boot-stage2-trng-known-good.img` | `814fbecfa5d9d146` |
| `boot-stage2-spinand-ro-known-good.img` | `b33108e6b11f73a7` |
| `boot-stage2-spinand-userdata-write-known-good.img` | `35094618f879d1a0` |
| `boot-stage2-spinand-littlefs-known-good.img` | `6aaef51f6836427c` |

## 分阶段验收结果（板级 README 标注为 hardware-verified）

| 阶段 | 内容 | 实测证据 |
| --- | --- | --- |
| **Stage 0** | NSH、40 MiB RAM 窗口、GIC、24 MHz architectural timer | `time "sleep 5"` 约 5 秒返回，后续 `sleep`/`uptime` 正常 |
| **Stage 1** | GPIO/pinctrl；官方高有效工作 LED（GPIO3_C6）→ `/dev/workled` | 输出/回读与 timer 回归通过 |
| **Stage 1** | DesignWare watchdog → `/dev/watchdog0` | 启动后保持停止，仅应用显式请求才启动；停止喂狗后板子复位并完整走完 BootROM/SPL/U-Boot/openvela 链 |
| **Stage 1** | PL330 DMA @ `0xff420000` | 报告 ARM peripheral ID `0x00241330`、8 通道、16 事件、64-bit 数据总线、非安全 manager；同步 channel-0 memcpy 通过 4096 字节缓存一致性与 guard 测试 |
| **Stage 2** | TSADC channel 0 → `/dev/uorb/sensor_temp0` | 遵循官方 v9 模拟电源/时钟/复位/转换表序列；初始 code 552（53.514 °C），5 次读数约 50.54 °C。**只读**：热中断、告警阈值与两条 TSHUT 路径均保持关闭 |
| **Stage 2** | TRNG v1 → `/dev/random`、`/dev/urandom` | 校验官方硬件版本 `0x46bc`、等待 seeded 状态、50 ms 超时、拒绝全零或连续重复的 256-bit 块；启动自检 + 两个不同样本 + 两个设备各 64 KiB 读取通过 |
| **Stage 2** | SPI-NAND/MTD（只读） | 只识别本板 `EF AE 21` 128 MiB 器件，用片上 ECC 读流程，暴露裸设备 + 7 个官方分区。**物理只读**：erase/写/标记坏块/改保护全部返回 `-EROFS`，控制器从不发写使能、编程或擦除命令。硬件确认 SFC v6、ID `ef ae 21`、几何参数、`boot[0]=d00dfeed` |
| **Stage 2** | SPI-NAND userdata 写入 | 只允许在物理 6 MiB `userdata` 范围（`0x02300000..0x028fffff`）内编程与擦除，其他地址在发 NAND 命令前就被拒绝；字节写、批量擦除、标记坏块仍禁用；每页编程后立即回读比对。2 KiB 页擦/写/回读、跨完整重启保留、受保护 boot FIT magic 仍为 `d00dfeed` |
| **Stage 2** | LittleFS 持久存储 | LittleFS 直接挂 `/dev/nand-userdata` 到 `/data`，2 KiB 读/编程单元与 128 KiB 擦除单元与 NAND 几何精确匹配；首次格式化、建文件、64 KiB 随机文件复制/比对、卸载重挂、完整重启后数据保留；`autoformat` 限定在 userdata MTD 分区，挂载失败不阻止 NSH 启动 |
| **Stage 3** | 板级外设组合 | 见板级 README「Stage 3: board peripheral bundle」 |
| **Stage 4** | 可插拔 SD/MMC 检查点 | 见板级 README「Stage 4」 |
| **Stage 4B/4C** | USB2 PHY + DWC3 外设检查点、DWC3 CDC ACM 设备 | 见板级 README 对应章节 |
| **Stage 7** | SC3336 + 2-lane MIPI D-PHY + CSI-2 host + 一帧 packed RAW10 RKCIF DMA @ 2304×1296 | `/dev/camera0` 为已知good冻结首帧回退；Stage-7B 增加有界 60 帧 ping-pong DMA 测试、两个 DMA 缓冲的独立哈希、FIFO/错误与超时检查、只读标准 V4L2 节点 `/dev/video0` |
| **Stage 7E** | ISP unity-gain 主通路检查点（Fix 5/6/7） | 官方 fast-path 顺序、干净的 sensor 启动、VICAP ID0 归属与专用 RAW feeder 缓冲 |
| **Stage 7F** | 有界连续 RKISP32 NV12 → `/dev/video1` | 见板级 README 对应章节 |

## 关键技术难点与解决

### 1. Rockchip GPIO v2 的写掩码寄存器约定（`rv1103_gpio.c`）

GPIO3 的寄存器数据来自官方 RV1103/RV1106 Linux BSP：

```text
GPIO3 基址           0xff550000
PCLK 门控            VI CRU gate 1 bit 15
GPIO3_C6 IOC mux     0xff558054
```

实现显式打开时钟、选择 mux function 0、**在把引脚切成输出之前先写入初始
电平**（避免切换瞬间的毛刺），并使用 GPIO v2 的写掩码寄存器约定。
这一段是"必须看官方 BSP 而不是猜"的典型：IOC mux 寄存器地址与 CRU 门控位
都不是通用规则能推出来的。

### 2. SPI-NAND 的分级写入保护（`rv1103_sfc_nand.c`）

这是本项目在安全性上做得最克制的一个驱动，采用**逐级放开**：

```text
第 1 级：完全只读           → 暴露裸设备 + 7 分区，erase/write/markbad 全 -EROFS
第 2 级：仅 userdata 可写   → 地址范围外的请求在发 NAND 命令前拒绝
                               字节写 / 批量擦除 / 标记坏块 仍禁用
                               每页编程后立即回读比对
第 3 级：LittleFS on userdata → 文件系统层保证 2 KiB/128 KiB 几何匹配
```

关键设计：`autoformat` 被限制在 userdata MTD 分区内；挂载失败不阻止 NSH
启动（避免存储故障导致整机不可用）；每阶段都验证受保护的 `boot` 分区 FIT
magic 仍为 `d00dfeed`。

### 3. 非 TrustZone 模型下的定时器路由

厂商 U-Boot 交接到 ARM **Secure** state 且不切 Non-secure。移植保留已验证的
非 TrustZone NuttX GIC/异常模型，并**同时注册两个 physical-timer PPI
（29 与 30）**，与 32 位 Linux 的应对策略一致。这是"不强行套用标准
TrustZone 流程"的务实选择。

### 4. SPI-NAND 启动的保留式替换

只替换 4 MiB `boot` 分区载荷，保留官方 BootROM / DDR blob / SPL(idblock) /
U-Boot。好处是把 DDR 初始化与坏块管理留在厂商链里；代价是 `boot` 分区
只有 4 MiB，镜像必须控制体积。

### 5. 相机链路的逐级推进（`luckfox_sc3336.c`，约 63 KB）

按 sensor → MIPI D-PHY → CSI-2 host → RKCIF DMA → V4L2 → ISP 的顺序逐级推进，
每一级都有独立检查点。RKISP32 被**刻意只做时钟与检查、暂不启动**，
直到前端 RAW 路径稳定——这避免了"ISP 参数与 sensor 配置互相掩盖问题"。

## 已知限制与未完成项

1. **RKISP32 未启动**：只做时钟与检查。
2. **TSADC 只读**：热中断、告警阈值、两条 TSHUT 路径均关闭。
3. **SPI-NAND 写入面窄**：只有 6 MiB `userdata` 可写；字节写、批量擦除、
   标记坏块不支持。
4. **MCU/Linux AMP 共存推迟**：当前是单核独立系统。
5. **Stage 7B/7E/7F 属于候选检查点**：60 帧 ping-pong、ISP unity-gain、
   RKISP32 NV12 等有界测试的完整验收结论以板级 README 各节为准。
6. 本移植是**早于 A733 的独立适配工作**，其 `vendor/rockchip/...` 路径需要
   与本仓 manifest 的 linkfile 映射配合使用。

## 证据边界

| 能力 | 板级 README 声称 | 本仓判定 |
| --- | --- | --- |
| Stage 0/1/2（timer、GPIO、WDT、PL330、TSADC、TRNG、SPI-NAND 只读/写/LittleFS） | 明确标注 hardware-verified，且每阶段有已知good镜像与 SHA-256 | **真机已验证**（以板级 README 记录为准） |
| Stage 3/4/4B/4C（外设组合、SD/MMC、USB PHY/DWC3、CDC ACM） | 有独立章节 | 以板级 README 各节为准 |
| Stage 7/7B（SC3336 + MIPI + CSI-2 + RKCIF + V4L2） | 明确标注 hardware-verified | **真机已验证**（以板级 README 记录为准） |
| Stage 7E/7F（ISP unity-gain、RKISP32 NV12） | 候选检查点 | 以板级 README 各节为准 |
