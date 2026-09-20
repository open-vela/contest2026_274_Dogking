# Rockchip RK3506 / Luckfox Lyra Zero-W openvela 移植说明

> 本文档是对仓库 `vendor/rockchip/boards/rk3506/luckfox-lyra-zero-w/README.md`（下称“板级 README”）
> 与同目录 `chips/rk3506`、`boards/rk3506/luckfox-lyra-zero-w/{configs,src,tools,scripts}` 源码的整理。
> 文中所有事实性陈述都标注了来源；**README 的完成度声明与“本文档核实程度”被严格区分**。
> 未在原始材料中出现的内容一律标注为“未记录”，不做推测性补全。

- 板级 README：`boards/rk3506/luckfox-lyra-zero-w/README.md`（394 行）
- 芯片源码：`chips/rk3506/`（`rk3506_boot.c`、`rk3506_gpio.c`、`rk3506_i2c.c`、`rk3506_spi.c`、
  `rk3506_sdmmc.c`、`rk3506_irq.c`、`rk3506_lowputc.c`、`rk3506_memorymap.c`、`rk3506_serial.c`、`rk3506_timer.c`）
- 板级源码：`boards/rk3506/luckfox-lyra-zero-w/src/`（`luckfox_boardinit.c`、`luckfox_gpio.c`、
  `luckfox_i2c.c`、`luckfox_spi.c`、`luckfox_sdmmc.c`、`luckfox_wifi.c`）

---

## 概述

板级 README 开篇原文：

> This is the single-core RK3506 port. Rockchip U-Boot initializes DDR, clocks and pin multiplexing,
> loads `amp.img` from the `amp` partition, and hands CPU0 to openvela. Linux and multi-OS AMP
> coexistence are intentionally out of scope for this configuration.

即：这是 **单核 RK3506 移植**；DDR、时钟、引脚复用全部由 Rockchip U-Boot 完成，U-Boot 从 `amp` 分区
加载 `amp.img` 并把 CPU0 交给 openvela；**Linux 与多 OS AMP 共存被明确排除在本配置范围之外**。

关键架构特征（README + 源码）：

| 项目 | 内容 | 来源 |
| --- | --- | --- |
| 启动链 | BootROM → U-Boot（DDR/时钟/引脚复用/UART0）→ FIT `amp.img` → openvela on CPU0 | README:3-8；`chips/rk3506/rk3506_boot.c:16-19` |
| 运行模式 | 单核（uniprocessor），不启用 SMP | README:10；`chips/rk3506/Kconfig:9` |
| OP-TEE | 占据 openvela 加载地址以下的低地址；U-Boot 加载时会重定位到 DDR 顶部，但 AMP 交接后不再执行，因此 Zero W 专用配置可回收该区域 | README:33-37 |
| 两套配置 | `configs/nsh`（127 MiB，兼容 128 MiB Lyra B 与 512 MiB Zero W）；`configs/nsh_512m`（Zero W 专用） | README:28-31 |

板级 Kconfig 只有一个布尔项 `BOARD_RK3506_UBOOT_HANDOFF`（默认 y），其帮助文本说明本板通过
Rockchip U-Boot 的 AMP/FIT 装载器进入，DDR/时钟/引脚复用/UART0 均由 U-Boot 初始化
（`boards/rk3506/luckfox-lyra-zero-w/Kconfig:7-13`）。

---

## 硬件规格

以下数据**全部来自板级 README 的 “Current hardware contract” 章节**（README:8-26）及板级/芯片头文件。

| 项目 | 规格 | 来源 |
| --- | --- | --- |
| SoC 名称 | README 仅写 “RK3506” / 标题 “openvela on RK3506 / Luckfox Lyra Zero W”；**未记录具体型号后缀、CPU 主频、NPU、GPU** | README:1-2 |
| CPU | Cortex-A7，ARMv7-A，hard-float，uniprocessor（单核） | README:10 |
| NPU | **README 未记录 NPU 是否存在，也未记录任何 TOPS 数值** | —（README 全文无 NPU/TOPS 字样） |
| DDR 容量 | 板卡层面：Lyra B 为 128 MiB、Lyra Zero W 为 512 MiB；openvela 窗口：`nsh` = `0x00100000`–`0x07ffffff`（127 MiB），`nsh_512m` = `0x00100000`–`0x1fffffff`（511 MiB） | README:15-16, 28-31 |
| 存储 | 启动 SD 卡：`/dev/mmcsd0`，固定在场（fixed-present），4-bit SDIO；板载启动 SPI-NAND 挂在独立的 FSPI 控制器上（openvela 不驱动它） | README:23, 249 |
| 可写分区 | GPT `rootfs` 分区，以 FAT 挂载到 `/data` | README:25 |
| 板载无线 | AIC8800DC，接在 **USB2 OTG1**（不是 SDIO），电源使能 GPIO1_C5（高有效） | README:22 |
| 控制台 | UART0 @ `0xff0a0000`，IRQ 66，24 MHz，**1,500,000 baud**，8-N-1 | README:11 |
| 中断控制器 | GICv2 @ `0xff581000` / `0xff582000` | README:12 |
| 系统定时器 | ARM generic non-secure physical timer，IRQ 30 | README:13 |
| 固件加载/向量地址 | `0x00100000` | README:14 |
| L1 页表 | 位于所选 DDR 窗口最后 16 KiB，从堆中排除；`nsh_512m` 放在 `0x1fffc000` | README:17, 36-37 |
| 活动 LED | GPIO1_A0，高有效，`/dev/actled` | README:18 |
| I2C2 | `/dev/i2c2`，GPIO0_A0 SDA / GPIO0_A1 SCL（经 RMIO），默认 100 kHz | README:19-20 |
| SPI0 | `/dev/spi0`，GPIO0_C0/C1/C2/C3 = CLK/MOSI/MISO/CS0 | README:21 |

板型字符串（源码中的确切取值）：

| 位置 | 取值 |
| --- | --- |
| `CONFIG_ARCH_BOARD_CUSTOM_NAME` | `"luckfox-lyra-zero-w"`（`configs/nsh/defconfig:10`，`configs/nsh_512m/defconfig:13`） |
| `CONFIG_ARCH_CHIP_CUSTOM_NAME` | `"rk3506"`（两份 defconfig:15/18） |
| SDK 配置名 | `luckfox_lyra_zero-w_openvela_sdmmc_defconfig`（README:116，含连字符 `zero-w`） |
| 板级目录（构建脚本参数） | `vendor/rockchip/boards/rk3506/luckfox-lyra-zero-w`（README:61 等） |

---

## 已适配组件与状态

判定口径说明：

- **README 完成时声明**：README 明确使用过去时/完成时（`passed`、`The real board …`、`reports`）声称已通过硬件的结果。
- **README 验收判据**：README 使用 `must` / `expected` 描述的检查点要求；这类文字描述的是“合格标准”，README 并未在同一段中声明“已观测到通过”。
- **仅源码集成**：源码存在并可在 defconfig 中启用，但 README 未给出任何通过声明。

| 组件 | 驱动文件 | 功能 | README 原文（英文原文引文） | 判定 |
| --- | --- | --- | --- | --- |
| UART0 控制台 | `chips/rk3506/rk3506_lowputc.c`、`chips/rk3506/rk3506_serial.c` + `CONFIG_16550_*`（`configs/nsh/defconfig:90-98`） | 1.5 Mbaud NSH 控制台；U-Boot 已完成初始化，故设 `CONFIG_16550_SUPRESS_INITIAL_CONFIG=y` | "the expected hand-off ends at: `NuttShell (NSH) NuttX-...` / `nsh>`" | README 验收判据（预期输出） |
| GPIO / 活动 LED | `chips/rk3506/rk3506_gpio.c` + `src/luckfox_gpio.c`（注册 `/dev/actled`） | GPIO1_A0 输出，高有效；初始化时先写低再改方向以避免上电闪一下 | "`gpio -o 1` must turn the blue activity LED on and `gpio -o 0` must turn it off." / "The GPIO driver intentionally initializes the LED low before changing the pin direction to avoid a boot-time flash." | README 验收判据；驱动实现细节与源码一致（`src/luckfox_gpio.c:62-72`） |
| I2C2 | `chips/rk3506/rk3506_i2c.c` + `src/luckfox_i2c.c` | RMIO 路由到 GPIO0_A0/A1，24 MHz 时钟源，内部上拉 + 施密特，100 kHz 默认 | "The first bring-up default is 100 kHz; 400 kHz support is present but should only be used after the 100 kHz hardware test is stable." / "An empty scan is valid when no I2C peripheral is populated; it must complete and return to NSH without hanging." | 已实现 + README 验收判据；**README 未声明已探测到任何从设备**；README 明确要求“A detected address is only accepted after a repeatable register read using the peripheral's data sheet.” |
| SPI0 | `chips/rk3506/rk3506_spi.c` + `src/luckfox_spi.c` | GPIO0_C0..C3 固定 M0 组；支持 mode 0/3、8/16 bit、1–4 MHz 测试频率 | "The internal-loopback tests have passed at mode 0 / 1 MHz and mode 3 / 4 MHz. The external MOSI-to-MISO checkpoint is intentionally deferred when no jumper wire is available; it is not a prerequisite for the onboard Wi-Fi work." | **内部回环：README 明确声明已通过（硬件）**；**外部 MOSI–MISO 跳线检查点：明确延后、未做** |
| SDMMC + GPT + FAT | `chips/rk3506/rk3506_sdmmc.c` + `src/luckfox_sdmmc.c` | DW-MSHC 控制器，400 kHz 识别、13 MHz 传输；PIO/IDMAC 双路径；解析 GPT 并把 `rootfs` 以 FAT 挂到 `/data` | "Both `cmp` commands must return silently. `dmesg` must contain one `IDMAC read active` and one `IDMAC write active` message, with no `IDMAC error`, data CRC, timeout, or short-transfer message." | README 验收判据（检查点清单）；README 未在该章节声明已通过。传输时钟/位宽以 README:39-46 的硬件约定形式给出 |
| AIC8800DC Wi-Fi 电源/PHY 检查点 | `src/luckfox_wifi.c`（`CONFIG_RK3506_AIC8800DC_PROBE`） | GPIO1_C5 供电，使能 USB PHY 与 OTG1 时钟，施加厂商 PHY 调优，校验 DWC2 核心 ID 与 PHY line-state | "WIFI: hardware checkpoint passed; AIC USB device is present"；"The exact low 16 bits of `GSNPSID`, interrupt status and host-port value may vary. The high 16 bits of `GSNPSID` must be `4f54`, and `linestate` must be non-zero." | **README 声明硬件检查点通过**（引文为 README 给出的成功输出样例） |
| DWC2 主机 + 根端口枚举 | 同上（`CONFIG_RK3506_AIC8800DC_ENUM_PROBE`） | 软复位、强制 host 模式、DMA/根端口初始化、EP0 SETUP/DATA/STATUS、分配地址 1、读设备/配置描述符 | "The real board reports a high-speed connection." / "The real Zero W result is `VID:PID=1a86:8091`, device class `0x09`, one interface and a 41-byte configuration descriptor. This is the onboard USB hub rather than the AIC8800DC itself." | **README 声明已在真实板卡上观测到**；同时明确“Seeing it proves root-port enumeration, but it is not sufficient to start the wireless firmware loader.” |
| USB Hub 扫描 + 子设备枚举 | 同上 | 读 hub 类描述符、逐端口上电、读/清端口变化、复位端口、以地址 ≥2 枚举高速子设备 | "The real downstream device is `a69c:88dc`, a high-speed composite device with three interfaces and a 222-byte configuration descriptor." | **README 声明已在真实板卡上观测到** |
| AIC 端点映射 + SET_CONFIGURATION | 同上 | 用多包 EP0 DMA 读完整配置描述符，打印每个接口/端点，校验厂商 WLAN 接口，发送 `SET_CONFIGURATION(1)` | "On the real board, interface 2 is the `ff/ff/ff` WLAN interface. Its 512-byte bulk endpoint pairs are `01/81` for data and `02/82` for messages…" | **README 声明已在真实板卡上观测到** |
| Bulk 只读消息（DBG_MEM_READ_REQ） | 同上 | 从 EP02 发厂商格式 `DBG_MEM_READ_REQ`，从 EP82 收确认，读 `0x40500000`（厂商 `system_config_8800dc()` 的首个芯片识别读） | "The following read-only Bulk checkpoint sends the vendor-format `DBG_MEM_READ_REQ` on EP02 and receives its confirmation on EP82." 其后只给出成功输出格式（`WIFI: AIC bulk message passed, [40500000]=........ chip-id=..`），**未写 passed/verified 字样** | 已实现；**README 未声明通过** |
| Wi-Fi 网络接口（`wlan0`） | 不存在 | — | "This hub-scan image deliberately does not provide `wlan0` yet." | **明确未实现** |

补充：Wi-Fi 检查点**只由 `configs/nsh_512m` 启用**（`configs/nsh_512m/defconfig:65-66` 有
`CONFIG_RK3506_AIC8800DC_PROBE=y` 与 `CONFIG_RK3506_AIC8800DC_ENUM_PROBE=y`；`configs/nsh/defconfig`
中两者均未出现），与 README:287-289 的说明一致：

> This checkpoint is enabled only by the Zero W `nsh_512m` configuration. The 127 MiB compatible `nsh`
> configuration remains suitable for Lyra B and does not power or probe the Zero W wireless module.

板级初始化顺序（`src/luckfox_boardinit.c:77-137`）：挂载 `/proc` → GPIO → I2C2 → SPI0 → 创建
`sdmmc-init` 线程（优先级 50、栈 4096，先睡 3 s 再探测）→ 创建 `wifi-probe` 线程（优先级 50、栈 4096，
先睡 1 s 再探测）。两个耗时探测都放在工作线程里，README 的对应解释是“so NSH and SDMMC
initialization are not blocked”（README:285）与“Keeping card discovery out of board_late_initialize also
prevents a failed probe from blocking the system's init task”（源码注释）。

---

## 构建与产物

### 1) openvela + Rockchip FIT 镜像

在 Linux 环境下、于 `quickly-openvela` 目录执行（README:54-76）。兼容 127 MiB 版本：

```sh
./nuttx/tools/build.sh \
  vendor/rockchip/boards/rk3506/luckfox-lyra-zero-w/configs/nsh \
  --cmake -j8

bash vendor/rockchip/boards/rk3506/luckfox-lyra-zero-w/tools/pack_amp.sh
```

Zero W（512 MiB DDR）专用版本：

```sh
./nuttx/tools/build.sh \
  vendor/rockchip/boards/rk3506/luckfox-lyra-zero-w/configs/nsh_512m \
  --cmake -j8

bash vendor/rockchip/boards/rk3506/luckfox-lyra-zero-w/tools/pack_amp.sh \
  cmake_out/luckfox-lyra-zero-w_nsh_512m/nuttx.bin
```

| defconfig 路径 | 启用内容差异 | 关键宏 |
| --- | --- | --- |
| `configs/nsh/defconfig` | 基础单核 NSH；无 Wi-Fi 检查点 | `CONFIG_RAM_SIZE=133169152`（127 MiB）、`CONFIG_RAM_START/VSTART=0x00100000`、`CONFIG_MMCSD_MULTIBLOCK_LIMIT=128`、`CONFIG_RK3506_SDMMC(_DMA)=y`、`CONFIG_RK3506_I2C2=y`、`CONFIG_RK3506_SPI0=y`、`# CONFIG_RK3506_SPI0_LOOPBACK is not set`、`CONFIG_NSH_MAXARGUMENTS=16` |
| `configs/nsh_512m/defconfig` | 追加 `CONFIG_RK3506_AIC8800DC_PROBE=y`、`CONFIG_RK3506_AIC8800DC_ENUM_PROBE=y` | `CONFIG_RAM_SIZE=535822336`（511 MiB）、其余同上；文件头注释警告“Do not use this configuration on the 128 MiB Luckfox Lyra B.” |

预期产物：

| 产物 | 路径 | 说明 |
| --- | --- | --- |
| openvela 原始二进制 | `cmake_out/luckfox-lyra-zero-w_nsh/nuttx.bin`（`pack_amp.sh:15` 的默认值）；512 MiB 版为 `cmake_out/luckfox-lyra-zero-w_nsh_512m/nuttx.bin`（README:75） | `CONFIG_RAW_BINARY=y` |
| FIT 镜像 | `boards/rk3506/luckfox-lyra-zero-w/amp.img`（README:83；“creates `amp.img` beside this README”） | 本仓库中该文件存在，**235,008 字节**（`LastWriteTime` 2026/8/1）；仓库内未附 SHA-256 |
| SDK 暂存副本 | `<RK3506_SDK_ROOT>/output/openvela/amp.img`（README:85-86；`pack_amp.sh:51` 的 `RK3506_AMP_STAGE` 默认值） | 供 SDK 打包阶段使用 |

FIT 打包细节（`tools/openvela.its`、`tools/pack_amp.sh`、`tools/pack_fit.py`）：

- `openvela.its`：镜像节点 `amp0`，`type=firmware`、`arch=arm`、`os=rtos`、`cpu=<0xf00>`、
  `load=<0x00100000>`、`entry=<0x00100000>`、`udelay=<10000>`，带 `hash { algo = "sha256"; }`；
  `configurations/conf` 的 `loadables="amp0"`。
- `pack_amp.sh` 调用 SDK 的 `rtos/bsp/rockchip/tools/mkimage -f openvela.its -E -p 0xe00 <out>`，
  随后执行 `mkimage -l` 校验（README:84：“validates it with the Rockchip SDK's `mkimage`”）。
- 宿主**没有 `dtc`** 时回退到无依赖的 `pack_fit.py`：它手工构造 FDT（magic `0xD00DFEED`、
  `FDT_BEGIN_NODE/PROP/END_NODE/END`），并两次构建以写死根节点 `totalsize` 属性——源码注释说明原因：
  “Rockchip's AMP loader reads the root-level `totalsize` property instead of the standard FDT header
  field.”；同时内嵌 SHA-256 `value`。README:102-104 说明这一回退产物“still contains a SHA-256 image hash
  and is parsed back by Rockchip `mkimage -l`”。

### 2) Zero W SD 卡 update 镜像（Rockchip SDK）

原生 Linux/ext4 检出（推荐）：

```sh
cd ../RK3506-ALL-Files/luckfox-lyra-sdk/lyra-zero-w
./build.sh luckfox_lyra_zero-w_openvela_sdmmc_defconfig
./build.sh all
```

README:120-128 说明该配置：用 `rk-amp` fragment 构建 U-Boot；**不构建 Linux 与根文件系统**；拷贝已暂存的
`amp.img`；使用 **4 MiB `amp` 分区**；打包 Rockchip `update.img`。产物为 `output/update/Image/update.img`
（README:128）。

在 Windows 解压后经 WSL 构建时，只修复已知丢失的符号链接并显式选择非原生文件系统
（README:130-145）：

```sh
cd quickly-openvela
bash vendor/rockchip/boards/rk3506/luckfox-lyra-zero-w/tools/repair_sdk_links.sh

cd ../RK3506-ALL-Files/luckfox-lyra-sdk/lyra-zero-w
RK_ALLOW_NON_NATIVE_FS=1 \
  ./build.sh luckfox_lyra_zero-w_openvela_sdmmc_defconfig
RK_ALLOW_NON_NATIVE_FS=1 ./build.sh all
```

`tools/repair_sdk_links.sh` 行为（源码）：扫描 `device/rockchip`、`u-boot`、`.repo/manifests` 三个
worktree 的 git 索引，取出 mode 为 `120000`（符号链接）的条目，用
`git --git-dir=<project-objects> cat-file -p <object>` 读回目标并重建链接；同时显式恢复
`.repo/projects`/`project-objects` 中被 repo 表示为链接的对象目录、`.repo/manifest.xml`、
`build.sh`、`Makefile`、`rkflash.sh`、`kernel -> kernel-6.1`。源码注释给出必要性：U-Boot 缺这些链接
虽仍可编译，但生成的版本串不完整且每次构建都会打印 “bad object HEAD”。脚本对**非空文件或目录拒绝覆盖**
（`restore_link()`：`Refusing to replace non-empty path`，README:144-145 亦有对应说明）。

宿主依赖（README:106-110）：Rockchip U-Boot 脚本需要 `python2`，此外还需要常规宿主编译器、`make`、
`bison`、`flex`、OpenSSL 开发文件和 `dtc`。

---

## 烧录与固件打包

| 工具 | 输入 | 产物 | 来源 |
| --- | --- | --- | --- |
| `tools/pack_amp.sh` | `cmake_out/.../nuttx.bin` | 板级 `amp.img`（FIT，`-E -p 0xe00`）+ SDK 暂存副本 `output/openvela/amp.img` | README:54-90；`tools/pack_amp.sh:36-56` |
| `tools/openvela.its` | `openvela.bin`（由 `nuttx.bin` 改名） | FIT 描述（load/entry `0x00100000`、cpu `0xf00`、sha256） | `tools/openvela.its:13-41` |
| `tools/pack_fit.py` | `nuttx.bin` | 无 `dtc` 时的等效 FIT（含根 `totalsize` 与 SHA-256） | README:102-104；`tools/pack_fit.py:92-158` |
| `tools/repair_sdk_links.sh` | Windows 解压的 SDK 检出 | 重建 160 个左右的符号链接中的已知项 | README:130-145；`tools/repair_sdk_links.sh:37-105` |
| SDK `build.sh`（`luckfox_lyra_zero-w_openvela_sdmmc_defconfig`） | 暂存的 `amp.img`、U-Boot（`rk-amp` fragment） | `output/update/Image/update.img`，4 MiB `amp` 分区 | README:106-128 |

烧录/打包相关的坑（均为 README 原文要点）：

1. **`amp.img` 变更后必须先跑 SDK 的 AMP 阶段**：
   > After changing `amp.img`, the SDK's AMP stage must run before firmware packing. `build.sh firmware`
   > alone reuses the previous `output/firmware/amp.img` and can silently package an old openvela binary.
   > Use `build.sh all`, or explicitly run:
   ```sh
   RK_ALLOW_NON_NATIVE_FS=1 ./build.sh amp
   RK_ALLOW_NON_NATIVE_FS=1 ./build.sh firmware
   ```
2. **外层镜像不能证明内层负载更新**：
   > Before flashing, unpacking the final update image or checking the U-Boot FIT hash is recommended.
   > A newly generated outer `update.img` does not by itself prove that its embedded AMP payload changed.
3. **串口**：3.3 V USB-to-TTL 接 UART0，**1,500,000 baud, 8-N-1**（README:149）。
4. **首次启动烟测命令**（README:157-177）：`hello` → `free` / `ps` / `dmesg` / `ls /dev` /
   `gpio -o 1 /dev/actled` / `sleep 1` / `gpio -o 0 /dev/actled`；`nsh_512m` 另需 `free` 与
   `cat /proc/meminfo`，README 要求报告的堆应接近 511 MiB 而非 ~127 MiB。
5. **IDMAC 检查点**（README:186-206）：用只读 `amp` 分区做数据源，`dd bs=16384 count=128` 经
   `/data` 复制两次并 `cmp`；`dmesg` 必须各出现一次 `IDMAC read active` / `IDMAC write active`，
   `, bounce` 后缀表示使用了 bounce 缓冲；识别阶段应为 `clock 400000 Hz, bus 1-bit (divider=65)`，
   传输阶段为 `clock 13000000 Hz, bus 4-bit (divider=2)`。
   **禁止直接写 `/dev/mmcsd0`、`/dev/uboot.raw`、`/dev/boot.raw`、`/dev/amp.raw`**（内含正在运行的固件）。
6. **I2C2 检查点**（README:208-244）：`ls /dev`、`dmesg`、`i2c bus`；期望出现 `/dev/i2c2`、
   `Bus 2: YES` 以及日志 `I2C2: /dev/i2c2 registered at 100 kHz on GPIO0_A0/A1 RMIO`；随后
   `i2c dev -b 2 -f 100000 03 77`（单字节读探测）与 `i2c dev -z -b 2 -f 100000 03 77`（零字节写探测）。
7. **SPI0 检查点**（README:246-277）：`CONFIG_RK3506_SPI0_LOOPBACK` 曾用于内部回环，**当前正常板级配置
   已关闭**；外部检查点需把 GPIO0_C1(MOSI) 与 GPIO0_C2(MISO) 直接短接，然后用
   `spi exch -b 0 -f 1000000 -m 0 -w 8 -x 8 0011223344556677` 与
   `spi exch -b 0 -f 4000000 -m 3 -w 8 -x 8 a55aa55a12345678` 校验
   （`Received` 必须等于 `Sending`）；**这几条命令行需要 `CONFIG_NSH_MAXARGUMENTS=16`**。
   接真实外设前必须拆除短接线；**绝不可把外设接到 FSPI/SPI-NAND 信号上，也不要把启动闪存当作通用 SPI0**。
8. **Wi-Fi 检查点**（README:279-394）：烧写后等待约两秒执行 `dmesg` / `ps` / `free`；成功输出样例、
   VID/PID、接口/端点映射均见上文表格。READM 明确要求 **VID/PID 必须来自真实板卡，不得按其它
   AIC8800DC 固件版本猜测**；失败时保留完整 `HCINT`/`HCTSIZ` 诊断行。

---

## 关键技术难点与解决

### 1) 启动交接与内存布局

- U-Boot 完成 DDR/时钟/引脚复用/UART0；openvela 侧 `arm_boot()` 只做三件事：
  `rk3506_setupmappings()` → `arm_fpuconfig()`（注释 “Cortex-A7 has VFPv4/NEON”）→ early serial
  （`chips/rk3506/rk3506_boot.c:21-35`）。
- MMIO 映射：把 `0xff000000` 起 `0x01000000`（16 MiB = 16 个 1 MiB 段）以 `MMU_IOFLAGS` 恒等映射，
  覆盖 UART0/GIC/GRF/CRU/GPIO 等；DDR 映射由 `arm_head.S` 建立（`chips/rk3506/rk3506_memorymap.c:12-34`）。
- 页表：`PGTABLE_SIZE 0x4000`（16 KiB），基址 = `CONFIG_RAM_START + CONFIG_RAM_SIZE - PGTABLE_SIZE*CONFIG_NCPUS`，
  并把 `CONFIG_RAM_END` 重定义为排除它（`chips/rk3506/chip.h:27-49`）；`nsh_512m` 因此落在 `0x1fffc000`
  （README:36-37）。
- 两套 DDR 窗口由 `CONFIG_RAM_SIZE` 区分（127 MiB / 511 MiB），FIT 的 load/entry 都是 `0x00100000`
  （README:78-79：“only the openvela-owned DDR limit and page-table location differ”）。

### 2) 中断与定时器

- GICv2：`up_irqinitialize()` 先断言向量表基址满足 `VBAR_MASK` 对齐，在 `CONFIG_ARCH_LOWVECTORS` 下写
  VBAR，再 `arm_gic0_initialize()` + `arm_gic_initialize()`，注释 “The first port is UP and owns the
  distributor”（`chips/rk3506/rk3506_irq.c:20-33`）。
- IRQ 编号：`RK3506_IRQ_CNTPNS 30`、`RK3506_IRQ_UART0 66`、`RK3506_IRQ_SDMMC 118`，`NR_IRQS 192`
  （`chips/rk3506/include/irq.h:12-18`）。
- 定时器：`up_timer_initialize()` 调用 `up_alarm_set_lowerhalf(arm_timer_initialize(0))`，注释说明
  频率传 0 是让 ARM generic-timer lower half 自己读 `CNTFRQ`（`chips/rk3506/rk3506_timer.c:15-17`）。
- 串口：`rk3506_lowputc.c` 是裸轮询实现（THR `+0x0000`、LSR `+0x0014`、`LSR_THRE (1<<5)`），
  完整驱动复用通用 `u16550_serialinit()`（`chips/rk3506/rk3506_serial.c:14`）。

### 3) GPIO（Rockchip v2 写掩码）

- **基地址/时钟**：GPIO1 `0xff870000`；GPIO1 IOC `0xff660000`；PCLK 门控写 `CRU 0xff9a0000 +
  0x080c (GATE_CON03)` 的 bit8+16，GPIO1 IOC 门控写 `+0x0858 (GATE_CON22)` 的 bit1+16。
  源码注释点明 **“A zero gate bit enables a Rockchip clock”**（写 0 使能）
  （`chips/rk3506/rk3506_gpio.c:21-47`）。
- **写掩码约定**：bits[31:16] 为字段掩码、bits[15:0] 为值；两种写法并存——`(bit<<16)|bit`
  （单 bit 置位，`rk3506_gpio.c:85-93`）与宏 `RK3506_WRITE_MASK(mask,val)=((mask)<<16)|((val)&(mask))`
  （`rk3506_i2c.c:45-46`、`rk3506_spi.c:39-40`、`rk3506_sdmmc.c:87-88`）。
- **半寄存器拆分**：pin ≥ 16 时寄存器偏移 `+4`、位为 `1u<<(pin&15)`（`rk3506_gpio.c:49-58`）。
- **引脚复用在本驱动内完成**：功能 0 = GPIO，写 `0x0fu << (shift+16)`，其中
  `shift=(pin%4)*4`，地址 `0xff660020 + (pin/8)*8 + ((pin%8)/4)*4`（`rk3506_gpio.c:60-73`）；
  每 8 pin 一组、每组两个 IOMUX 寄存器、每寄存器 4 个 4-bit 字段。
- **避免上电毛刺**：先写 `SWPORT_DR_L`（`+0x0000`）置电平，再写 `SWPORT_DDR_L`（`+0x0008`）置方向
  （`rk3506_gpio.c:119-127`；板级注释 “The schematic shows GPIO1_A0 driving an NPN transistor. High turns
  the activity LED on”，`src/luckfox_gpio.c:62-66`）。
- **上拉未处理**：GPIO1 路径没有写 PULL 寄存器，上拉状态沿用 U-Boot 遗留（源码 grep 确认；I2C/SPI/SDMMC
  路径才显式配置 PULL）。
- 已知源码观察（README 未提及）：`rk3506_gpio1_read()` 只读 `GPIO1 + 0x0070 (EXT_PORT)` 单字，从不读
  `+0x0074` 的高半区，因此 pin 16–27 的读取结果取自低半区（`rk3506_gpio.c:96-105`）。活动 LED 是 pin 0，
  不受影响；Wi-Fi 电源脚（pin 21）在代码中只写不读。

### 4) I2C2 与 RMIO 路由

- **寄存器**：`RK3506_I2C2_BASE 0xff060000`；控制器寄存器 `I2C_CON +0x00`、`I2C_CLKDIV +0x04`、
  `I2C_MRXADDR +0x08`、`I2C_MRXRADDR +0x0c`、`I2C_MTXCNT +0x10`、`I2C_MRXCNT +0x14`、`I2C_IEN +0x18`、
  `I2C_IPD +0x1c`、`I2C_TXDATA(n) +0x100+4n`、`I2C_RXDATA(n) +0x200+4n`（`chips/rk3506/rk3506_i2c.c:50-59`）。
- **时钟**：选 24 MHz 振荡器、分频 1 → `CLKSEL_CON33 (0xff9a0384)` 写 `WRITE_MASK(0x3f,0)`；
  `GATE_CON12 (+0x830)` 写 `WRITE_MASK((1<<2)|(1<<3),0)`；`PMU_GATE_CON00 (0xff9b0800)` 写
  `WRITE_MASK(1<<7,0)`。注释原文：“Rockchip clock gates are active high: writing zero enables the clock.”
  （`rk3506_i2c.c:546-554`）
- **RMIO（Matrix IO）路由**（这是本板最容易踩错的地方）：
  `GPIO0A_IOMUX_SEL_0 (0xff950000)` ← `WRITE_MASK(0x00ff,0x0077)`（**功能 7 选 RMIO**）；
  `RM_GPIO0A0_SEL (0xff910080)` ← `WRITE_MASK(0x007f,0x0014)`（**RMIO 功能 0x14 = I2C2 SDA**）；
  `RM_GPIO0A1_SEL (0xff910084)` ← `WRITE_MASK(0x007f,0x0013)`（**0x13 = I2C2 SCL**）。
  源码注释警告：“The generic Rockchip RTOS pin defaults use different pins and must not be copied.”
  README:210-212 同样强调不要用 RK3506 RTOS BSP 的 GPIO0_A4/A5 默认值（`rk3506_i2c.c:24-27, 556-566`）。
- **电气**：内部上拉 `GPIO0A_PULL` ← `WRITE_MASK(0x000f,0x0005)`，施密特输入 `GPIO0A_SMT` ←
  `WRITE_MASK(0x0003,0x0003)`（`rk3506_i2c.c:567-570`）。
- **纯轮询、不用中断**：`I2C_IEN` 恒写 0，状态一律读 `I2C_IPD`；轮询步进 5 µs，超时 =
  `I2C_BASE_TIMEOUT_US(20000) + len*I2C_BYTE_TIMEOUT_US(200)` µs（`rk3506_i2c.c:88-90, 466-484, 572`）。
- **分频算法**：24 MHz 输入；100 kHz 档取 `speedkhz=100`、`minhighns=5000`（4000 ns 高 + 1000 ns 上升）、
  `minlowns=5000`（4700 ns 低 + 300 ns 下降）、`startsetup=1`；400 kHz 档取 `speedkhz=400`、
  `minhighns=900`（600+300）、`minlowns=1600`（1300+300）、`startsetup=0`。计算
  `total=ceil(ratekhz/(speedkhz*8))`、`high=ceil(ratekhz*minhighns/8e6)`、`low=ceil(ratekhz*minlowns/8e6)`，
  两者下界 2，若 `high+low < total` 则把差额优先补到 `low`；最后写
  `((high-1)<<16)|(low-1)` 到 `I2C_CLKDIV`，并把 `SDA_CFG(1)|START_CFG(startsetup)` 写进 `I2C_CON`
  （`rk3506_i2c.c:143-204`）。
  - **本文档按源码整数运算推导**：100 kHz 档 `high=15, low=15` → `CLKDIV=0x000E000E`；
    400 kHz 档 `high=3, low=5` → `CLKDIV=0x00020004`。源码把计数单位统一按 8 个输入时钟换算
    （`total=ceil(clk/(speed*8))`），据此 100 kHz 档合计 30×8=240 个 24 MHz 周期 = 10 µs（正好 100 kHz），
    400 kHz 档合计 8×8=64 周期 ≈ 2.67 µs（≈375 kHz，满足最小高/低电平时间）。
    **README 未给出实测 SCL 频率**，以上仅为按源码公式的推导，需示波器/逻辑分析仪复核。
- **控制器行为怪癖**：接收按 32 字节分块，仅最后一块置 `I2C_CON_LAST_NACK`；发送把 8×32 bit 打包、
  首字节为地址；当 `msg->length < 4` 时把“写寄存器地址 + 读数据”融合成一次硬件事务
  （`I2C_MODE_REGISTER_TX`，`*consumed = 2`），源码注释称
  “This is the only reliable true repeated-start form exposed by this controller”（`rk3506_i2c.c:226-290, 424-447`）。
- **错误路径**：NACK → `-ENXIO`；超时 → `-ETIMEDOUT`；`I2C_M_TEN`/`I2C_M_NOSTART`、负长度、空缓冲 →
  `-ENOTSUP`；互斥保护用 `NXMUTEX_INITIALIZER`（`rk3506_i2c.c:133, 309, 414-418, 490, 507`）。

### 5) SPI0

- **寄存器布局与 DW_apb_ssi/Synopsys SSI 兼容**（按偏移推断，源码未点名控制器型号）：
  `CTRLR0 0x00`、`CTRLR1 0x04`、`ENR 0x08`、`SER 0x0c`、`BAUDR 0x10`、`TXFTLR 0x14`、`RXFTLR 0x18`、
  `TXFLR 0x1c`、`RXFLR 0x20`、`SR 0x24`、`IMR 0x2c`、`ICR 0x38`、`DMACR 0x3c`、`TXDR 0x400`、`RXDR 0x800`
  （`chips/rk3506/rk3506_spi.c:42-56`）。基地址 `RK3506_SPI0_BASE 0xff120000`（`:29`）。
- FIFO 深度 64，阈值 31（`SPI_FIFO_DEPTH 64`，`:68, 374-375`）；输入 24 MHz、默认 1 MHz（`:69-70`）。
- 分频：`divider=ceil(24e6/f)` 后取偶 `(divider+1)&~1`，钳位 `[2,0xfffe]`；改分频前先写 `CTRLR0=0`、
  `ENR=0`（`:150-151, 182-191`）。1 MHz→div 24；4 MHz→div 6。
- 模式：mode 1/3 置 CPHA、mode 2/3 置 CPOL（`:136-144`）；位宽仅 8/16 bit（`:58-59, 217`）。
- 片选：**硬件 CS 手动翻转**——`select()` 直接 `putreg32(selected?1:0, SPI_SER)` 且忽略 `devid`；
  `SPI_CR0_SSD_ONE` 保持传输间 CS 有效（`:62, 162-168`）。
- 内部回环：`CONFIG_RK3506_SPI0_LOOPBACK` 仅在 `rk3506_spi_configure()` 中置 `SPI_CR0_LOOPBACK (1u<<25)`，
  没有其它回环代码路径（`:65, 146-148`）。
- 传输：32-bit 访问宽度（`SPI_CR0_APB_8BIT (1<<13)`、`ENDIAN_BIG (1<<11)`），TX 填充受
  `txwords-rxwords < 64` 与 `TXFLR < 64` 约束，空闲时填充 0xff/0xffff，起始前先排空 RX FIFO，
  传输期间置/清 `ENR`（`:243-308`）。
- 超时：交换超时 `MSEC2TICK(100 + nwords*nbits*4000/frequency)`，收尾等待 `SR & SPI_SR_BUSY`
  最多 100 ms；失败日志 `"ERROR: SPI0 transfer timeout (%zu/%zu words)\n"`（`:250-251, 296, 302-306`）。

### 6) SDMMC（DesignWare MSHC）+ IDMAC

- **寄存器/时钟**：`RK3506_SDMMC_BASE 0xff480000`（`chips/rk3506/include/rk3506_sdmmc.h:36`），IRQ 118；
  FIFO 为 256 字 × 4 B = 1024 B 双向。源码保留 U-Boot 选定的 **52 MHz** SDMMC 源时钟，只解除门控：
  `GATE_CON17 (+0x844)` 的 bit6|bit7 与 `GATE_CON19 (+0x84c)` 的 bit8
  （`chips/rk3506/rk3506_sdmmc.c:67-77, 3128-3131`）。
  ```c
  /* RK3506 integration registers.  U-Boot already selected a 52 MHz SDMMC
   * source clock.  The first openvela storage stage deliberately preserves
   * that source and only ungates the clocks, configures the SD pins and uses
   * the controller's own divider. */
  ```
- **时钟/位宽**：`BOARD_SDMMC_FREQUENCY 52000000`、`BOARD_SDMMC_IDENT_TARGET 400000`、
  `BOARD_SDMMC_TRANSFER_TARGET 25000000`、`BOARD_SDMMC_SAFE_TARGET 12500000`，宏
  `BOARD_SDMMC_CEIL(a,b)`；`SDMMC_CLKDIV0(n)=((n+1)>>1)`（board.h:15-36、`rk3506_sdmmc.h:164`）。
  据此：识别 `CEIL(52e6,400k)=130 → CLKDIV 65 → 52e6/(2*65)=400 kHz`；
  传输 `CEIL(52e6,25e6)=3 → CLKDIV 2 → 13 MHz`；切回 `SAFE_TARGET` 时 `CEIL(52e6,12.5e6)=5 → CLKDIV 3
  → 8.67 MHz`——与 README:50-52 的 “approximately 8.67 MHz” 及 README:203-204 打印的
  `divider=65`/`divider=2` 完全一致。日志格式：
  `"SDMMC: clock %lu Hz, bus %u-bit (divider=%lu)\n"`（`rk3506_sdmmc.c:1716-1723`）。
- **PIO 与 IDMAC 的分工**：`buflen < RK3506_IDMAC_MINSIZE (512)` 或 `> RK3506_IDMAC_MAXSIZE
  (16 描述符 × 4096 = 64 KiB)` 走 PIO；**每次 IDMAC 复位失败也回退 PIO**；cache 行不对齐的缓冲使用
  bounce 缓冲（`rk3506_sdmmc.c:147-150, 2810-2834, 2862-2891`）。这与 README:39-46 的
  “Multi-block requests are limited to 128 sectors (64 KiB), which matches the current IDMAC descriptor
  and bounce-buffer capacity.” 一致。
- **IDMAC 描述符**：`struct sdmmc_dma_s {des0..des3}`，32 B 对齐；标志
  `OWN(1<<31)`、`CH(1<<4)`、`FS(1<<3)`、`LD(1<<2)`、`DIC(1<<1)`，`MCI_DMADES1_MAXTR 4096`；
  `des2 = buffer+offset`，`des3 = 下一描述符`（最后一个为 0）；`ndesc = ceil(buflen/4096)`；
  写 `DBADDR` 前对描述符数组做 `up_clean_dcache`；随后 `CTRL |= INTDMA|DMAENABLE`、
  `BMOD = FB|DE|PBL_4XFRS`、`FIFOTH` 使用 `DMABURST_4XFRS`
  （`rk3506_sdmmc.c:59-65, 2905-2962`）。
- **bounce 缓冲**：静态 64 KiB、32 B 对齐；写路径 `memcpy` + `up_clean_dcache`，读路径
  `up_invalidate_dcache` + 在中断里回拷（`:1255-1266, 2823-2850`）。
- **复位回退**：`rk3506_wait_clear()` 以 1 µs 步进轮询最多 100 ms，源码注释解释原因——
  “reset can run with interrupts disabled during early board initialization”（`:663-694`）；
  失败时打印 `"SDMMC: %s timeout reg=%08lx mask=%08lx\n"`，名称含 `BMOD reset`、`CTRL reset`、
  `IDMAC bus reset`、`read FIFO reset`、`write FIFO reset`。
- **命令怪癖（关键）**：**所有命令都加 `SDMMC_CMD_USEHOLD (1<<29)`**，源码注释：
  > Without it, read commands happen to work on RK3506, but the data driven after CMD24 can violate the
  > card-side setup window and is rejected with DCRC
  （`rk3506_sdmmc.c:1880-1886`）；CMD12 追加 `STOPABORT`，CMD0 追加 `SENDINIT`。
- **只读策略**：`CONFIG_MMCSD_READONLY` 下 `MMCSD_WRDATAXFR` 返回 `-EROFS`
  （日志 `"ERROR: Write command rejected in read-only mode\n"`）；卡状态固定为 `SDIO_STATUS_PRESENT`
  （“This first storage stage is intentionally fixed-media and read-only”，`:1471-1478`）；
  R4/R5 响应类型未实现（`-ENOSYS`）。
- 逐字错误日志（便于现场比对）：`"SDMMC: CIU command start timeout cmd=%08lx status=%08lx rintsts=%08lx\n"`、
  `"SDMMC: transfer timeout dma=%u write=%u remaining=%lu status=%08lx rintsts=%08lx idsts=%08lx\n"`、
  `"SDMMC: data CRC error pending=%08lx remaining=%ld\n"`、`"SDMMC: short transfer remaining=%ld\n"`、
  `"SDMMC: IDMAC error idsts=%08lx dsc=%08lx buf=%08lx\n"`、
  `"SDMMC: IDMAC read active (%lu bytes%s)\n"` / `"… write active …"`、
  `"SDMMC: IDMAC fast reuse active\n"`（`:647, 900, 1162, 1280, 1344, 2896, 2971-2996`）。
- 板级注册（`src/luckfox_sdmmc.c`）：`mmcsd_slotinitialize(0, sdio)` → `parse_block_partition("/dev/mmcsd0")`
  → 依次用 `bchdev_register` 生成 `/dev/uboot.raw`、`/dev/boot.raw`、`/dev/amp.raw`（只读）与
  `/dev/rootfs.raw`（可写），并把 `/dev/rootfs` 以 `vfat` 挂到 `/data`；最后再以只读方式暴露整卡
  `/dev/sdcard`。

### 7) AIC8800DC Wi-Fi 检查点（DWC2 主机 + 厂商 Bulk 协议）

寄存器与地址（`src/luckfox_wifi.c:32-67`）：

| 名称 | 地址 | 备注 |
| --- | --- | --- |
| CRU | `0xff9a0000` | `GATE_CON01 = +0x0804`（写 `WRITE_MASK(1<<4,0)`）、`GATE_CON07 = +0x081c`（写 `WRITE_MASK(0x0f00,0)`） |
| USB PHY | `0xff2b0000` | `PHY1_TUNE +0x0430`、`PHY1_LINESEL +0x0494`、`PHY_CLKOUT +0x041c` |
| GRF | `0xff288000` | `GRF_USBPHY_CON1 +0x0070`、`GRF_USBPHY_STATUS +0x0118` |
| USB2 OTG1 (DWC2) | `0xff780000` | `GSNPSID +0x0040`、`GINTSTS +0x0014`、`HPRT0 +0x0440`、`GRSTCTL +0x0010`、`GHWCFG2 +0x0048`，主机通道基址 `+0x0500 + ch*0x20` |

- **供电与识别顺序**：先把 GPIO1_C5（pin 21）配成输出且为低 → 20 ms → 解除 CRU 门控 → 施加 PHY 调优
  （`PHY1_TUNE` 清 bit2 与 bits6:4、置 `5<<4` 选 425 mV HS 眼图；`PHY1_LINESEL` 清 bits6:3、置 `3<<3`
  选 TX 驱动的 FS/LS 线路状态；`PHY_CLKOUT` 置 `0x27<<2` 打开 480 MHz 输出并等 1300 µs；
  `GRF_USBPHY_CON1` 写 `WRITE_MASK(0x1ff,0)` 让 USB2 host 端口退出挂起）→ 拉高电源 →
  轮询 `GRF_USBPHY_STATUS` 的 bit13:12（linestate），最多 100 次 × 10 ms
  （`src/luckfox_wifi.c:1154-1249`）。
- **核心校验**：`GSNPSID` 高 16 位必须等于 `0x4f54`（`DWC2_OTG_ID`），否则报
  `"WIFI: invalid DWC2 core ID\n"`；linestate 为 0 时告警
  `"WIFI: no USB device pull-up detected; check module power\n"`（README:311-312 给出这两条排障含义）。
- **DWC2 主机初始化**：等 `AHBIDLE` → 写 `CSFTRST` 软复位并等清零 → 清 `FORCEDEV`、置 `FORCEHOST`
  （`GUSBCFG` bit30/bit29）等 30 ms → 确认 `GINTSTS` bit0（host 模式）→ `GAHBCFG = INCR4|DMAEN`、
  `PCGCCTL=0` → `HCFG` 置 1（48 MHz FS/LS 时钟）→ 刷 TX/RX FIFO → 由 `GHWCFG2` bits18:14 计算通道数
  并逐个 `CHDIS`/`CHEN` 停通道、清中断 → 端口上电/复位时序（power 20 ms → power+reset 50 ms →
  清 reset 100 ms）（`src/luckfox_wifi.c:180-285`）。
- **速度编码坑**：DWC2 在 `HPRT` 中把 high/full/low 编码为 **0/1/2**，驱动打印名字而不是直接打印数字，
  以免把 0（高速）误读为失败（README:327-329；`src/luckfox_wifi.c:257-275`）。
- **EP0 控制传输**：256 字节、256 字节对齐的静态 DMA 缓冲；OUT 前 `up_clean_dcache`、IN 前
  `up_invalidate_dcache`；`HCCHAR` 组包（MPS/EP/类型/设备地址/`MULTICNT_1`），低速设备置 `LSDEV`；
  `HCTSIZ` 写入长度、包数与 PID；最多重试 100 次，`NAK` 则每 1 ms 重试，其它中断立即报错并打印
  `HCINT`/`HCTSIZ`（`src/luckfox_wifi.c:287-402`）。
- **OUT 残余计数坑**（源码注释）：部分 DWC2 版本在 OUT DMA 成功后不更新剩余字节数，因此
  `XFERCOMP` 即视为整个 OUT 包被接受，只有 IN 才用剩余字节做短包统计
  （`src/luckfox_wifi.c:368-376`）。
- **Hub 处理**：根端口设备类为 `0x09` 时进入 `luckfox_wifi_scan_hub(1, mps, config[5])`：读 hub 类描述符
  （`setup[0]=0xa0`、类型 `0x29`），端口数取 `hubdesc[2]`（必须在 1..8），通电等待
  `hubdesc[5]*2+20` ms，逐端口 `GET_STATUS`（`0xa3`）、`CLEAR_FEATURE(C_CONNECTION=16)`、
  `SET_FEATURE(PORT_RESET=4)`，最多 50 次 × 10 ms 等 reset 清零且 enable 置位，然后清
  `C_ENABLE(17)`/`C_RESET(20)` 并枚举地址 ≥2 的子设备（`src/luckfox_wifi.c:898-1039`）。
- **Split 事务缺失**：高速 hub 后面的 full/low speed 子设备需要 split 事务，代码显式返回 `-ENOTSUP`
  并打印 `"…DWC2 split is not enabled\n"`，而不是发起非法传输（README:354-357；`src/luckfox_wifi.c:745-762`）。
- **配置描述符解析**：校验 `offset == length`、`interfaces == config[4] == 3`、至少 1 个厂商接口、
  至少 1 个端点；仅统计 alt=0 的接口（源码注释：“bNumInterfaces counts unique interface numbers, not
  alternate settings”），仅接受厂商 `ff/ff/ff` 接口上 512 字节的批量端点，按出现顺序把第一对当作
  data、第二对当作 msg（`src/luckfox_wifi.c:520-643`）。README:380-384 特别指出 **interface 1 的 alt 0..5
  属于蓝牙等时传输，不能算作独立接口**。
- **厂商 Bulk 消息**：20 字节请求（外层 4 字节头 + dummy word + lmac_msg）：`request[0]=16`、
  `request[2]=0x11 (USB_TYPE_CFG_CMD_RSP)`、`request[8..9]=0x0400 (DBG_MEM_READ_REQ)`、
  `request[10]=0x01 (TASK_DBG)`、`request[12]=100 (DRV_TASK_ID)`、`request[14]=4`、
  `request[16..19]=0x40500000`（小端）；响应校验 `packet_length+4 <= actual`、`(response[2]&0x7f)==0x11`、
  `response_id==0x0401`、`parameter_length>=8`、返回地址回读必须等于 `0x40500000`，最后打印
  `"WIFI: AIC bulk message passed, [40500000]=%08x chip-id=%02x\n"`（chip-id 取 `memory_value` 的 bit23:16）
  （`src/luckfox_wifi.c:645-727`）。
- 该实现与厂商驱动的对应关系由源码注释给出：“Match aicwf_set_cmd_tx() from the vendor driver.”，
  并说明这是只读操作、是 `system_config_8800dc()` 的第一步。

---

## 已知限制与未完成项

### README 明确承认的

1. **不提供 `wlan0`，无线尚未能用**：README:360-363 原文 “This hub-scan image deliberately does not
   provide `wlan0` yet.”，下一步是“the reusable DWC2 host-controller interface and AIC8800DC firmware
   loader, followed by the 802.11 driver and openvela network stack.”。芯片 Kconfig 同样写明
   “This option does not yet provide a network interface.”（`chips/rk3506/Kconfig:58-59`）。
2. **外部 SPI MOSI–MISO 检查点延后**：README:276-277 “The external MOSI-to-MISO checkpoint is
   intentionally deferred when no jumper wire is available; it is not a prerequisite for the onboard Wi-Fi
   work.”（内部回环已通过）。
3. **SD 高速模式未协商**：README:48-52 “The generic SD-card layer does not yet negotiate SD high-speed
   mode with CMD6, so the port does not use the 26/52 MHz settings supported by U-Boot.”；13 MHz 是
   “a conservative performance step”，并给出回退到约 8.67 MHz 的方法（把 `BOARD_SDMMC_TRANSFER_TARGET`
   改成 `BOARD_SDMMC_SAFE_TARGET` 后重新构建）。
4. **`nsh_512m` 不能刷进 128 MiB 的 Lyra B**：README:29-31、`configs/nsh_512m/defconfig:4-5`。
5. **多 OS / Linux 共存不做**：README:5-6 “Linux and multi-OS AMP coexistence are intentionally out of
   scope for this configuration.”
6. **I2C 只做了无从设备的注册/扫描说明**，README 强调空扫描是合法结果、不能假设触摸屏存在、
   400 kHz 必须等 100 kHz 稳定后再用（README:213-244）。
7. **USB split 事务未实现**（全速/低速下游设备只报告不传输）：README:354-357。
8. **不得直接写启动介质**：`/dev/mmcsd0`、`/dev/uboot.raw`、`/dev/boot.raw`、`/dev/amp.raw`
   （README:205-206）；**不得把 FSPI/SPI-NAND 当通用 SPI0 用**（README:271-273）。
9. **打包流程陷阱**：`build.sh firmware` 会静默复用旧 `amp.img`（README:88-96）。

### 源码中可见、README 未提及的

| 项目 | 内容 | 位置 |
| --- | --- | --- |
| `FIXME`（总线共享） | `rk3506_lock()` 函数体只有 `return OK;`，注释保留 `/* FIXME: Implement the below function to support bus share: rk3506_muxbus_sdio_lock(lock); */` | `chips/rk3506/rk3506_sdmmc.c:1387` |
| 未声明的 Kconfig 符号 | `RK3506_SDMMC_PWRCTRL`、`RK3506_SDMMC_REGDEBUG` 只出现在注释块中，Kconfig 未定义 | `chips/rk3506/rk3506_sdmmc.c:109-118` |
| 寄存器宏重复定义 | `RK3506_SDMMC_TBBCNT` 被 `#define` 两次 | `chips/rk3506/include/rk3506_sdmmc.h:108-109` |
| 无类型常量传参 | `rk3506_putreg(0xffffffff, RK3506_SDMMC_RINTSTS)` 传入 `uint32_t` 形参 | `chips/rk3506/rk3506_sdmmc.c:1505` |
| GPIO 高半区读取 | `rk3506_gpio1_read()` 始终读 `EXT_PORT(+0x70)`，未访问 `+0x74`，pin 16–27 的读取取自低半区（README 未说明；活动 LED 是 pin 0，Wi-Fi 电源脚只写不读，故当前无功能影响） | `chips/rk3506/rk3506_gpio.c:96-105` |

### 完全未记录的信息

- **NPU**：README/源码全文未出现 NPU、TOPS、RKNN 等字样，**无法判断本芯片是否带 NPU**。
- **SoC 具体型号后缀、CPU 主频、内存类型/位宽、eMMC**：README 未记录（存储只提到 SD 卡与启动 SPI-NAND）。
- UART0 的实测启动时间、SD 卡实测吞吐：README 未给出（只有构造性检查点命令与 bit 率/时钟设置）。

---

## 证据边界

### 本文档的依据类型

| 类型 | 含义 | 例子 |
| --- | --- | --- |
| A. README 硬件声明 | README 用过去时/完成时声称在真实板卡上观察到 | SPI 内部回环 “have passed at mode 0 / 1 MHz and mode 3 / 4 MHz”；Wi-Fi “hardware checkpoint passed”；“The real board reports a high-speed connection.”；`VID:PID=1a86:8091`、`a69c:88dc`、`01/81`/`02/82` 端点映射 |
| B. README 验收判据 | README 用 `must`/`expected` 描述合格标准，未声明已观测 | NSH 提示符、LED 开关、IDMAC 检查点、I2C 扫描、SPI 外部短接、Wi-Fi Bulk 检查点 |
| C. 源码事实 | 源码中可直接读到的常量、寄存器、时序、日志字符串 | 本文“关键技术难点与解决”全部内容 |
| D. 本文档推导 | 由源码公式推算，README 未给出实测值 | I2C `CLKDIV=0x000E000E`/`0x00020004` 及 ≈100 kHz/≈375 kHz；SDMMC 65/2/3 分频与 400 kHz/13 MHz/8.67 MHz 的对应 |
| E. 完全未记录 | 原始材料中没有 | NPU/TOPS、SoC 型号后缀、实测启动时间与吞吐 |

### 本次整理**没有**做的事（因此不能作为证据的部分）

- **没有在真实 RK3506 / Lyra Zero W 板卡上运行任何检查点**：没有串口日志、`dmesg` 原始输出、
  镜像拆包校验或 U-Boot FIT 哈希比对。所有“通过”均为**转述 README 的声明**。
- **没有构建**：仓库内不存在 `cmake_out/`、`RK3506-ALL-Files/luckfox-lyra-sdk/lyra-zero-w` 等构建/SDK
  目录，本文列出的构建命令与产物路径全部来自 README 与脚本默认值，未在本机复现。
- **仓库内没有镜像哈希**：README 提到用 `mkimage -l` 校验 FIT，但本仓库未附带 `amp.img` 的 SHA-256；
  仓库中的 `boards/rk3506/luckfox-lyra-zero-w/amp.img`（235,008 字节）只证明打包脚本曾运行过，
  **不证明它与当前源码一致，也不证明它通过了硬件验收**。
- **未做硬件级电气复核**：I2C 实际 SCL 频率、SPI 实际时序、SD 时钟准确性、DWC2 信号完整性均无实测数据。
- **未对照 Rockchip 官方 TRM/数据手册**：所有寄存器含义均以本仓库源码的宏与注释为准；
  代码里未命名的部分（如各 CRU 门控位对应的具体时钟名）本文档不做命名。
