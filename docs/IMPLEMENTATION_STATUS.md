# A733 / Cubie A7Z 适配状态

状态日期：2026-09-20。“已通过”表示已在真实的 4 GiB Cubie A7Z 上观察到，
而不只是完成编译。

## 已验证的系统基线

- Boot0/SCP/BL31/U-Boot 通过 `booti` 将控制权以 EL1 交给 openvela。
- AArch64 运行时、MMU、异常、GICv3 和虚拟定时器正常。
- UART0 提供早期输出，并在 115200 8N1 下提供可交互的 NSH 控制台。
- 4 GiB DRAM 通过分区堆区域暴露，同时保留固件预留区域。
- procfs、`free`、`ps`、`uptime`、`dmesg`、信号和 Ctrl+C 正常。
- `poweroff` 和 `reboot` 使用开发板/PSCI 路径。
- 8 核 SMP 已识别 6× Cortex-A55 与 2× Cortex-A76；NSH、Wi-Fi、NPU 与本地
  LLM 回归通过。启动期多核串口字符交错属于当前诊断输出限制。

## 已验证的存储与外设

- SDMMC0：PIO，400 kHz 初始化，12 MHz 传输，4-bit 总线。
- GPT 分区：firmware、EFI、openvela 和 data。
- FAT 数据分区以读写方式挂载到 `/data`，支持 UTF-8 长文件名。
- 用户 LED、watchdog、THS 温度传感器、I2C2/I2C7、SPI1 和风扇 PWM。
- 测试板没有 UFS，并且 UFS 缺失被设计为非阻塞状态。

## 已验证的 FCU760K Wi-Fi 与服务

- 模组：FCU760K / AIC8800D80-U02，通过专用 USB1 高速链路连接。
- BootROM 标识 `a69c:8d80`，运行时标识 `a69c:8d81`。
- 固件上传、补丁配置、重新枚举、LMAC/RF/ME/法规域设置和 station VIF。
- 2.4 GHz 与 5 GHz 主动扫描、WPA2-PSK/CCMP、DHCP、ARP、DNS、TCP/UDP。
- 原生 `wifi` 命令、记忆网络重连和可选的开机自启动。
- SSH/SCP、FTP、curl/wget/HTTPS、NTP 和 iperf。
- 板载 TRNG 为 `/dev/random` 和 `/dev/urandom` 提供 TLS/SSH 随机数。
- 观测到的吞吐基线：TCP 4.13 Mbit/s；UDP 10.8 Mbit/s，单次 30 秒测试丢包
  5.7%。这只是功能基线，不是峰值性能声明。

## openvela 公共 Wi-Fi 控制面

- `wlan0` 已实现 WEXT 扫描、扫描结果、模式、认证、密码、频率、BSSID、ESSID、
  国家码和基本状态 IOCTL；
- 已启用并成功编译 openvela `apps/wireless/wapi` 库及 `wapi` 命令；
- `wifi scan/list/connect/disconnect` 已从私有字符设备写命令迁移到 WAPI；
- 密码仍由 `wifi connect` 隐藏式读取，WEXT 查询不会回显密码；
- 扫描缓存从 16 扩展到 64，避免密集 2.4 GHz 环境挤掉 5 GHz 结果；
- 私有 `/dev/a733-wifi` 当前仅作为详细诊断和旧接口兼容，真机验证后再默认关闭
  写控制接口。

v69 真机回归确认 WAPI 在 2.4/5 GHz 均能扫描并完成关联，但在 ZBCK-E 多 BSSID
AP 上，AP 持续重传 EAPOL Message 3，最终以 reason 15 断开。日志同时证明 M3
MIC、PTK/GTK 派生和 USB TX 提交均成功，因此问题定位为 Message 4 发送时序：
旧实现先向 FCU760K 安装 PTK，再发送 M4，使 M4 被数据路径提前加密；严格 AP
不会接受该帧。

v70 候选实现已经按标准 supplicant 顺序修正为：校验/解密 M3 → 发送 M4 → 安装
PTK → 安装 GTK → 开放 controlled port → DHCP；并在异步 disconnect indication
到达时同步清除 carrier、密钥安装标志和 WPA 状态，避免 reason 15 后仍显示已连接。
该版本已完成 AArch64 编译、链接和 SD 镜像文件系统校验，等待真机验证后再列入
“已验证”基线。

详细边界与验收命令见 `docs/OPENVELA_COMPONENT_BOUNDARIES.md`。

## 已验证的官方 Agent 与文字桌宠

- 使用 openvela 官方 `packages_ai_agent` 的消息总线、上下文、LLM proxy、TLS、
  Skills 和工具注册机制；产品层 `aipet` 负责桌宠路由和表现接口。
- `aipet init` 支持 DeepSeek/MiMo、默认或自定义模型、隐藏输入 API Key，并使用
  板级密钥加密持久化；配置成功后 Agent 开机自启动。
- 实机连续中文对话通过：首次 TLS 请求约 7.5 秒，连接复用后约 1.8 秒。
- 快速规则、云端优先、失败转本地接口、UTF-8 校验和回复动作/表情白名单已实现。
- 本地 LLM 已通过线程安全库接口注册到产品路由，支持强制本地请求，以及断网或
  云端失败后的真实 Qwen 回退；屏幕、舵机、电机和 LED 仍只保留安全接口，
  不宣称硬件动作已经执行。

## 已验证的本地 Qwen2.5-1.5B 基线

- 1.1 GB Q4_K_M GGUF 的元数据、tokenizer、embedding、Q/K/V、RoPE、GQA、
  28 层 Transformer、LM head、KV cache 和连续生成均已在板端逐阶段验证。
- 模型可常驻 4 GiB 内存；6 核工作池默认使用 CPU2..7（4×A55 + 2×A76），
  保留 CPU0..1 给系统。缓存命中后单 token 前向检查点约 0.6 秒。
- 中文流式输出和有限多轮历史已通过现场测试；桌宠 `LocalRouteBackend` 已接入。
  独立参考精度、长稳、跨请求持久 KV、采样策略和强制中断清理仍待完成。

## ST7735 与 LVGL 显示候选

- 复用 openvela/NuttX 官方 `drivers/lcd/st7735.c` 和 LVGL NuttX LCD backend；
  A733 板级代码只负责 SPI1、PL5 D/C、PB0 RESET 与 `/dev/lcd0` 注册。
- `display status/test/run` 已编译进入镜像，RGB565、SPI Mode 0、12 MHz，目标
  面板为 128×160 ST7735。
- **已实机确认**（2026-09-20）：
  - `/dev/lcd0` 注册成功，`display status` 报 `lcd0=ready`；
  - 电源域、引脚复用与 D/C 通路全部打通——`rpio-pl` 读到
    `mux1` bit20..23 = 1（gpio_out）、`data` bit5 = 1（PL5 高），
    D/C 电压由 2.13 V 变为 3.3 V；
  - 主 PIO `cfg1=ff6666ff`，PD10..13 已复用为 SPI1 function 6；
  - **面板初始化序列确证执行**：`st7735_lcdinitialize` 耗时由约 30 ms 变为
    **537 ms**（含 380 ms 稳定延时 + FRMCTR/PWCTR/VMCTR/gamma/COLMOD）；
    反汇编 `st7735_lcdinitialize` 可逐条核对到 12 条面板命令的立即数
    （`0xffffffb1..b4`、`0xffffffc0..c5`、`0xe0`/`0xe1`）、参数
    （`0x2c`/`0x2d` ×4、`0xa2`、`0x84`、`0x8a`、`0x2a`）与 `0x17c`(380 ms)。
- **仍未通过**：画面为**竖条纹**、颜色不正确（应为白底 + 顶部红绿蓝三色带 +
  深灰眼睛与嘴）。已新增 `display fill <RRGGBB>...` 纯色实测命令（绕过 LVGL
  直写 `/dev/lcd0`），每种颜色连续显示两种 SPI 字节序，用于最终区分
  "字节序问题"与"颜色格式问题"；并提供 `wordmsb`/`wordlsb` 运行时切换。
- 期间定位并修复的四个层次问题（详见 `docs/ST7735_LVGL_PORT.md` v121–v123）：
  1. 主 PIO pinctrl 误用旧版布局（应为 hw_type 4）；
  2. R_PIO bank L 基址把 bank 序号乘进偏移，且 PL5 的 mux 在 `+0x04` 而非
     `+0x00`，导致 PL5 一直是输入态；
  3. 面板初始化补丁的 10 个 hunk 被 `patch` 全部忽略，而退出码非致命，
     构建"成功"但内核里没有初始化序列——已改为按文本判定补丁状态并加入
     `require_contains` 构建断言；
  4. `st7735_bpp()` 仅在 bpp 变化时才发 COLMOD，颜色深度可能被继承——
     已在初始化序列中无条件发送 `COLMOD=(BPP>>2)|1`。

## 音频与语音状态

- UART4 `/dev/ttyS4`、PJ24/PJ25、9600 8N1 和 TW-TTS UTF-8 帧已完成。
  v115 实机暴露的 termios `B9600`/数值波特率混用导致 `-22`，已在 v116
  修复。v116 实机已进入参数帧发送阶段，但 UART FIFO 状态始终为零并在首字节
  超时（`stage=parameters/-110`）。v117 按官方 sun60iw2 CCU 定义显式选择
  24 MHz APB-UART 父时钟，并以“拉复位、开门控、释放复位”顺序初始化，新增
  `/dev/a733-uart4` 寄存器诊断。v117 实机诊断发现 CCU、PIO、UART 全部为零，
  由此确认旧实现仍使用了错误的 CCU 基址和传统 pinctrl 布局。v118 已改为
  官方 DTS 的 CCU `0x02002000`，并采用 sun60iw2 HW type 4 的 `0x80` 首 bank、
  `0x80` bank 步长、`0x20/0x30` 驱动/上下拉偏移。v118 实机确认
  `bgr=00010001`、PJ24/PJ25 function 4、`LSR=0x60`、`USR=0x06`；TW-TTS
  初始化三帧和文本帧全部发送成功，`frames=4 failures=0 stage=complete`，随后
  重复播报继续成功。UART4 TTS 基础链路已通过。
- I2S0 的 MAX98357A/INMP441 引脚与时钟资源已确认，`/dev/a733-audio` 只做
  非破坏诊断；尚未注册 openvela PCM lower-half，也没有播放/采集数据流。
- 官方云端 ASR 接口和本地 ASR 骨架已经调研/保留，但没有真实麦克风 PCM 输入，
  因此当前桌宠语音输入链路未完成。

## 已验证的 VIP2 NPU

- 电源/时钟/复位、IRQ、DMA 内存池、MMU 页表、缓存维护和同步任务 ABI。
- 硬件 CID `0x1000003b`；运行时设备 `/dev/npu0`。
- LeNet 输出 CRC32 `d50f5d79`，字节前缀
  `003c000000000000000000000000000000000000`，与 Linux VIPLite 黄金结果一致。
- YOLOv5s 三输出黄金回归通过。
- YOLOv8n PCQ 六输出黄金回归和动态 640×640 RGB 输入通过。dog 输入有检测结果，
  black 输入无检测结果，证明结果来自给定输入而不是回放固定输出。
- 当前交换路径为：获得授权的 Linux VIPLite prepare/trace → 已核对的 A7PM 包 →
  openvela 执行器。openvela 尚未实现通用专有 NBG 链接。

### 已编译、等待开发板回归的 NPU ABI v4

- 新增 `A733_NPUIOC_RUN_A7PM`，把 A7PM 包、可选动态输入、NPU 执行、输出 CRC
  和 YOLOv8 检测结果合并为一次原子调用。
- 新增原生 `npu` 命令，支持 `info`、`selftest`、`report`、通用 `run` 和便捷
  `yolo` 子命令。后续 UVC 链路将直接使用相同 ABI，而不再组合多个 `echo`。
- ABI 使用整数化的千分比分数和百分之一像素坐标，避免向应用暴露内核浮点 ABI。
- 该增量已经完成 AArch64 全量编译与链接；由于本段尚未刷入开发板，所以不把
  ABI v4 的板端行为列入上面的“已通过”清单。

## 进行中的外部 USB/UAC/UVC

v113 是最后一个取得实机日志的诊断版本，仍未观察到端口连接。v114 候选加入
Combo PHY USB 表、host/UTMI 设置和最小 xHCI ring，但只有构建证据，没有实机
日志。USB 麦克风和摄像头均尚未枚举。完整边界见
`docs/a733/USB_V114_CANDIDATE.md`。

下一实现检查点：

1. 保留当前可用的固件交接状态；
2. 不再复位 DWC3 或 xHCI；
3. 建立 DMA32 DCBAA、命令环、事件环和中断器状态；
4. 启动控制器并发出 Enable Slot / Address Device；
5. 枚举描述符，实现 UVC PROBE/COMMIT 并采集帧；
6. 转换/缩放为 RGB640，送入现有 YOLOv8 动态输入并输出框、类别和分数。

## 尚未完成

- Bluetooth HCI 和协议配置文件；
- Imagination BXM GPU 运行时；
- UFS 主机/存储和 UFS 优先启动；
- MIPI CSI IMX214 驱动；
- SMP 已基本运行；DVFS、热管理和生产级多核长稳仍待加固；
- openvela 内通用 NBG 编译器/链接器。

这些项目有意不计入已完成特性声明。
