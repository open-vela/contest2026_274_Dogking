# A733 / Cubie A7Z openvela 完整 AI 交接文档

交接日期：2026-09-10（Asia/Shanghai）  
项目根目录：`D:\My-Program\AI-Agent-Program\GPTsprogram\Openvela`  
openvela 工作树：`D:\My-Program\AI-Agent-Program\GPTsprogram\Openvela\quickly-openvela`  
资料总目录：`D:\My-Program\AI-Agent-Program\GPTsprogram\Openvela\A733-A7Z-ALL-Files`

> 给接手 AI 的第一条指令：不要重新克隆、移动或重建 openvela 环境，不要删除现有构建目录，不要用 Linux 内核驱动替换现有 NuttX/openvela 实现。直接使用当前 `quickly-openvela` 工作树和 WSL Ubuntu 构建。当前目录不是一个可用的 Git 仓库，所以恢复必须依靠本交接包、`archives` 快照和 SHA-256，而不能假设可以 `git reset`。

## 1. 当前结论（接手者先读）

这是瑞莎 Radxa Cubie A7Z（Allwinner A733，4 GiB LPDDR4/4X，无 UFS，当前从 microSD 启动）的 openvela/NuttX 裸机适配。项目已经远远超过“能启动”的阶段：ARM64 内核、交互式 NSH、4 GiB 内存、SD/GPT/FAT、温度/LED/看门狗、Wi-Fi 2.4/5 GHz、WPA2、DHCP、TCP/IP、curl/wget、SSH、FTP 和 VIP2 NPU 的 LeNet/YOLOv5/YOLOv8 真机推理均已打通。

当前正在做的工作是：外接 USB UVC 摄像头 → A733 → YOLOv8 NPU → 检测结果输出。最新版本是 USB 摄像头 v65。它尚未枚举摄像头，已经定位到 Cadence Combo0 USB3 PHY/xHCI 的复位恢复问题。下一位 AI 不应回退去猜 Type-C、摄像头供电或普通 USB2 PHY；应从“保留固件 handoff，不复位 xHCI，直接建立 xHCI DMA rings”继续。

当前稳定功能基线不是 v65 的摄像头功能，而是 v55 服务功能加 v50 NPU 功能；v65 镜像在此稳定基线上增加了仍在开发的 USB 摄像头检查点。

## 2. 硬件和启动边界

- 开发板：Radxa Cubie A7Z。
- SoC：Allwinner A733，2× Cortex-A76 + 6× Cortex-A55，ARM64。
- 内存：4 GiB；openvela 已能识别约 4 GiB，并用 split-region 方式避开保留区。
- 当前存储：microSD；板上未安装 UFS。UFS 探测失败必须保持非阻塞，不能让它影响 SD 启动。
- 未来：完成 SD 适配后再做 UFS，并最终优先 UFS、回退 SD。
- 板载网络：没有以太网。板载 FCU760K（AIC8800D80 U02）Wi-Fi 6 + Bluetooth 5.4，通过专用 USB1 连接。
- 当前相机：用户要先适配外接 USB 摄像头；另有 Radxa Camera 13M 214（Sony IMX214）MIPI CSI，资料调查已记录，但驱动尚未实现。
- BootROM/BOOT0/SPL/BL31/SCP/U-Boot 使用官方 Allwinner/Radxa 启动链。openvela 以内核 Image 形式由 U-Boot `booti` 启动，入口地址由官方链路交接。
- 当前最小 SD 镜像已经不依赖 Debian rootfs；官方 Debian 镜像仍是参考和 Linux golden-trace 采集环境。

## 3. 已完成内容（按功能而非零散版本）

### 3.1 ARM64 启动、内存和控制台——真机通过

- 从官方 BOOT0/SPL/BL31/SCP/U-Boot 链进入 openvela EL1。
- AArch64 C runtime、链接脚本、镜像头和启动标记完成。
- NSH 可以启动并运行，`uname -a` 显示 `arm64 cubie-a7z`。
- 4 GiB RAM 已纳入 NuttX heap，`free` 真机显示约 4.27 GB 可管理内存。
- procfs 自动挂载，`free`、`ps`、`uptime`、`/proc/meminfo` 可用。
- UART 控制台输出、输入、阻塞/非阻塞读取、termios、控制终端和 Ctrl+C 信号路径已实现。
- 早期启动标记（如 A7Z0、N0、H0、C0 等）是历史调试标记；不能把它们误判为异常。

### 3.2 板级基础外设——真机通过

- `/dev/userled`：板载/用户 LED GPIO。
- `/dev/watchdog0`：看门狗 lower-half。
- `poweroff`、`reboot`：通过 PSCI/板级 power/reset 接口。
- `/dev/uorb/sensor_temp0`：A733 THS 温度传感器，`sensortest` 真机通过。
- `/dev/a733-hw`：综合硬件诊断节点，显示 PIO、WDT、THS、SDMMC、TWI、SPI、PWM、无线和 UFS 状态。
- `/dev/i2c2`、`/dev/i2c7`：40-pin header I2C。
- `/dev/spi1`：header SPI polling 驱动。
- `/dev/pwm0`：风扇 PWM（PWM1_CH9/PJ27）。
- UFS 缺失状态可诊断但不会阻止启动。

### 3.3 microSD、GPT 和 FAT——真机通过

- SDMMC0：PIO 模式，初始化 400 kHz，工作 12 MHz，4-bit。
- `/dev/mmcsd0` 可连续读取 512 B、16 KiB、64 KiB 块。
- GPT 分区注册：`/dev/a7z-firmware`、`/dev/a7z-efi`、`/dev/a7z-openvela`、`/dev/a7z-data`。
- `/dev/a7z-data` 作为 FAT 读写挂载到 `/data`。
- 最小 SD 镜像独立于 Debian rootfs，可由 U-Boot extlinux 自动选择 openvela。

### 3.4 FCU760K Wi-Fi——真机通过，Bluetooth 未完成

- USB1 EHCI root port、模块 PM0/PM1 电源、USB2 PHY、BootROM 枚举完成。
- BootROM VID:PID `a69c:8d80`，固件运行态 `a69c:8d81`。
- 已实现 AIC BootROM bulk 协议、D80-U02 固件上传、patch table/config、启动和重新枚举。
- 已实现 runtime endpoint map、LMAC version/start、efuse MAC、RF calibration、ME config、CN regulatory channel table、station VIF/MAC start。
- `/dev/a733-wifi` 提供底层诊断和兼容控制。
- 原生 `wifi` 命令提供更易用的扫描、2.4/5 GHz 连接、断开和状态管理；不移植 Linux `nmcli`。
- WPA2-PSK/CCMP、四次握手、DHCP、DNS、IPv4、ARP、TCP/UDP 数据路径已通过。
- 可记忆上次 Wi-Fi 并选择开机自动重连；凭据只允许在用户明确配置后保存在 `/data`，源码和镜像中没有用户密码。
- 真机性能记录：TCP 约 4.13 Mbit/s；UDP 约 10.8 Mbit/s，30 秒测试约 5.7% 丢包。它证明数据链路可用，但仍需做性能/稳定性优化。
- 早期出现“双方先互相 ping 才能 SSH”的根因是 ARP 宣告/入站路径；已增加 gratuitous ARP 和 ARP/TCP 诊断并完成修复。
- 尚未完成：Bluetooth HCI、Wi-Fi 6/VHT/HE 性能、并发和长期漫游/断线重连压力测试。

### 3.5 网络工具、SSH、FTP 和服务管理——真机通过

- DNS、NTP、ping、iperf、wget、curl、HTTPS CA bundle 可用。
- A733 CE v5 TRNG 提供 `/dev/random`、`/dev/urandom`，用于 TLS/SSH 安全随机数；启动与连续性测试存在。
- SSH server/client/scp 已适配 libssh；Ed25519 主机密钥、密码认证、PTY 和远程 NSH 均通过。
- SSH 解决过的关键问题：libssh 静态初始化时机、mbedTLS 线程后端、`sh -l` 与 NSH 不兼容、Ctrl+C 未释放监听 socket、ARP 入站不可达、主机密钥变化。
- FTP server 根目录可设为 `/data/models`，支持小文件和大文件、上传/下载、UTF-8/中文文件名、路径安全检查、多个连接和失败恢复。
- FAT/32-bit 文件接口下，单文件按 `< 2 GiB` 作为可靠上限；还受 `/data` 可用空间限制。
- `service` 管理器为 Wi-Fi 自动连接、SSH、FTP 提供可选开机自启动及状态控制。
- 密码、Wi-Fi 凭据、AI API key 不应写入源码、构建脚本或镜像模板。

### 3.6 VIP2 NPU——LeNet、YOLOv5、YOLOv8 真机通过

- VIP2 电源、时钟、复位、IRQ 和硬件身份已通过；CID `0x1000003b`。
- `/dev/npu0` runtime ABI v3 已实现 context、buffer、MMU page table、cache、submit、sync、cancel/reset、guard 检查。
- Linux VIPLite 2.0.3.2-AW-2024-08-30 被用作 golden 参考，openvela 运行时不依赖 Linux 或 `libVIPhal.so`。
- LeNet：openvela 真硬件推理输出 CRC `d50f5d79`，输出字节 `003c000000000000000000000000000000000000`，与 Linux golden 完全一致。
- A7PM：自定义 prepared-model 容器，含已捕获的 VIP 地址布局、region、command 和 golden 校验。解析器做尺寸、CRC、窗口、重叠、命令和输出保护。
- YOLOv5s：第二模型、较大资源映射、command tail 修复并通过 golden 验证。
- YOLOv8n PCQ：6 个输出、动态输入、DFL decode/NMS 已打通；dog 输入能得到真实检测，black 输入无检测，证明不是固定输出回放。
- 重要限制：当前是“Linux 官方 VIPLite 预编译/trace → A7PM → openvela 执行”的路线，不是通用 NBG 编译器/链接器。新模型仍需在官方 Linux/VIPLite 环境准备并采集。
- 当前暂缓 NPU 是为了先打通摄像头；NPU v50 verified archive 是 YOLOv8 回归基线。

## 4. 当前最新工作：USB 摄像头 v65（尚未完成）

### 4.1 最新镜像和构建输出

- 镜像：`A733-A7Z-ALL-Files\openvela-a733-cubie-a7z-sd-usb-camera-v65-candidate.img`
- 镜像大小：2,147,483,648 bytes。
- 镜像 SHA-256：`f2f126c414b24e2e77f6fd8c239b5c7e2a0b2bc03847f86051268ca175266683`
- 内核：`quickly-openvela\cmake_out\cubie-a7z_nsh_v65_xhci_handoff_clean\nuttx.bin`
- 内核 SHA-256：`583fad8f85de3a0655c16b40a464d1e36866245e292dde39fb0b699c734908fb`
- 构建目录：`quickly-openvela\cmake_out\cubie-a7z_nsh_v65_xhci_handoff_clean`
- 构建脚本：`A733-A7Z-ALL-Files\build-a733-usb-camera-v65.sh`
- 打包脚本：`A733-A7Z-ALL-Files\package-a733-usb-camera-v65.sh`

### 4.2 最新实板证据

最新日志来自：
`C:\Users\Lenovo\.codex\attachments\416af5a3-cfd6-46f9-bd79-72044f9c8fee\pasted-text.txt`

关键诊断：

```text
xhci=06a00000 version=0120 caplen=48 maxports=2 hcs=02000140
cmd=00000002 sts=00000000 ports=0/0 reset=-110 cnr-polls=0 checkpoint=-110
dwc3 id/gctl/u2cfg/u3pipe/app/ext 全部在 HCRST 后变成 0
typec ET7304 id=6dcf:1711 role=05 cc=02 power=0c orientation=CC1 checkpoint=0
USB2 PHY line=1
```

v62 在任何复位前曾经看到有效 handoff：xHCI `sts=1`，两个 PORTSC 约 `0x2a0`，DWC3 寄存器有效。  
v64 加 DWC3 soft reset 后，xHCI `sts=0x801`（Halted + Controller Not Ready），CNR 1000 次不清。  
v65 移除 DWC3 soft reset，只执行标准 xHCI HCRST，但 `USBCMD.HCRST` 位本身不清，`reset=-110`，甚至未进入 CNR 等待；同时 DWC3/应用寄存器读成 0。

### 4.3 已排除的错误方向

- 不是摄像头未插：ET7304 明确检测到 Type-C sink，CC1 方向有效。
- 不是普通 USB2 PHY 完全掉电：PHY line state 有效。
- 不是 xHCI 基址错误：caplength/version/maxports/HCSParams 能正确读取。
- 不是 NuttX 主系统挂死：NSH、Wi-Fi、存储和 NPU 基线仍可运行。
- 不要继续盲目调整 Type-C role、延时或反复 DWC3/xHCI reset；现有证据已指向 Combo PHY/PIPE reset recovery 缺失。

### 4.4 根因判断

A733 的外接 USB-C 3.x 路径依赖 Cadence Combo0 USB3 PHY/SerDes/PIPE。官方 Linux 会执行完整的 `sunxi-cadence-combophy.c` 初始化。当前 openvela 只覆盖了 Type-C 控制器、USB2 PHY 和有限应用寄存器；官方固件交接状态可读，但一旦复位 xHCI/DWC3，缺少完整 Combo PHY 恢复序列，导致 HCRST/CNR 无法完成。

### 4.5 下一步首选实现（v66）

先走 handoff-preserving 路线：

1. 保留 BL31/U-Boot 留下的 DWC3、Cadence Combo PHY 和 xHCI 状态。
2. 不执行 DWC3 soft reset。
3. 不执行 xHCI HCRST。
4. 确认控制器 halted 且 CNR=0；若 handoff 状态不满足，只报告，不破坏它。
5. 分配 DMA32、64-byte 对齐的 DCBAA；根据 HCSParams2 分配 scratchpad array/pages（如数量为 0 则跳过）。
6. 建立 command ring，首个 link TRB 指回 ring，正确设置 cycle bit。
7. 建立 event ring、ERST、ERDP，配置 interrupter 0（IMAN/IMOD/ERSTSZ/ERSTBA/ERDP）。
8. 设置 `DCBAAP`、`CRCR`，更新 `CONFIG.MaxSlotsEn`，清遗留 status/event。
9. 最后设置 `USBCMD.RUN` 和 INTE；等待 PORTSC CCS/port change 或直接读取已有连接状态。
10. 执行 Enable Slot → Address Device；建立 input context/device context/slot context/EP0 context。
11. 通过 EP0 control transfer 获取 device/config/string descriptors。
12. 找 UVC VideoControl/VideoStreaming interface、formats、frames、iso/bulk endpoints；实现 PROBE/COMMIT。
13. 建立 transfer ring，采集第一帧到 `/dev/video0` 或新的 `/dev/uvc0`。

必须参考 NuttX 通用 xHCI 数据结构/规范，但不要假设现有 NuttX USB host 栈可以直接绑定这个没有标准 lower-half 的自定义路径。可以先做最小枚举检查点，再逐步接入通用 USB/UVC。

### 4.6 备选实现（清洁复位，之后做）

移植官方 Linux/Allwinner BSP 的完整 Cadence Combo0 USB3 PHY：`sunxi-cadence-combophy.c` 及其依赖的 CCU/reset/PMU/SerDes 配置。完成后才能安全支持 DWC3/xHCI clean reset、USB3 SuperSpeed、热插拔和错误恢复。这条路线工作量更大，但生产化最终需要。

## 5. 从零恢复和 WSL 构建

### 5.1 环境规则

- 在 Windows 保留当前路径；不要重新复制整个 openvela 到 WSL 虚拟磁盘。
- 通过 WSL Ubuntu 访问 `/mnt/d/My-Program/AI-Agent-Program/GPTsprogram/Openvela`。
- 使用仓库内预编译的 build-tools、AArch64 GCC、CMake 和 Ninja；构建脚本已设置 PATH。
- 不要删除 `quickly-openvela/cmake_out`；历史构建目录用于定位回归。
- 当前树没有可用 Git 元数据；改动前先复制相关文件到新的 `archives/a733-...` 版本目录。
- 所有源代码编辑在 Windows 当前工作树完成或用 `apply_patch`；WSL 只负责构建、镜像文件系统操作和 Linux 工具。

### 5.2 构建 v65

在 PowerShell：

```powershell
wsl -d Ubuntu -- bash -lc "cd /mnt/d/My-Program/AI-Agent-Program/GPTsprogram/Openvela/A733-A7Z-ALL-Files && ./build-a733-usb-camera-v65.sh --incremental"
```

全量重配构建时去掉 `--incremental`。新版本必须新建构建目录（例如 v66），避免覆盖 v65 证据。

### 5.3 镜像打包规则

- 包装脚本从前一版 2 GiB 镜像复制，只替换 openvela 分区中的 `/boot/openvela/a733/Image`。
- 脚本用固定 offset/size loop mount，完成后做 `cmp`、`e2fsck -fn`、`sgdisk -v` 和 SHA-256。
- 新镜像名称必须带版本和 `candidate`；真机通过后才创建 `verified` archive。
- 不要覆盖 v65 镜像；新版本从 v65 复制。
- 打包需要 WSL root/loop/mount 权限。先确认目标文件不存在，脚本也会拒绝覆盖。

## 6. 当前源文件逐项说明

### 6.1 芯片目录 `quickly-openvela/vendor/allwinnertech/chips/a733`

- `a733_boot.c`：ARM64 chip boot、early marker、额外 RAM region 和网络初始化入口。
- `a733_image_header.S`：U-Boot/ARM64 可加载 Image 头。
- `a733_lowputc.S`：最早期 UART 输出。
- `a733_serial.c`：A7Z polling UART console、RX worker/ring、termios、控制终端和 Ctrl+C。
- `a733_hwdiag.c`：`/dev/a733-hw` 诊断节点。
- `a733_watchdog.c`：NuttX watchdog lower-half。
- `a733_tsadc.c`：THS 温度读取和 uORB sensor lower-half。
- `a733_trng.c`：CE v5 硬件 TRNG、自检、random device 支持。
- `a733_sdmmc.c`：SDMMC0 PIO/SDIO lower-half，卡枚举和块读写。
- `a733_header_peripherals.c`：I2C2/I2C7、SPI1、PWM fan 等 40-pin 外设。
- `a733_wifi_usb.c`：FCU760K 的 USB1 EHCI、BootROM/固件、LMAC/RF/scan/associate/WPA2/DHCP、wlan0 RX/TX、ARP 诊断；大文件但不可遗漏。
- `a733_npu.c`：VIP2 power/clock/reset/IRQ/MMU command execution 和硬件自检。
- `a733_npu_memory.c`：VIP2 DMA pool、物理/VIP 映射、cache 和 guard。
- `a733_npu_runtime.c`：`/dev/npu0` ABI、A7PM、LeNet、YOLO decode/DFL/NMS、dynamic input。
- `a733_usb_camera.c`：当前 v65 USB camera/Type-C/USB2/xHCI bring-up；下一步主要修改文件。
- `include/chip.h`：A733 MMIO/芯片定义。
- `include/irq.h`：A733 IRQ 定义。
- `include/a733_peripherals.h`：板级外设公开初始化接口和寄存器定义。
- `include/a733_npu_internal.h`：NPU internal ABI/硬件接口。
- `include/a733_npu_memory.h`：NPU allocator/mapping 接口。
- `CMakeLists.txt`、`Kconfig`、`Make.defs`：A733 驱动构建和配置入口。

### 6.2 板目录 `quickly-openvela/vendor/allwinnertech/boards/a733/cubie-a7z`

- `configs/nsh/defconfig`：当前功能全集的唯一权威配置；USB camera/NPU/Wi-Fi/SSH/FTP/curl 等均依赖它。
- `src/a7z_boardinit.c`：板级初始化顺序、procfs、硬件、NPU、存储、Wi-Fi/USB camera 和服务启动。
- `src/a7z_gpio.c`：用户 LED GPIO。
- `src/a7z_power.c`：poweroff/reboot。
- `src/a7z_storage.c`：GPT partition 注册和 `/data` FAT mount。
- `include/board.h`：板级引脚/接口定义。
- `scripts/dramboot.ld`：A733 DRAM 链接布局。
- `tools/extlinux-openvela.conf`：U-Boot extlinux 条目模板。
- 其余 CMake/Kconfig/Makefile：板级集成。

### 6.3 自定义应用

- `quickly-openvela/apps/system/a733wifi/`：用户可用 `wifi` 命令，扫描、选择 2.4/5 GHz、交互输入密码、连接、自动重连和状态。
- `quickly-openvela/apps/system/a733services/`：`service` 管理与 `/data` 持久化配置，控制 Wi-Fi/SSH/FTP 自启动。
- `quickly-openvela/apps/system/a733ftpd/`：FTP 服务包装器、用户/根目录/状态控制。

### 6.4 跨目录必要改动

- `apps/external/curl/curl_config.h`：NuttX curl 配置和同步 DNS。
- `apps/external/libssh/libssh/src/init.c`：libssh 显式初始化时序。
- `apps/external/libssh/libssh/examples/ssh_server.c`：A733 sshd、PTY/NSH、多连接、清理和 host key。
- `apps/external/libssh/libssh/examples/ssh_client.c`：SSH client 适配。
- `apps/external/libssh/libssh/examples/libssh_scp.c`：SCP 适配。
- `apps/netutils/ftpd/ftpd.c`、`ftpd.h`、`Kconfig` 和 `apps/include/netutils/ftpd.h`：FTP 大小文件、路径/UTF-8、错误恢复和多客户端。
- `apps/netutils/ntpclient/ntpclient.c`、`Kconfig`：联网后校时及证书时间依赖。
- `apps/netutils/iperf/iperf.c`：网络吞吐测试相关适配。
- `apps/nshlib/nsh_netcmds.c`：网络命令集成。
- `apps/include/system/a733_services.h`：服务管理公开接口。
- `nuttx/sched/task/task_start.c`：历史 Ctrl+C/任务启动相关快照；修改新控制台前先核对。

## 7. 版本/阶段简史（只保留接手所需结论）

- 早期阶段：收集 A733/Radxa BSP、官方 Debian、原理图/placement；确认 4 GiB、SD 优先、无 UFS。
- Stage 1：从官方启动链进入 ARM64 openvela，逐个修复 EL/栈/BSS/异常/GIC/定时器/控制台，最终进入 NSH。
- 基础合并阶段：procfs、4 GiB heap、LED、WDT、THS、SDMMC、GPT/FAT、I2C/SPI/PWM。
- FCU760K v33.1：BootROM、固件、LMAC、RF、2.4/5 GHz scan、WPA2、DHCP 和 IP 数据链真机通过。
- NPU v43：LeNet 真硬件 golden 通过。
- NPU v44：通用 A7PM 第一模型通过。
- NPU v46/v47：YOLOv5 golden/command tail 通过。
- NPU v48-v50：YOLOv8 六输出、动态输入、dog/black 语义验证通过；v50 是 NPU 稳定基线。
- 网络/SSH v35-v45：wifi manager、curl/TLS、Ctrl+C、TRNG、NTP、ARP/LAN、SSH socket 和远程 shell 逐步修复。
- v51-v55：power/reboot、SSH/FTP 自启动、服务管理、FTP 大小文件和 Unicode 路径；v55 为服务功能候选基线，用户后续实测核心功能成功。
- USB camera v56-v65：从外部端口/Type-C/USB2 PHY/EHCI/xHCI 枚举检查逐步定位到 Combo PHY/xHCI reset recovery；v65 仍未枚举 UVC。

完整逐次日志在 `A733_OPENVELA_STATUS.md` 和各 `A733_*_Vxx.md`，不要仅凭上面简史删除旧机制。

## 8. 待完成任务清单（优先级和验收门槛）

### P0：完成 USB UVC → YOLOv8 全链路

1. v66 handoff-preserving xHCI ring 初始化（见 4.5）。
2. 枚举摄像头，打印 VID/PID、device/config/interface/endpoint descriptors。
3. 实现 UVC PROBE/COMMIT 和至少一种摄像头真实支持的格式。
4. 采集稳定帧；优先 YUY2/NV12/RGB，若只有 MJPEG 则需要 JPEG decoder。
5. 颜色转换、resize/letterbox 到 640×640 RGB，写入 YOLOv8 dynamic input buffer。
6. 调用现有 `/dev/npu0` YOLOv8 prepared model，执行 DFL/NMS。
7. 输出 bbox/class/score；保存可选原始帧和标注结果到 `/data`。
8. 连续运行、Ctrl+C、USB 超时、拔插、NPU timeout、内存 guard 均能恢复。

验收：同一物体连续 100 帧有稳定检测；无帧泄漏；断开摄像头不挂系统；重新连接可恢复；black frame 无假检测；dog/person 实图产生合理框。

### P1：USB 主机生产化

- 移植完整 Cadence Combo0 PHY clean-reset 路径。
- 对接通用 xHCI/USB host/UVC 框架，替代长期维护私有最小栈。
- 支持 USB3、热插拔、多个 transfer ring、isochronous 和错误恢复。

### P1：Radxa Camera 13M 214（IMX214）MIPI CSI

- 先读 `CAMERA_IMX214_RESEARCH.md`、原理图和官方 Linux DTS/driver。
- 查清 sensor I2C bus/address、reset/pwdn GPIO、XVCLK、regulators、CSI lanes、link frequency、mode table。
- 移植 IMX214 sensor init、A733 CSI2 DPHY、VIN/ISP、DMA buffer；再复用 YOLOv8 pipeline。

### P1：NPU 通用化和目标模型

- 建立可重复的官方 Linux model compile/trace/A7PM 流水线，减少手工地址依赖。
- YOLOv8：真实数据集量化校准、COCO/自定义类别、准确率回归、摄像头前后处理优化。
- 人脸：检测模型 + embedding + 本地数据库，而不是把“人脸识别”混同为 YOLO 类别检测。
- 离线语音助手：音频输入 → VAD → ASR → 约 1.5B 量化 LLM → TTS；逐模型调查 VIP2 支持算子，不能假设整个 Transformer 可放 NPU。CPU fallback、内存峰值和 KV cache 是主要风险。
- 1.5B LLM 在 4 GiB 上只能采用强量化、小 context 和分块/CPU-NPU 混合；先做内存预算和单模型 benchmark。

### P2：Bluetooth 5.4

- FCU760K runtime USB 已暴露 BT event/ACL endpoints，但尚无 HCI device。
- 实现 USB HCI transport、command/event/ACL，注册 NuttX Bluetooth HCI，再做 scan/pair/audio/profile。

### P2：GPU

- Imagination BXM-4-64 的固件、MMU、内核接口和用户态闭源依赖需单独调查。
- 先完成最小 power/clock/identity/MMU/job checkpoint，再讨论 OpenGL/Vulkan；不能把 Linux 用户态 blob 直接当作 openvela 可用库。

### P2：UFS 和启动产品化

- 当前无 UFS 板无法真机验证。将来装 UFS 后移植 UFS host、块设备、GPT/filesystem。
- 最终启动优先 UFS、失败回退 SD；保留恢复镜像。

### P2：系统质量

- SMP/8 核、DVFS、低功耗、热策略。
- Wi-Fi 长稳、断线自动重连、并发吞吐和 5 GHz DFS。
- SSH/FTP 权限模型、密钥认证、无明文密码默认值、服务资源上限。
- 自动化硬件回归、版本化配置、真正恢复 Git 仓库并提交清晰 patch series。

## 9. 交接包与需要另外复制的内容

交接包会包含：

- 本文和当前总状态文档；
- 当前 A733 chip/board/custom apps 源码；
- 跨目录网络/SSH/FTP/curl/NTP/NSH 必要源码；
- v35-v65 的文档、构建/打包/安装/验证脚本和 SHA 文件；
- v65 的 `nuttx.bin`、ELF、System.map、`.config`、defconfig；
- `prepared-models`（约 30 MB）；
- 自动生成的逐文件 SHA-256 manifest。

以下体积大或属于官方参考，应从原位置单独复制给接手 AI，不能只给压缩包：

1. `quickly-openvela\`：完整现有 openvela 工作环境（必须，保持原位最好）。
2. `A733-A7Z-ALL-Files\openvela-a733-cubie-a7z-sd-usb-camera-v65-candidate.img`：最新可烧写镜像，2 GiB。
3. `A733-A7Z-ALL-Files\radxa-a733_bullseye_cli_r6.output_512.img`：官方 Debian golden/trace 环境，约 3.8 GB。
4. `A733-A7Z-ALL-Files\allwinner-device-device-a733-v1.4.6\`：官方 device/BSP 文件。
5. `A733-A7Z-ALL-Files\linux-5.15-product-aiot-stable\`：官方 Linux 驱动参考，尤其 USB Combo PHY、CSI/ISP、NPU、UFS。
6. `A733-A7Z-ALL-Files\bsp-product-aiot-stable\`：BSP 集成、配置和固件参考。
7. `A733-A7Z-ALL-Files\u-boot-2018-product-aiot-stable\`：当前启动和 USB handoff 参考。
8. `A733-A7Z-ALL-Files\spl-pub-product-aiot-stable\`、`a733-product-aiot-stable\`：SPL/平台代码。
9. `A733-A7Z-ALL-Files\docs-product-aiot-stable-Software 软件类文档\` 和 `A733_User Manual_V0.92.pdf`：寄存器/SDK 文档。
10. `A733-A7Z-ALL-Files\radxa_cubie_a7z_schematic_v1.11*`、`radxa_cubie_a7z_components_placement_map_v1.11*`：板级连线与器件位置。
11. `A733-A7Z-ALL-Files\fcu760k-official-reference\`：Wi-Fi/BT 官方参考。
12. `A733-A7Z-ALL-Files\allwinner-model-zoo.tar.gz`、`model-zoo-extracted\`：NPU 模型。
13. `A733-A7Z-ALL-Files\docker_images_v2.0.x.zip`、`docker-images-extracted\`：官方 NPU 工具链容器。
14. `A733-A7Z-ALL-Files\viptrace-lenet-20260413-072201.tar.gz`、`viptrace-yolov5s-20260829-030008.tar.gz`、`viptrace-yolov8n-pcq-20260830-153436.tar.gz`：Linux golden traces。
15. `A733-A7Z-ALL-Files\yolov8-openvela\`、`a733-viplite-golden\`：NPU 复现和转换工作区。
16. `A733-A7Z-ALL-Files\archives\`：各次 verified/candidate 恢复点；磁盘允许时全部复制。
17. 用户真实硬件日志附件，尤其本文件 4.2 指定的 v65 日志；Codex attachment 目录不会随工作区自动复制。

## 10. 接手后的安全工作方式

- 先验证 v65 文件 SHA，不要第一步就重编译或覆盖镜像。
- 读取 `A733_OPENVELA_STATUS.md`、`A733_USB_CAMERA_V56.md` 到 `V65.md` 和 `a733_usb_camera.c`。
- 创建 v66 文档、构建目录、脚本和新镜像名；绝不覆盖 v65。
- 每次只引入一个可证伪的硬件假设，诊断节点记录完整寄存器、超时阶段和 route restore。
- 所有轮询必须有超时；错误时恢复控制器/路由或至少保持 NSH 可用。
- DMA buffer 要满足 32-bit physical、对齐、cache clean/invalidate 和 guard；A733 xHCI context size 从 HCCPARAMS1 CSZ 位读取。
- 真机日志是唯一升级为 verified 的依据。能构建、能启动、寄存器可读都不等于功能完成。
- 不保存 Wi-Fi 密码、SSH/FTP 密码、API key。用户日志中曾出现过明文 Wi-Fi 密码，交接文档和包不要复制那段明文。
- 遇到官方闭源 blob/API，不要伪造兼容层；明确分开“硬件检查点”“协议通过”“完整产品功能”。

## 11. 第一轮验收命令

烧录 v65 后可先确认基线：

```text
uname -a
free
mount
ls /dev
cat /dev/a733-hw
cat /dev/a733-wifi
cat /dev/npu0
cat /dev/a733-uvc
dmesg
```

USB camera 开发测试：

```text
cat /dev/a733-uvc
echo probe > /dev/a733-uvc
cat /dev/a733-uvc
dmesg
```

NPU 回归（确保 `/data/npu` 模型存在）：

```text
echo lenet > /dev/npu0
echo selftest > /dev/npu0
echo apitest > /dev/npu0
cat /dev/npu0
```

网络和服务回归请优先使用 `wifi`、`service`、`ifconfig`、`ping`、`curl`、`wget`、SSH/FTP 自动测试脚本，而不是直接反复写 `/dev/a733-wifi`。

## 12. 不确定性声明

- v65 只证明 Type-C/USB2 PHY/xHCI MMIO 和故障边界，未证明 UVC 枚举或图像采集。
- 服务 v55 后用户报告 SSH/FTP/Wi-Fi 功能成功，但没有统一的最终 `verified-v55` 目录；交接包保存当前源码，而 `a733-services-v55-candidate` 是最近的服务源码快照。
- NPU v50 是有真机语义证据的稳定 NPU 恢复点；当前源码后来又加入服务和 USB camera，不应把“最新源码”与“所有子系统都重新完整回归”混为一谈。
- 当前工作树不是有效 Git 仓库，无法自动证明每一处相对上游的差异；交接包因此包含所有已知跨目录改动。

接手者的最短正确路径：校验并保存 v65 → 阅读 USB v56-v65 → 在 `a733_usb_camera.c` 实现无复位 xHCI rings → 真机枚举 UVC → 采帧 → 复用 v50 YOLOv8 dynamic-input 路径 → 做连续帧和错误恢复。
