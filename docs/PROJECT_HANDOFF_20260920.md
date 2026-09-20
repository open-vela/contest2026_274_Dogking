# Cubie A7Z openvela AI 桌宠项目交接文档

- 更新时间：2026-09-20
- 目标读者：接手后继续开发、构建、烧录和实机调试的 Agent/开发者
- 当前开发分支：`a733-cubie-a7z-share`
- 个人协作仓库：`https://github.com/hxy299/hxy299-a733-share-hub.git`
- 正式比赛仓库：`https://github.com/open-vela/contest2026_274_Dogking.git`

> 本文是当前状态的总入口。`docs/` 中 v69～v120 的阶段记录、失败日志和板测
> 证据必须保留；遇到冲突时，以日期更新且明确标注“实机通过”的文档、真实串口
> 日志和当前源码为准，不要把“编译通过”写成“硬件通过”。

## 0. 接手前必须遵守的规则

1. 不要永久修改 `quickly-openvela` 官方工作区。板级/应用源码进入本比赛仓库的
   `openvela-overlay/`，公共仓必要修改进入 `patches/`；构建脚本负责临时覆盖、
   应用补丁，并通过 trap 恢复。
2. 每个可运行阶段先提交 Git 恢复点，再做高风险的 SMP、MMU、USB、NPU、时钟
   或大规模重构。禁止使用 `git reset --hard` 清除不确定来源的改动。
3. 不删除任何现有开发过程文档。过时结论应在新文档中标注被哪个版本取代，
   而不是抹掉失败过程。
4. 不提交 Wi-Fi 密码、API Key、SSH/FTP 密码、主机私钥、用户数据、模型、A7PM、
   `.img`、ELF、构建目录或厂商许可不明的二进制。
5. 个人协作阶段只推送 `share` 远端。正式参赛前必须按
   `docs/SUBMISSION_CHECKLIST.md` 把 manifest/远端恢复为组委会要求，并处理 CLA。
6. 用户的旧 Linux 桌宠工程只读参考，不得修改：
   `D:\My-Program\AI-Agent-Program\cc-program\Radxa_A7Z_Sencond_Pro\Now-Program`。
7. 旧 ST7735 适配对话只读参考：
   `D:\My-Program\AI-Agent-Program\cc-program\Radxa_A7Z_Sencond_Pro\.vscode\chat-json\chat-ST7735.json`。

## 1. 项目目标

在 Radxa Cubie A7Z（Allwinner A733，6×Cortex-A55 + 2×Cortex-A76，4 GiB
内存，VIP2 NPU）上原生运行 openvela，并完成可参赛、可复现的 AI 桌宠：

```text
文本/语音/视觉输入
  -> 低延迟路由
  -> openvela 官方 AI Agent（DeepSeek/MiMo）或本地 Qwen2.5-1.5B
  -> 情绪、动作和文本输出
  -> UART4 TTS / 后续 I2S 音频 / ST7735 LVGL 表情
  -> 摄像头帧经 VIP2 NPU YOLOv8 产生视觉事件
```

产品原则是“openvela 官方能力优先”：官方已经提供 Agent、消息总线、TLS、Skills、
LVGL、LCD、Audio/Media 抽象时，优先使用官方接口；只有 A733 缺少的 SoC/板级
lower-half 和产品层路由才自建。底层仍必然包含 NuttX 内核、驱动与 POSIX API，
但项目不是只运行 NuttX 样例，而是组合 openvela 官方组件形成产品链路。

## 2. 技术栈、依赖与版本

### 2.1 硬件

- 开发板：Radxa Cubie A7Z，Allwinner A733，4 GiB，当前样板无 UFS。
- CPU：CPU0..5 Cortex-A55，CPU6..7 Cortex-A76；8 核 SMP 已实机启动。
- NPU：VIP2，CID `0x1000003b`，约 3 TOPS（厂商标称）。
- Wi-Fi/BT：FCU760K / AIC8800D80-U02，专用 USB1；BT 尚未完成。
- 数据盘：microSD GPT，第 4 分区 FAT 挂载 `/data`。
- UART TTS：UART4，PJ24/PJ25，9600 8N1，TW-TTS UTF-8 协议。
- 显示：1.8 英寸 ST7735，128×160，RGB565，SPI Mode 0。
- 计划音频：MAX98357A 扬声器、INMP441 麦克风（均尚未形成 PCM 设备）。
- 计划 USB：Linux 下免驱的 USB 麦克风和 UVC 摄像头；openvela xHCI/UAC/UVC
  尚未完成。

### 2.2 软件

- openvela + NuttX ARM64，U-Boot `booti`，EL1。
- openvela 官方 `packages_ai_agent`：消息总线、上下文、LLM proxy、TLS、Skills、
  工具注册和 NSH 命令。
- openvela/NuttX 官方 LVGL、`lcd_dev`、通用 ST7735 驱动。
- C/C++，CMake/Ninja，AArch64 bare-metal toolchain。
- WSL2 Ubuntu（Windows 11 主机）负责 Linux 构建；PowerShell 负责 Git/文件管理。
- 本地 LLM：Qwen2.5-1.5B-Instruct GGUF Q4_K_M，CPU/NEON，不使用 NPU。
- NPU 模型：LeNet、YOLOv5s、YOLOv8n-PCQ，A7PM prepared 包。

### 2.3 本机关键路径

```text
工作区根：
D:\My-Program\AI-Agent-Program\GPTsprogram\Openvela

官方完整工作区（临时使用，构建后必须恢复）：
D:\My-Program\AI-Agent-Program\GPTsprogram\Openvela\quickly-openvela

实际提交仓库：
D:\My-Program\AI-Agent-Program\GPTsprogram\Openvela\A733-A7Z-ALL-Files\contest2026_274_Dogking-dev-ai-contest-2026

大型本地制品目录：
D:\My-Program\AI-Agent-Program\GPTsprogram\Openvela\A733-A7Z-ALL-Files
```

## 3. 如何获取、运行、构建和测试

### 3.1 从个人协作仓库获取完整工作区

在 Ubuntu/WSL 中执行：

```bash
mkdir -p ~/openvela-contest
cd ~/openvela-contest
repo init \
  -u https://github.com/hxy299/hxy299-a733-share-hub.git \
  -b a733-cubie-a7z-share \
  -m contest2026_274_Dogking.xml
repo sync -c -j8
```

manifest 会把仓库中的 overlay/linkfile 映射到完整 openvela 工作区。不要只复制
单个 `nuttx.bin` 后宣称源码可复现。

### 3.2 构建

```bash
cd ~/openvela-contest
bash contest2026_274_Dogking/tools/build-a733.sh
```

本机现有工作区可用：

```bash
cd /mnt/d/My-Program/AI-Agent-Program/GPTsprogram/Openvela/quickly-openvela
OPENVELA_WORKSPACE="$PWD" \
A733_BUILD_DIR="$PWD/cmake_out/cubie-a7z_nsh" \
bash ../A733-A7Z-ALL-Files/contest2026_274_Dogking-dev-ai-contest-2026/tools/build-a733.sh
```

可选：`--incremental`、`JOBS=12`。首轮或 defconfig 改动后应全量构建。构建脚本
会执行 openvela-first 边界检查、暂存 overlay、应用补丁、校验 SMP/WAPI/显示配置、
输出哈希，并在成功、失败或中断后恢复官方源码。

主要产物：

```text
cmake_out/cubie-a7z_nsh/nuttx.bin
cmake_out/cubie-a7z_nsh/nuttx
cmake_out/cubie-a7z_nsh/System.map
```

`nuttx.bin` 是带 Linux ARM64 Image header 的内核，必须由 U-Boot `booti` 启动，
不能使用 `go 0x40200000`。

### 3.3 生成和验证镜像

```bash
BASE=/path/to/known-good-a733-base.img
OUT=/path/to/openvela-a733-current-candidate.img
KERNEL=cmake_out/cubie-a7z_nsh/nuttx.bin

bash contest2026_274_Dogking/tools/package-a733-kernel-image.sh \
  "$BASE" "$OUT" "$KERNEL"

KERNEL_SHA=$(sha256sum "$KERNEL" | cut -d' ' -f1)
bash contest2026_274_Dogking/tools/verify-a733-image.sh \
  "$OUT" "$KERNEL_SHA"
```

打包器只复制基础镜像并替换第 3 分区 `/boot/openvela/a733/Image`，不会覆盖输入。
验证必须同时通过 `e2fsck -fn`、内核回读哈希和 `sgdisk -v`。

### 3.4 当前最新候选 v120

```text
镜像：openvela-a733-cubie-a7z-sd-st7735-lvgl-v120-candidate.img
大小：2147483648 bytes
SHA-256：6bececd77790eba146de0ef5762a2ec11b296a9665fb08f7f9fe24e67024941c

内核：nuttx.bin
大小：2183008 bytes
SHA-256：f3871b2422ba000e14df016197fe2a09217a58f03f873e62b4e5084c73700eb7
```

本机镜像路径：
`A733-A7Z-ALL-Files/openvela-a733-cubie-a7z-sd-st7735-lvgl-v120-candidate.img`。
该大文件不进入 Git。v120 已通过构建、ELF 符号、ext4/GPT 和内核回读校验，
但 ST7735 仍待实机点亮。

### 3.5 首轮综合板测

```text
uname -a
free
ps
cat /dev/a733-hw
wifi scan
wifi connect <SSID>
ifconfig
ping -c 6 <gateway>

aipet status
aipet ask "请只回答：在线链路成功"
aipet local status
aipet local ask "请只回答：本地链路成功"

cat /dev/a733-uart4
aipet tts status
aipet tts test "串口语音测试成功"

cat /dev/npu0
npu info
npu selftest

ls /dev/lcd0
display status
display test 30
```

板端日志与照片必须按版本保存。任何失败都先收集 `dmesg`、相关 `/dev/a733-*`
诊断节点和 `ps/free`，不要立即叠加多项寄存器修改。

## 4. 目录结构说明

```text
contest2026_274_Dogking/
├── board/a733-cubie-a7z/
│   ├── openvela-overlay/
│   │   ├── vendor/allwinnertech/chips/a733/      # A733 SoC lower-half/诊断
│   │   ├── vendor/allwinnertech/boards/a733/     # Cubie A7Z 板级初始化
│   │   └── apps/system/
│   │       ├── a733wifi/                          # Wi-Fi 管理
│   │       ├── a733services/                      # 自启动/服务管理
│   │       ├── a733ftpd/                          # FTP 服务
│   │       ├── a733npu/                           # NPU CLI
│   │       ├── a733display/                       # ST7735/LVGL UI 入口
│   │       ├── aipet/                             # 桌宠路由/Agent/TTS
│   │       ├── aipetllm/                          # 本地 Qwen 推理
│   │       └── aipetasr/                          # 本地 ASR 骨架
│   ├── archives/                                  # 本地恢复材料，Git 忽略
│   └── README.md
├── docs/                                          # 总览、阶段记录和板测证据
│   └── a733/                                      # v69～v119 专项过程文档
├── patches/                                       # 公共仓临时兼容补丁
├── tools/
│   ├── build-a733.sh                              # 稳定构建入口
│   ├── build-a733-wapi.sh                         # 实际暂存/补丁/构建逻辑
│   ├── package-a733-kernel-image.sh               # 镜像复制与内核替换
│   └── verify-a733-image.sh                       # 独立镜像校验
├── contest2026_274_Dogking.xml                    # repo manifest/linkfile
├── openvela.xml                                   # 官方比赛基线
└── README.md                                      # 对外总览
```

重要权威文档：

- `docs/IMPLEMENTATION_STATUS.md`：完成边界。
- `docs/TEST_EVIDENCE.md`：真机证据。
- `docs/BUILD_AND_IMAGE.md`：可复现构建/镜像。
- `docs/ARTIFACTS.md`：大文件与哈希。
- `docs/OPENVELA_COMPONENT_BOUNDARIES.md`：openvela/NuttX/板级边界。
- `docs/AI_PET_OPENVELA_FIRST_ARCHITECTURE.md`：官方能力优先约束。
- `docs/a733/AIPET_CHAIN_OVERVIEW_20260920.md`：桌宠全链路。
- `docs/a733/AIPET_LOCAL_LLM_AGENT.md`：本地 Qwen 路由。
- `docs/a733/AUDIO_UART_I2S_STAGE.md`：UART/I2S。
- `docs/a733/USB_V114_CANDIDATE.md`：外部 USB 冻结点。
- `docs/ST7735_LVGL_PORT.md`：显示接线和 v120 板测。
- `docs/SUBMISSION_CHECKLIST.md`：正式参赛前必做项。

## 5. 已完成功能

### 5.1 真机已通过

- Boot0/SCP/BL31/U-Boot `booti` → ARM64 openvela EL1 → NSH。
- 8 核 SMP：6×A55 + 2×A76；4 GiB 分区堆、procfs、信号、Ctrl+C。
- UART0、LED、看门狗、THS、I2C2/I2C7、SPI1、风扇 PWM。
- SDMMC0、GPT 分区节点、FAT `/data` 读写。
- FCU760K 双频扫描、WPA2/CCMP、DHCP、ARP、DNS、TCP/UDP、断线状态清理。
- `wifi`/WAPI、NTP、curl、wget、HTTPS、SSH/SCP、FTP、iperf。
- 服务可选自启动、Wi-Fi 记忆上次连接并自动连接。
- 官方 `packages_ai_agent`：DeepSeek/MiMo 初始化、API Key 加密保存、自启动、
  连续中文对话；首次 TLS 约 7.5 秒、复用后约 1.8 秒（现场样本）。
- UART4 TW-TTS：`/dev/ttyS4`、UTF-8 初始化和文本帧、自动播报。
- 本地 Qwen2.5-1.5B：tokenizer、Q4_K/Q6_K、NEON、28 层前向、KV cache、
  中文流式输出、有限多轮、模型常驻、CPU2..7 工作池。
- 本地 Qwen 已通过 `LocalRouteBackend` 接入桌宠，支持断网/云端失败回退。
- VIP2 NPU：MMU/DMA/IRQ/任务 ABI，LeNet、YOLOv5s、YOLOv8n PCQ golden
  回归及动态 dog/black 输入。

### 5.2 已构建和打包，仍待对应硬件实测

- ST7735/LVGL v120：官方 ST7735、`/dev/lcd0`、官方 LVGL NuttX LCD backend、
  `display status/test/run`。
- NPU 原子 A7PM ABI v4 的最新应用接口（旧模型路径已实机验证；最新 ABI 仍应
  在新镜像做一次完整回归）。
- 外部 USB v114 诊断候选，只能称为 xHCI/PHY 研究起点，不能称为枚举完成。
- I2S0 `/dev/a733-audio` 非破坏诊断；没有 PCM lower-half。

### 5.3 关键实机参考值

- NPU CID：`1000003b`。
- LeNet CRC：`d50f5d79`。
- YOLOv5s CRC：`53127e9c ddb5675e 6e01633e`。
- YOLOv8n CRC：`55d0becd ead06611 ba209c57 2637657c 9e7f743a 2eb4d5ce`。
- Wi-Fi TCP 功能基线约 4.13 Mbit/s；UDP 约 10.8 Mbit/s（不是峰值承诺）。
- Qwen 模型首次 SD PIO 加载约 175 秒；常驻后单 token 前向检查点约 0.6 秒。
- UART4 正常诊断：`bgr=00010001`、PJ24/PJ25 function 4、`LSR=0x60`、
  `USR=0x06`。

## 6. 当前正在做什么

最近阶段是 ST7735/LVGL：

- 最新显示提交：`4df0e90 feat: add A733 ST7735 LVGL display stack`。
- 已把旧 Linux 工程的接线和面板参数转换为标准 openvela/NuttX 板级实现。
- 通用控制器逻辑没有复制进私有驱动，而是复用官方 `st7735.c`。
- A733 SPI1 增加 16-bit RGB565 和 `SPI_CMDDATA`；PL5 是 D/C，PB0 是 RESET。
- v120 镜像已验证，但用户当前尚未回传屏幕实机结果。

ST7735 接线：

| 信号 | A733 | 物理 Pin |
| --- | --- | ---: |
| SCK | PD11 / SPI1_CLK | 23 |
| MOSI | PD12 / SPI1_MOSI | 19 |
| CS | PD10 / SPI1_CS0 | 24 |
| DC | PL5 / R_PIO | 22 |
| RESET | PB0 | 7 |
| VCC/BL | 3.3 V | 1/17 |
| GND | GND | 任一 GND |

## 7. 待办事项（按建议优先级）

### P0：接手后先建立可信基线

1. 拉取个人远端分支，确认 HEAD 与本文提交之后的交接提交一致。
2. 全量构建一次，确认构建脚本退出后官方工作区没有残留补丁或 `.rej`。
3. 烧录 v120，回归启动、SMP、Wi-Fi、Agent、UART TTS、本地 Qwen、NPU。
4. 点亮 ST7735：保存 `dmesg`、`display status`、`display test 30` 和屏幕照片。
5. 若颜色/方向错误，只先调整 `CONFIG_LCD_ST7735_BGR` / `CONFIG_LCD_RPORTRAIT`；
   不要在应用层偷偷交换颜色或坐标。

### P1：完成桌宠显示产品层

1. 建立单一 LVGL UI 线程和有界消息队列。
2. 将 `PetReply.emotion/actions/text` 转成 UI 事件；Agent/LLM/TTS 线程不得直接
   调用 LVGL。
3. 实现 idle/listening/thinking/speaking/error 表情，保持状态直到明确切换。
4. 先做局部刷新和 10～15 FPS，再测 SPI polling 性能；必要时单独开发 DMA，
   不要把性能重构和功能修复混在一个提交。

### P1：打通语音输入

1. 优先选择一条硬件路径，不要 USB 与 I2S 同时试错。
2. I2S 路线：先读 `/dev/a733-audio` 实测 PLL/CCU，再实现标准 Audio lower-half；
   先 MAX98357A PCM 播放，再 INMP441 capture。
3. USB 路线：先获取同板 Linux 的 `lsusb -v`、拓扑和 xHCI 寄存器黄金基线，
   再移植标准 xHCI platform host/UAC；不要继续手写不完整的枚举状态机。
4. PCM 成立后连接官方 Media Recorder → 云端 ASR → 当前文字路由 → UART TTS。
5. 本地 ASR 骨架只有接口和 mock；真实模型、依赖、内存和延迟必须重新验收。

### P1：摄像头 → NPU → 桌宠

1. 完成 UVC 枚举、PROBE/COMMIT、等时/批量端点和稳定帧采集。
2. 明确像素格式，完成 RGB640 resize/letterbox/量化。
3. 调用 NPU 原子 ABI 执行 YOLOv8，解析框、类别、分数。
4. 将检测结果作为桌宠视觉事件输入统一路由和 UI，不要让视觉线程直接操作 LVGL。
5. 人脸识别需要新增合法模型与隐私策略；当前只完成通用检测基础。

### P2：本地 LLM 优化

1. 建立 Linux/llama.cpp 同模型、同 prompt、同 token 的数值回归。
2. 完成跨轮持久 KV cache、上下文截断和稳定 session ID。
3. 增加温度/top-k/top-p 等采样，但保留确定性 greedy 回归模式。
4. 继续优化 NEON、行分块、权重页缓存和热管理；实测而非按核心数推断收益。
5. 可靠取消：优先 `aipet local stop`，补齐 Ctrl+C、超时、卸载和并发请求测试。

### P2/P3：其余平台功能

- Wi-Fi 长稳、重连、DFS、并发吞吐、网络就绪分级。
- 官方 Agent 流式片段接 `SentenceStream`，降低首句延迟。
- Bluetooth HCI、GPU、UFS、MIPI CSI IMX214。
- 生产级 DVFS、温控、看门狗恢复、掉电一致性和权限安全。
- 将公共仓补丁拆分成符合 openvela upstream 流程的独立 PR。

## 8. 已知 Bug、限制和报错

1. **ST7735 未实机验证**：v120 只证明编译/链接/镜像正确；可能仍有面板 variant、
   RGB/BGR、方向、偏移或刷新性能问题。
2. **SPI 性能风险**：当前 A733 SPI1 是 polling lower-half，限制 12 MHz，FIFO
   分块 63 bytes；官方初始化中的逐像素 fill 可能较慢。先实测再优化。
3. **SMP 启动串口乱码/交错**：8 核早期启动同时打印导致字符交错，进入 NSH 后
   通常正常；不应误判为内核崩溃，但生产版应收敛早期输出。
4. **本地模型首次加载慢**：约 1.1 GiB 从 SDMMC PIO 读取约 175 秒。常驻后才快。
5. **本地 LLM 热量和取消**：长生成可能过热降频；强制 Ctrl+C/并发卸载没有完成
   全面长稳验证，优先协作式 stop。
6. **网络 ready 判断较粗**：产品层主要以 `wlan0` IPv4 判断在线，不等于 DNS、
   NTP、TLS 和公网都可用；需要分级状态机。
7. **USB 外设未枚举**：v113/v114 的 `enumeration=-11` 表示未执行完整标准枚举，
   不是设备描述符已失败。USB 麦克风和摄像头均不可用。
8. **I2S 无数据流**：`/dev/a733-audio` 只是非破坏诊断，MAX98357A/INMP441
   没有 BCLK/LRCK/PCM。
9. **NPU 模型工具边界**：openvela 可执行准备好的 A7PM，但通用专有 NBG
   prepare/link 仍依赖授权 Linux VIPLite 工具，不能伪造开源实现。
10. **云端首次请求延迟**：TLS 首次握手明显慢于复用连接；需要连接保活和网络
    就绪初始化，但不能把 API Key 烧进镜像。
11. **大文件不在 Git**：模型、镜像、A7PM 和固件必须按 `docs/ARTIFACTS.md`
    单独复制并验证哈希。

## 9. 关键决策及原因

1. **官方能力优先**：Agent 用 `packages_ai_agent`，显示用官方 LVGL/ST7735，
   避免项目被认定为仅适配 NuttX 或重复造轮子。
2. **overlay + patch + trap 恢复**：比赛代码可提交，官方 repo 保持干净，也能
   精确说明哪些修改属于板级、哪些应 upstream。
3. **逐阶段硬件检查点**：复杂 SoC bring-up 一次只验证一个可观测层，所有轮询
   有超时，避免启动永久卡死。
4. **CPU 与 NPU 分工**：Qwen2.5-1.5B 当前跑 CPU，因为 VIP2 工具链/算子和动态
   KV 不适合直接迁移；YOLO/LeNet 使用 NPU prepared 路线。
5. **CPU0..1 留系统，CPU2..7 给 LLM**：默认掩码 `fc`，降低 Wi-Fi/Agent 与
   计算工作池互相干扰；A76 行权重高于 A55，但仍以实测为准。
6. **模型常驻内存**：用约 1.1 GiB RAM 换掉每 token 重读 SD；4 GiB 平台可承受。
7. **UART TTS 先于 I2S**：用户已有可用模块，链路风险低；I2S 必须先把官方
   audio lower-half 做对，不能把裸 FIFO 写入桌宠业务代码。
8. **动作/语音/视觉故障隔离**：任何表现硬件失败都不应拖垮文字对话主链路。
9. **显示单线程**：LVGL 只由 UI 线程调用，其余模块发送事件，减少线程安全故障。
10. **大制品不进 Git**：避免 GitHub 限制、许可证风险、凭据泄漏和不可审查历史。

## 10. 已尝试但失败或被替代的方案

1. **WPA2 M3 后先装 PTK 再发 M4**：严格 AP 会拒绝加密时序错误的 M4，出现
   reason 15；已改为发送 M4 后再安装 PTK/GTK。
2. **只靠私有 `/dev/a733-wifi` 管理网络**：不符合 openvela 公共接口目标；已迁移
   到 WEXT/WAPI，私有节点只保留诊断和兼容。
3. **UART4 使用错误 CCU/传统 pinctrl 地址**：寄存器全零、FIFO 超时；已按官方
   DTS/sun60iw2 HW type 4 修正，并在 v118 实机通过。
4. **UART termios 混用 `B9600` 编码与数值 9600**：导致 `TCSETS -EINVAL`；已
   分别保存 `c_cflag` 编码和 `c_speed` 数值。
5. **xHCI 直接堆寄存器试验**：虽能进入 RUN，但没有完整 Root Hub、Enable Slot、
   Address Device、EP0 和等时传输，端口仍无 CCS；v114 后冻结，建议移植标准
   platform host。
6. **每次 LLM token 从 SD 读取权重**：单 token 曾约 473 秒；模型常驻 RAM 后
   降到约 0.6 秒前向检查点。
7. **仅使用单个 A76**：约 1.15 秒；固定 2×A76 或 6 核一度收益有限，后续真正
   持久工作池和并行矩阵路径约 0.6 秒。不能只设置 affinity 而不拆分计算。
8. **过早并行启动 SMP 次核**：曾出现 CPU7 `ELR=0`、同步异常和启动失败；通过
   次核启动栈边界/页对齐和分阶段 2→6→8 核验证修复。相关 v89～v92 文档保留。
9. **直接移植 Linux `spidev`/Python ST7735**：不适合 openvela 产品和提交要求；
   当前改为官方 NuttX ST7735 + LVGL + A733 板级 glue。
10. **把编译成功当硬件成功**：USB、I2S、显示都曾容易产生这种误判；当前文档
    强制区分源码、构建、镜像和真机四类证据。

## 11. 环境变量和运行配置（已脱敏）

### 11.1 构建变量

```text
OPENVELA_WORKSPACE=/path/to/full/openvela/workspace
A733_BUILD_DIR=/path/to/cmake_out/cubie-a7z_nsh
JOBS=8
BUILD_KEEP_GOING=0|1
```

### 11.2 板端数据目录

```text
/data/aic/                         FCU760K 固件（不进 Git）
/data/npu/                         A7PM、输入和 NPU 数据（不进 Git）
/data/models/                      GGUF 等模型（不进 Git）
/data/ai_agent/config/             Agent/桌宠配置
/data/ai_agent/skills/             Skills
/data/ai_agent/memory/             记忆
/data/ai_agent/sessions/           会话
/data/ai_agent/ca.pem              HTTPS CA
```

默认本地模型：

```text
/data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf
大小：1117320736 bytes（以板端文件及来源哈希再次核对）
默认 CPU mask：fc（CPU2..7）
默认 max tokens：32，可配置 1..64
```

### 11.3 私密配置

- Wi-Fi 密码由 `wifi connect` 隐藏输入并保存到板端，不写入源码。
- DeepSeek/MiMo API Key 由 `aipet init` 隐藏输入并以板级密钥加密保存。
- SSH host key、SSH/FTP 密码只存在 `/data`。
- 交接仓库中不应出现真实 key/token/password；接手者必须再次执行 secret scan。

## 12. 下一步建议

最稳妥的接手顺序：

1. **不要先改代码**：拉取、构建、核对 v120 哈希和工作区恢复。
2. **先完成显示实机闭环**：它是当前唯一“代码和镜像已好、只差板测”的最近任务。
3. **提交 v121 显示板测恢复点**：记录面板照片、日志、方向/色序和刷新耗时。
4. **再把桌宠事件接 LVGL**：只改产品层，不同时动 SPI lower-half。
5. **语音优先选 I2S 或 USB 一条路线**：建议若 MAX98357A/INMP441 在手，先做
   I2S 标准 audio lower-half；USB 必须先重建标准 xHCI platform 架构。
6. **摄像头链路最后接 NPU**：NPU 已可用，真正风险在采集与预处理，不要重复
   修改已通过的 VIP2 执行器。
7. 每完成一层都更新 `IMPLEMENTATION_STATUS.md`、`TEST_EVIDENCE.md`、
   `ARTIFACTS.md` 和对应阶段文档，再提交和推送个人协作分支。

## 13. Git 恢复点和远端策略

近期关键提交：

```text
4df0e90  feat: add A733 ST7735 LVGL display stack
7fc0adf  feat: connect local Qwen fallback to AI pet
4c2edac  docs: record UART4 TTS board validation
1c1d944  fix: correct A733 UART4 MMIO mapping
8504457  fix: sequence A733 UART4 clock and reset
f192890  fix: correct A733 UART4 termios baud handling
002a310  feat: add A733 UART4 agent TTS and I2S diagnostics
17b06c9  fix: drain FCU760K runtime message endpoint
```

远端用途：

```text
share  -> hxy299/hxy299-a733-share-hub.git       当前联合开发/恢复仓库
origin -> open-vela/contest2026_274_Dogking.git  正式比赛仓库，当前不要直接推送
```

日常推送：

```bash
git switch a733-cubie-a7z-share
git status
git pull --ff-only share a733-cubie-a7z-share
git push share a733-cubie-a7z-share
```

若队友并行开发，优先短分支 + PR 合入个人仓库，不要 force push。正式提交组委会
前，重新阅读官方当时最新规则、签署 CLA、恢复官方 manifest/分支，并运行
`docs/SUBMISSION_CHECKLIST.md`；个人仓库配置只是临时协作方案。

## 14. 交接验收清单

接手者在开始新功能前应能回答并证明：

- [ ] 当前 HEAD、分支和 `share` 远端正确。
- [ ] 仓库无未提交改动，历史 docs 未被删除。
- [ ] 全量构建成功，`nuttx.bin`/ELF/System.map 齐全。
- [ ] 构建退出后官方工作区无项目补丁残留、无 `.rej`。
- [ ] 镜像校验通过，烧录目标 TF 卡前已备份 `/data` 私密配置和模型。
- [ ] 板端 8 核、内存、存储、Wi-Fi、Agent、UART TTS 和 NPU 基线未回退。
- [ ] ST7735 的完成状态仍按实机证据描述。
- [ ] 新增功能使用 openvela 官方组件优先，并明确 NuttX/板级边界。
- [ ] 所有新凭据和大文件均未进入 Git。

完成这些检查后，再从第 7 节的 P0/P1 继续开发。
