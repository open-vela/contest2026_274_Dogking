# Dogking A733：面向端侧 AI 的 openvela 多平台新硬件适配

**2026 首届 openvela AI 硬件开发者大赛 · 技术报告**

---

## 1、信息表

| 项目 | 内容 |
| --- | --- |
| 作品名称 | Dogking A733 —— 面向端侧 AI 的 openvela 多平台新硬件适配 |
| 队伍名称 | Dogking（专属仓 `contest2026_274_Dogking`） |
| 团队分工 | 见下方"团队分工"说明 |
| 选题方向 | **主赛道：新硬件平台适配**；扩展方向：**AI 硬件产品创新** |

### 团队分工

| 成员 | GitHub | 承担工作 |
| --- | --- | --- |
| 队长 / 独立开发 | `hxy299` | 全部 SoC 移植、驱动开发、AI 桌宠产品层、构建与镜像工具链、文档与 AI Coding 日志 |
| 组委会 | — | 提供专属仓、基线 manifest 与赛事资料 |

> 本作品由 `hxy299` 独立完成。仓库 `logs/hxy299/` 下的 AI Coding 日志可直接核对
> 全部开发过程（107 个会话、28 个自然日、23,933 条事件）。

### 选题方向说明

本作品同时命中两个方向，且**以新硬件适配为主**：

1. **新硬件平台适配（主）**：在一块此前没有 openvela 支持的国产 SoC 开发板
   （Allwinner A733 / Radxa Cubie A7Z）上完成从 BootROM 到 AI 应用的全栈移植，
   并额外完成两颗 Rockchip SoC（RK3506、RV1103）的 openvela 移植。
2. **AI 硬件产品创新（扩展）**：在此平台上落地一个可离线运行、断网可降级的
   AI 桌宠产品，深度使用 openvela 官方 AI Agent 组件与 NPU。

---

## 2、摘要

本作品将 openvela/NuttX 原生移植到三块国产 SoC 开发板，并在其上构建可离线运行的
AI 桌宠产品。主平台为瑞莎 Radxa Cubie A7Z（全志 A733，2×Cortex-A76 +
6×Cortex-A55，4 GiB LPDDR4，3 TOPS VIP2 NPU）。项目**不在 Linux 用户空间模拟
openvela**，而是由板载 Boot0/SCP/BL31/U-Boot 通过 `booti` 直接引导 ARM64
openvela 内核进入 EL1。

已完成并**经真机验证**的能力：8 核 SMP 启动、4 GiB 分区堆、microSD/GPT/FAT 文件
系统、FCU760K 双频 Wi-Fi（WPA2/DHCP/DNS/TCP/UDP，实测 TCP 4.13 Mbit/s）、
SSH/FTP/curl/wget/NTP 网络服务、VIP2 NPU 真实推理（LeNet/YOLOv5s/YOLOv8n，
CRC32 与 Linux VIPLite 黄金结果逐字节一致）、openvela 官方 `packages_ai_agent`
联网对话（DeepSeek/MiMo，首次 TLS 7.5 s、复用 1.8 s）、本地
Qwen2.5-1.5B Q4_K_M CPU 推理（6 核工作池，单 token 前向约 0.6 s）、UART4
TW-TTS 语音播报（`frames=4 failures=0 stage=complete`）。

创新点有三：其一，**一次适配三颗 SoC**，并把可复用的移植方法论沉淀为
3 个自定义 Skill；其二，全部硬件结论以**串口日志与寄存器实测**为准，建立了
"源码 / 构建 / 镜像 / 真机"四级证据边界，明确拒绝把"编译通过"写成"硬件通过"；
其三，在 AI Agent 链路中实现**云端优先 + 本地 Qwen 兜底**的断网降级路由。

---

## 3、正文

### 3.1 绪论

#### 3.1.1 项目背景与问题定义

**应用场景**：离线 AI 助手与视觉识别终端。典型场景包括家庭陪伴机器人、工业
巡检终端、无网环境下的语音交互设备。这类场景的共同约束是：网络不可靠或不
允许上云、功耗与成本受限、需要本地实时响应。

**行业 / 用户痛点**（具体化）：

1. **国产 SoC 缺少 RTOS 生态**。全志 A733、瑞莎 Cubie A7Z 这类新板卡出厂只
   提供 Debian/Linux 镜像。想在它上面做低功耗、快启动、可裁剪的产品形态，
   开发者必须先自己完成一整条 BSP 移植，工作量以月计。
2. **"能跑 Demo"与"能出产品"之间有一道鸿沟**。大量所谓"适配完成"只到
   "内核能启动"，而 Wi-Fi、文件系统、NPU、音频、显示这些真正决定产品形态的
   子系统仍然是空的。
3. **AI 能力与硬件表现层脱节**。云端大模型 API 调用本身不难，难的是把它接到
   真实的屏幕、喇叭、麦克风、摄像头和 NPU 上，并且断网时不能整个产品失效。
4. **移植过程缺乏可迁移的方法论**。每换一颗芯片就从头踩一遍
   （CCU 基址、pinctrl 布局、MMU、DMA、USB PHY），经验无法沉淀。

#### 3.1.2 技术难点

| # | 难点 | 具体表现 |
| --- | --- | --- |
| D1 | **无参考 BSP 的冷启动** | A733 在 openvela 上游没有移植，需要自行推导 CCU/R_PIO/SPI/UART 寄存器基址与布局 |
| D2 | **寄存器基址与布局在不同芯片间不通用** | 全志 pinctrl 存在多套 HW type；用错布局会让寄存器"可写但无效"，且完全不报错 |
| D3 | **8 核 SMP 启动竞态** | 次核启动栈边界与页对齐处理不当会导致 `ELR=0` 同步异常 |
| D4 | **USB Wi-Fi 固件交接** | AIC8800D80 需要 BootROM → 固件上传 → 重新枚举 → LMAC/RF/ME 初始化多阶段交接 |
| D5 | **NPU 工具链闭源** | VIP2 的 NBG 编译器不开放，必须走 Linux VIPLite 黄金 trace → A7PM → openvela 执行器 |
| D6 | **1.1 GiB 大模型在 4 GiB 内存上的推理** | 权重从 SD 卡 PIO 读取极慢（曾达 473 s/token），需要常驻 + 多核并行 |
| D7 | **构建系统的静默失败** | 补丁"没打上"与"打上了"在 `patch` 的退出码上无法区分，会产出功能缺失但校验全过的镜像 |

#### 3.1.3 创新点

**技术层面**

1. **三平台一次性适配**：A733（ARM64，8 核）+ RK3506（三核异构）+ RV1103
   （ARM32 + RISC-V），三套完全不同的启动链与中断控制器。
2. **四级证据边界方法论**：把"源码正确 / 构建通过 / 镜像可校验 / 真机可运行"
   严格分开记录，并在文档中拒绝越级声明。这一条在比赛中直接转化为可信度。
3. **寄存器级排障范式**：不靠猜，而是建立"寄存器实测 → 对照官方 BSP 源码 →
   定位布局差异 → 反汇编验证修复已进入二进制"的闭环。本轮 ST7735 与 UART4
   的问题都是这样定位的。

**场景层面**

4. **断网降级不是开关而是路由**：云端 Agent 失败时自动切到本地 Qwen，
   而不是简单报错。网络状态分级（链路 / IP / DNS / TLS）而非单一 `wlan0` 判断。

**交互层面**

5. **AI 输出结构化标签驱动硬件**：LLM 回复中携带情绪/动作标签，经白名单
   解析后驱动屏幕表情与 TTS，而不是让业务线程直接操作硬件。

### 3.2 系统方案设计

#### 3.2.1 系统总体架构

```text
┌──────────────────────────────────────────────────────────────────────┐
│                          应用层 (apps/system)                        │
│  aipet 桌宠路由 │ aipetllm 本地推理 │ aipetasr 语音 │ a733display 显示 │
│  a733wifi 网络  │ a733npu NPU CLI  │ a733ftpd │ a733services 服务管理 │
└───────────────────────────┬──────────────────────────────────────────┘
                            │ 官方 API：ai_agent / LVGL / lcd_dev /
                            │          WAPI / netdev / uORB
┌───────────────────────────┴──────────────────────────────────────────┐
│              openvela 官方公共组件层 (packages / apps / frameworks)   │
│  packages_ai_agent：消息总线·上下文·LLM proxy·TLS·Skills·工具注册     │
│  LVGL + lcd_dev + drivers/lcd/st7735.c · apps/wireless/wapi          │
│  mbedtls · lwIP · VFS/FAT · procfs · NSH                            │
└───────────────────────────┬──────────────────────────────────────────┘
                            │ 标准 NuttX 驱动接口 (spi/i2c/mmcsd/lcd/…)
┌───────────────────────────┴──────────────────────────────────────────┐
│        板级移植层  vendor/<vendor>/boards/<soc>/<board>/src          │
│  A733: cubie-a7z  │  RK3506: luckfox-lyra-zero-w  │  RV1103: pico-mini │
│  板级初始化·GPIO/I2C/SPI/SDMMC 封装·设备注册·启动链适配              │
└───────────────────────────┬──────────────────────────────────────────┘
                            │
┌───────────────────────────┴──────────────────────────────────────────┐
│        芯片移植层  vendor/<vendor>/chips/<soc>                       │
│  A733: CCU/R_PIO/SPI1/UART4/NPU VIP2/SDMMC/Wi-Fi AIC8800D80         │
│  RK3506: GPIO/I2C/SPI/SDMMC/Timer/IRQ/MemoryMap                     │
│  RV1103: GPIO/I2C/SPI/DMA/SFC-NAND/SARADC/PWM/TRNG/USB-CDC/TSADC/WDT │
└───────────────────────────┬──────────────────────────────────────────┘
                            │
┌───────────────────────────┴──────────────────────────────────────────┐
│                  openvela / NuttX 内核 (ARM64 / ARM32 / RV)          │
│  SMP·MMU·GICv3·调度·VFS·网络栈·POSIX                                │
└──────────────────────────────────────────────────────────────────────┘
```

**数据流（AI 桌宠主链路）**：

```text
用户输入(文字/语音)
   │
   ├─→ 快速规则匹配 ──命中──→ 直接回复
   │
   └─→ 未命中
         │
         ├─→ 网络就绪？ ──是──→ 官方 packages_ai_agent
         │                        └─ TLS → DeepSeek / MiMo
         │                             │
         │                          失败/断网
         │                             ↓
         └────────────────────→ 本地 Qwen2.5-1.5B (CPU2..7, 常驻)
                                   │
                                   ↓
                        PetReply { text, emotion, actions }
                                   │
                    ┌──────────────┼──────────────┐
                    ↓              ↓              ↓
              LVGL 表情      UART4 TW-TTS     动作白名单
              (/dev/lcd0)    (/dev/ttyS4)     (安全接口)
```

#### 3.2.2 方案论证与选型

**开发板选型理由**

| 候选 | 结论 | 理由 |
| --- | --- | --- |
| **Radxa Cubie A7Z（A733）** | **主平台，选用** | 8 核 big.LITTLE、4 GiB 内存（足以常驻 1.1 GiB 模型）、3 TOPS NPU、板载双频 Wi-Fi 模组、上游无 openvela 支持——适配价值最高 |
| Luckfox Lyra Zero-W（RK3506） | 选用为第二平台 | 三核异构、成本低、Rockchip 生态资料完整，验证方法论可移植性 |
| Luckfox Pico Mini（RV1103） | 选用为第三平台 | 极低成本 + 内置 NPU、SPI NAND 启动，验证小内存与异构核场景 |
| 树莓派 4B / RK3588 板 | 未选 | 上游已有较好 openvela/NuttX 支持，适配增量小，不符合"新硬件适配"赛道的得分取向 |
| 纯 Linux 用户态跑 openvela | 否决 | 不符合大赛"项目须使用 openvela 系统能力"的要求，也无法体现 BSP 移植工作量 |

**端 / 云职责划分与降级策略**

| 能力 | 端侧 | 云侧 | 断网 / 弱网降级 |
| --- | --- | --- | --- |
| 对话 | 本地 Qwen2.5-1.5B（28 层、KV cache、中文流式） | DeepSeek / MiMo | 云端失败 → 自动切本地；本地为常驻进程，切换无加载延迟 |
| 视觉 | VIP2 NPU YOLOv8n（6 输出、动态输入） | — | 完全本地，无降级需求 |
| 语音合成 | UART4 TW-TTS 模块 | — | 完全本地 |
| 语音识别 | 本地 ASR 接口骨架（未完成） | 云端 ASR 接口已调研 | 当前语音输入链路未完成，文档明确标注 |
| 网络状态 | 分级：链路 / IPv4 / DNS / TLS | — | 分级判断避免"有 IP 就认为在线"的误判 |
| 配置与密钥 | API Key 板级密钥加密存储于 `/data` | — | 不烧进镜像，不进 Git |

**与备选方案的对比**

| 维度 | 本方案 | 备选 A：只做应用层 | 备选 B：Linux + 容器 |
| --- | --- | --- | --- |
| 是否满足赛道要求 | 是（全新 SoC 移植） | 否（无适配增量） | 否（非 openvela 原生） |
| 启动时间 | 秒级 | 秒级 | 十秒级 |
| 内存占用 | 可控、可裁剪 | 依赖底层 | GB 级 |
| AI 能力 | NPU + 端侧 LLM + 云 Agent | 仅云 | 仅云 |
| 工作量大头 | BSP 移植（真实难点） | 无 | 无 |

#### 3.2.3 关键模块设计

**（1）网络模块**：`wlan0` 通过标准 WEXT/WAPI 接口暴露，私有 `/dev/a733-wifi`
降级为诊断与兼容用途。密码由 `wifi connect` 隐藏式读取并保存到板端，WEXT
查询不回显。扫描缓存由 16 扩展到 64，避免密集 2.4 GHz 环境挤掉 5 GHz 结果。

**（2）桌宠路由模块**：三层路由——快速规则 / 云端 Agent / 本地 Qwen。回复统一
解析为 `PetReply{text, emotion, actions}`，经白名单过滤后分发。任何表现层
（屏幕、喇叭、动作）失败都不得拖垮文字主链路。

**（3）显示模块**：复用 openvela/NuttX 官方 `drivers/lcd/st7735.c` 与官方 LVGL
NuttX LCD backend；板级代码只负责 SPI1、PL5 D/C、PB0 RESET 与 `/dev/lcd0`
注册。LVGL 只允许由单一 UI 线程调用，其余模块发送事件。

**（4）服务管理模块**：`a733services` 提供可选自启动；Wi-Fi 记忆上次连接并
自动重连；Agent 首次配置后开机自启。

**（5）构建与镜像模块**：`tools/build-a733.sh` 为唯一稳定入口，负责边界检查、
暂存 overlay、幂等应用补丁、校验关键配置、输出哈希，并在退出（含失败与中断）
时通过 trap 恢复官方工作区。镜像工具链含生成器 + 两个独立复核脚本，共三条
互相独立的校验路径。

**多设备 / 多节点角色说明**

| 平台 | 运行的系统 | 角色 |
| --- | --- | --- |
| Radxa Cubie A7Z | openvela (ARM64) | 主节点：AI 推理、网络、显示、语音 |
| Luckfox Lyra Zero-W | openvela (ARM32) | 轻量节点：外设采集与控制 |
| Luckfox Pico Mini | openvela (ARM32 + RV 协处理器) | 极简节点：低成本传感与执行 |

### 3.3 核心算法与技术原理

#### 3.3.1 AI 算法实现

**端侧模型（三档，按算力分层）**

| 模型 | 运行单元 | 框架 / 格式 | 量化 | 实测结果 |
| --- | --- | --- | --- | --- |
| LeNet | VIP2 NPU | 官方 A733 模型 → A7PM | 厂商 prepared | CRC32 `d50f5d79`，与 Linux VIPLite 一致 |
| YOLOv5s | VIP2 NPU | 同上 | 同上 | 三输出 CRC `53127e9c ddb5675e 6e01633e` 全一致 |
| YOLOv8n-PCQ | VIP2 NPU | 同上 | 同上 | 六输出 CRC 全一致；dog 输入有框、black 输入 0 框 |
| Qwen2.5-1.5B-Instruct | CPU（A55+A76） | 自移植 ggml 前向 | Q4_K_M（1.1 GiB） | 28 层前向 + KV cache，单 token 约 0.6 s |

**NPU 算力占用**：VIP2 CID `0x1000003b`，厂商标称 3 TOPS。openvela 侧实现了
电源/时钟/复位、IRQ、DMA 内存池（1 MiB pool + 24 MiB prepared 区）、MMU
页表和缓存维护。YOLOv8n 六输出在 Linux VIPLite 上报告 12,604 µs，
openvela 执行器结果与之逐字节一致。

**NPU 关键限制（诚实边界）**：通用专有 NBG 编译/链接仍依赖授权 Linux VIPLite
工具。openvela 当前实现的是**执行器**（能跑已 prepared 的 A7PM），不是编译器。
我们没有伪造一个开源 NBG 实现。

**端侧 LLM 的关键设计**：

- **模型常驻内存**：用约 1.1 GiB RAM 换掉每 token 重读 SD 卡。改进前单 token
  曾达约 473 秒，常驻后降到约 0.6 秒——**这是本项目投入产出比最高的一次优化**。
- **CPU 亲和性分工**：默认掩码 `fc`，CPU2..7 做计算（4×A55 + 2×A76），
  CPU0..1 留给系统、Wi-Fi 与 Agent，减少互相干扰。
- **仅设 affinity 不够**：早期只绑定核心收益有限（约 1.15 s），真正把矩阵
  运算拆成并行工作池后才降到约 0.6 s。

**云端模型（MiMo / DeepSeek）**

- 通过 openvela 官方 `packages_ai_agent` 的 LLM proxy 调用，不自行拼 HTTP。
- **接口**：OpenAI 兼容的 chat completions。
- **鉴权**：API Key 由 `aipet init` 隐藏输入，使用板级密钥加密后持久化到
  `/data/ai_agent/config/`，**不写入源码、不烧进镜像、不进 Git**。
- 实测：首次 TLS 握手约 7.49 s，连接复用后约 1.83 s。

#### 3.3.2 关键机制设计

**（1）决策 / 融合 / 调度：三层路由**

```text
输入 → [L1 快速规则]  命中(问候/时间/固定指令) → 立即回复，0 网络开销
         │未命中
         ├→ [L2 云端 Agent] 网络就绪 → DeepSeek/MiMo
         │                    │失败或断网
         └→ [L3 本地 Qwen] ←──┘  常驻进程，无缝接管
```

**（2）标签驱动硬件（结构化输出解析）**

LLM 回复中携带情绪/动作标签，经白名单解析后驱动表现层。规则：
`emotion == "普通"` 时不触发切换，避免流式输出中间句把表情刷回 idle
（这是原 Linux 原型踩过的坑，移植时保留修复）。

**（3）寄存器级排障算法（本项目最有价值的方法论）**

```text
现象：某外设寄存器读回全 0，或引脚电平不随写入变化
  ↓
① 建只读诊断节点，把相关寄存器一次性 dump 出来（不猜）
  ↓
② 对照官方 BSP 驱动源码（不是手册、不是博客）取权威布局
  ↓
③ 定位差异：基址？bank 步长？寄存器偏移？字段位移？
  ↓
④ 修复后反汇编验证修复**真的进入了二进制**
  ↓
⑤ 再上板
```

这一条在 ST7735 上抓出了两个层层嵌套的 bug（`pinctrl HW type` 用错、
R_PIO bank L 基址把 bank 序号乘进了偏移），在 UART4 上抓出了 CCU 基址错误。

**（4）幂等补丁应用（防静默失败）**

`patch` 的退出码无法区分"已应用"和"未应用"（两种情况都可能返回 0/1）。
实测结论：只能**按文本判定**——`Unreversed patch detected!  Ignoring -R.`
表示未应用，干净的 `checking file ...` 表示已应用。再配合
`require_contains` 断言，把"补丁没打上"从静默失败变成构建期硬失败。

#### 3.3.3 openvela 系统能力的深度运用

**落地的核心能力：图形 + AI + 多媒体（三项全覆盖）**

| 能力 | 用到的 openvela 组件 | 落地情况 |
| --- | --- | --- |
| **图形** | `drivers/lcd/st7735.c`、`lcd_dev`、`apps/graphics/lvgl`、`LV_USE_NUTTX_LCD` | `/dev/lcd0` 已注册；`display status/test` 可用；面板初始化序列已确证执行；颜色输出仍在实机收敛中 |
| **AI** | `packages_ai_agent`（消息总线、上下文、LLM proxy、TLS、Skills、工具注册）、`apps/system/aipet` | 联网对话、API Key 加密、自启动、连续中文对话 **实机通过** |
| **多媒体** | UART4 TTY、`/dev/ttyS4`、I2S0 诊断 | TW-TTS 语音播报 **实机通过**（`frames=4 failures=0`）；I2S PCM lower-half 未完成 |

**其余深度使用的 openvela / NuttX 能力**：

- **网络**：`apps/wireless/wapi`、WEXT ioctl、netdev、lwIP、DHCP/DNS/ARP
- **文件系统**：VFS、FAT（UTF-8 长文件名）、GPT 分区解析、procfs
- **系统**：SMP 调度、MMU、GICv3、虚拟定时器、信号、watchdog、uORB 传感器
- **安全**：mbedtls（TLS/SSH）、板载 TRNG 提供 `/dev/random`
- **服务**：NSH、SSH/SCP、FTP、curl/wget、NTP、iperf

**对 openvela 的优化 / 拓展与改进建议**

我们向公共仓提交的兼容性改动（`patches/`，可独立拆分为上游 PR）：

| 补丁 | 内容 | 建议 |
| --- | --- | --- |
| `nuttx-st7735-werror.patch` | ST7735 驱动的 `CONFIG_LCD_NOGETRUN` 保护、`%zu` 格式修正、批量 `SPI_SNDBLOCK` 填充、完整面板初始化序列（FRMCTR/PWCTR/VMCTR/gamma/COLMOD） | **上游**：NuttX ST7735 驱动缺少面板上电初始化序列，且 `st7735_bpp()` 只在 bpp 变化时才发 COLMOD，导致颜色深度依赖缓存值。建议上游无条件发送 COLMOD |
| `lvgl-nuttx-lcd-release-free.patch` | `lv_nuttx_lcd.c` 的 `display_release_cb()` 不再 `lv_free(disp->buf_1/buf_2)` | **上游 Bug**：`lv_display_set_buffers()` 会把 `buf_1/buf_2` 指向 `lv_display_t` **内嵌**的 `_static_buf1/_static_buf2`，对其调用 `free()` 是释放内部指针，会破坏堆（实测数据异常于 `mm_forcefree`）。建议上游修复 |
| `ai-agent-a733-pet-and-http.patch` | Agent 桌宠通道 + HTTP chunked 收尾 | **上游**：chunked 传输缺少收尾处理 |
| `ai-agent-single-instance.patch` | Agent 单实例保护 | 可选上游 |
| `ai-agent-provisioning.patch` | API Key 板级加密配置 | 可选上游 |
| `readline-utf8-backspace.patch` | readline UTF-8 退格按字符而非字节删除 | **上游**：中文输入下退格会删半个字符 |
| `nuttx-arm64-a733-aff1-cpuid.patch` | ARM64 CPUID 亲和性 | **上游**：ARM64 缺少 CPUID 实现 |

**改进建议汇总**：① NuttX ST7735 面板初始化序列应进入驱动主线；② LVGL NuttX
释放路径的内部指针 `free()` 是真实缺陷；③ readline 的 UTF-8 退格应默认按字符；
④ 建议 openvela 提供统一的 "SoC 移植检查清单" 模板，把 CCU/pinctrl 这类
"错了一点都不报错"的坑变成可勾选项，能显著降低新芯片适配的隐性成本。

### 3.4 系统实现

#### 3.4.1 软件 / 固件架构

```text
contest2026_274_Dogking/
├── board/
│   ├── a733-cubie-a7z/openvela-overlay/     # A733 板级与芯片源码
│   │   ├── vendor/allwinnertech/chips/a733/      # CCU/R_PIO/SPI1/UART4/NPU/Wi-Fi
│   │   ├── vendor/allwinnertech/boards/a733/     # Cubie A7Z 板级初始化
│   │   ├── apps/system/a733*                     # 板级用户命令
│   │   └── apps/system/aipet{,llm,asr}           # AI 桌宠产品层
│   └── rockchip/                            # RK3506 / RV1103 移植
│       ├── chips/rk3506/  chips/rv1103/
│       └── boards/rk3506/luckfox-lyra-zero-w/
│           boards/rv1103/luckfox-pico-mini/
├── docs/                                    # 状态、证据、阶段记录
│   ├── a733/            # 阶段文档 + version-archive/（历史版本记录）
│   └── rockchip/        # 两颗 Rockchip SoC 的移植说明
├── patches/                                 # 公共仓可审查兼容补丁
├── skills/                                  # 自定义 Skill（3 个）
├── logs/hxy299/                             # AI Coding 日志（107 会话）
├── tools/                                   # 构建 / 镜像 / 校验工具链
└── contest2026_274_Dogking.xml              # repo manifest 与 linkfile 映射
```

**关键工程约束**：源码只在比赛仓维护。板级/应用代码通过 `openvela-overlay/`
进入，公共仓必要修改进入 `patches/`；构建脚本负责临时覆盖、应用补丁，并通过
trap 在退出时恢复官方工作区。这样既能提交可复现的完整工程，又能精确说明
哪些修改属于板级、哪些应当上游。

#### 3.4.2 数据流与关键流程

**启动流程**

```text
上电 → BootROM(Boot0) → SCP → BL31(ATF) → U-Boot
   │
   ├─ U-Boot 读 GPT 分区 3 (ext4)
   ├─ 读 /extlinux/extlinux.conf
   ├─ 加载 /extlinux/nuttx.bin + sun60i-a733-cubie-a7z.dtb
   └─ booti <kernel> - <dtb>     ← 必须用 booti，不能用 go
         │
         ↓
   openvela ARM64 内核 @ EL1
         │
         ├─ MMU / GICv3 / 虚拟定时器初始化
         ├─ 8 核 SMP 分阶段启动（2 → 6 → 8）
         ├─ 分区堆 4 GiB + procfs
         ├─ 板级外设：UART0/SDMMC0/GPT/FAT/I2C/SPI/PWM/WDT/THS/TRNG
         ├─ FCU760K Wi-Fi 固件交接 → wlan0
         ├─ VIP2 NPU 检查点 → /dev/npu0
         └─ NSH
```

**镜像生成与校验流程（三条独立路径）**

```text
nuttx.bin (含 Linux ARM64 Image header, text_offset=0x200000)
   │
   ├─ ① tools/make_flashable_image.py --all
   │      check → copy → apply → minimal → verify
   │      产物：512 MiB 可烧写镜像 + SHA-256
   │
   ├─ ② tools/reverify_image.py
   │      按 NuttX 规则**独立重读**：GPT 不重叠 + FAT16 + LFN 逆序/校验和
   │
   └─ ③ tools/verify-minimal.sh
          公式与地址**全部重写**，不引用生成脚本（防自证循环）
          校验：启动链、GPT CRC、/data 固件、ext4 内核回读、重定位地址
```

#### 3.4.3 硬件设计与适配

**是否完成全新硬件平台适配或驱动开发：是。三颗 SoC，全部为全新移植。**

**① 主平台：Radxa Cubie A7Z（Allwinner A733）**

| 项目 | 规格 |
| --- | --- |
| SoC | Allwinner A733（sun60iw2p1） |
| CPU | 2× Cortex-A76 + 6× Cortex-A55，8 核 SMP |
| 内存 | 4 GiB LPDDR4/4X |
| NPU | VIP2，CID `0x1000003b`，约 3 TOPS |
| 存储 | microSD（GPT：firmware / EFI / openvela / data） |
| Wi-Fi/BT | FCU760K / AIC8800D80-U02，专用 USB1 高速链路 |
| 音频 | UART4 TW-TTS（9600 8N1，PJ24/PJ25，function 4） |

**新开发的自定义驱动（芯片层）**

| 驱动 | 文件 | 难点与解决方案 |
| --- | --- | --- |
| CCU 时钟控制 | `a733_header_peripherals.c` | **难点**：全志主 CCU 有多套候选基址，用错则寄存器全 0 且不报错。**解决**：以官方 `sun60iw2p1.dtsi` 为准取 `0x02002000`，并在 UART4 上以"读回 `bgr=00010001`"验证 |
| PIO / R_PIO pinctrl | 同上 | **难点**：全志 pinctrl 存在多套 HW type，主 PIO 是 hw_type 4（首 bank `+0x80`、步长 `0x80`、drive `+0x20`、pull `+0x30`），R_PIO 是 hw_type 1（bank L 为原点、步长 `0x30`、mux `+0x00`、data `+0x10`、dlevel `+0x14`、pull `+0x24`）。**用错布局时寄存器"可写但无效"，完全静默**。**解决**：逐个对照官方 BSP `pinctrl-sunxi.c` 的 `hw_info` 表与 `pinctrl-sun60iw2-r.c` 的 `bank_base` 推导；并注意 **PL5 的 mux 在 `+0x04`（pin 5 落在第二个 mux 字），不是 `+0x00`** |
| SPI1（ST7735 显示） | 同上 | 实现 16-bit RGB565 传输、`SPI_CMDDATA` 接口、批量 `SPI_SNDBLOCK`、轮询 FIFO 分块；PL5 作 D/C，PB0 作 RESET |
| UART4（TTS） | `a733_uart4.c` | **难点 1**：termios 混用 `B9600`（`c_cflag` 编码值）与数值波特率 9600，导致 `TCSETS` 稳定 `EINVAL`。**难点 2**：CCU 基址与 pinctrl 布局错误。**解决**：分别保存 `c_cflag` 编码与 `c_speed` 数值；CCU 取 `0x02002000`，pinctrl 用 hw_type 4 |
| SDMMC | `a733_sdmmc.c` | PIO 模式，400 kHz 初始化 / 12 MHz 传输 / 4-bit 总线；修复多块传输 |
| NPU VIP2 | `a733_npu.c` / `a733_npu_runtime.c` / `a733_npu_memory.c` | 电源/时钟/复位、IRQ、DMA 内存池、MMU 页表、缓存维护、原子任务 ABI（`A733_NPUIOC_RUN_A7PM`） |
| Wi-Fi AIC8800D80 | `a733_wifi_usb.c` | **难点**：BootROM → 固件上传 → 重新枚举（`a69c:8d80` → `a69c:8d81`）→ LMAC/RF/ME/法规域 → station VIF 多阶段交接。**解决**：逐阶段检查点 + 超时；修复 WPA2 M4 发送时序（先发 M4 再安装 PTK/GTK） |
| TRNG | `a733_trng.c` | 为 mbedtls 提供熵源 |

**② 第二平台：Luckfox Lyra Zero-W（Rockchip RK3506）**

见 `docs/rockchip/ROCKCHIP_RK3506_LYRA_ZERO.md`。已移植 GPIO / I2C / SPI /
SDMMC / Timer / IRQ / MemoryMap 等芯片层驱动与板级支持。

**③ 第三平台：Luckfox Pico Mini（Rockchip RV1103）**

见 `docs/rockchip/ROCKCHIP_RV1103_PICO_MINI.md`。已移植 GPIO / I2C / SPI /
DMA / SFC-NAND（SPI NAND 启动）/ SARADC / PWM / TRNG / USB / USB-CDC / TSADC /
WDT 等，并提供 SPI NAND 启动打包工具。

**关键 BOM / 接线（主平台桌宠形态）**

| 模块 | 型号 | 接口 | 备注 |
| --- | --- | --- | --- |
| 开发板 | Radxa Cubie A7Z | — | 4 GiB，无 UFS |
| 显示屏 | 1.8" ST7735 128×160 RGB-TFT | SPI1 | 3.3 V |
| 语音合成 | TW-TTS 模块 | UART4 | 9600 8N1 |
| 存储 | microSD ≥ 8 GB | SDMMC0 | GPT 四分区 |

**ST7735 接线表（已实机确认）**

| 信号 | A733 引脚 | 物理 Pin | 备注 |
| --- | --- | --- | --- |
| SCK | PD11 / SPI1_CLK | 23 | function 6 |
| MOSI | PD12 / SPI1_MOSI | 19 | function 6 |
| CS | PD10 / SPI1_CS0 | 24 | function 6 |
| **DC** | **PL5 / R_PIO** | **22** | Linux 侧为 GPIO357（gpiochip1:5） |
| RESET | PB0 | 7 | 主 PIO GPIO |
| VCC / BL | 3.3 V | 1 / 17 | — |
| GND | GND | 任一 | — |

**适配难点与解决方案（ST7735 完整排障记录）**

这是本项目最能体现"寄存器级排障"价值的一条链，共 4 层问题，逐层剥离：

| 层 | 现象 | 根因 | 解决方案 |
| --- | --- | --- | --- |
| 1 | 屏幕全白，D/C 恒 0.49 V | 主 PIO pinctrl 用了旧版布局（`bank*0x30`），PD10..13 从未复用为 SPI1 | 改用 hw_type 4（首 bank `+0x80`、步长 `0x80`）；实测 `cfg1=ff6666ff` |
| 2 | 屏幕仍全白，D/C = 2.13 V | R_PIO bank L 基址把 bank 序号乘进了偏移（`11 × 0x30 = 0x210`）；且 **PL5 的 mux 在 `+0x04` 而非 `+0x00`**，实际改的是 PL0..PL3，PL5 一直是输入态 | 按官方 BSP 取 bank L = `+0x000`、步长 `0x30`、mux `+0x04`；实测 `mux1` bit20..23=1、`data` bit5=1，D/C 到 3.3 V |
| 3 | 屏幕亮但全是竖条纹、颜色不对 | 面板初始化序列（FRMCTR/PWCTR/VMCTR/gamma）**根本没编进内核**——补丁的 10 个 hunk 全被 `patch` 忽略，但退出码非致命，构建"成功" | 修 `apply_patch()` 按文本判定状态 + `require_contains` 构建断言；用 `difflib` 从 HEAD 重新生成补丁 |
| 4 | 初始化已确证执行（`st7735_lcdinitialize` 耗时 30 ms → 537 ms），条纹仍在 | 颜色格式可能被继承：ST7735 上电默认 **18-bit**，而 `st7735_bpp()` 仅在 `dev->bpp` 变化时才发 COLMOD | 在初始化序列中**无条件**发送 `COLMOD = (ST7735_BPP >> 2) \| 1`（16bpp → `0x05`）；并新增 `display fill` 纯色实测命令做最终判定 |

**当前状态**：第 4 层仍在实机收敛中。已确证 D/C 正常、SPI 数据通路正常、
面板初始化序列已执行（反汇编逐条核对 12 条命令立即数 + 380 ms 延时）。
**我们没有把"条纹消失"写成已完成**，因为还没有拿到纯色实测的确认。

#### 3.4.4 应用 / 交互端设计

**UI 设计**：1.8" 128×160 屏，LVGL 渲染。状态机涵盖
idle / listening / thinking / speaking / error 五种表情。设计原则：
① LVGL 只由单一 UI 线程调用；② 表情在收到明确切换指令前保持不变；
③ 先做局部刷新，性能优化不与功能修复混在同一次提交。

**交互端**：

```text
nsh> aipet init            # 首次：选后端、选模型、隐藏输入 API Key
nsh> aipet status          # 查看链路状态
nsh> aipet ask "..."       # 云端对话
nsh> aipet local ask "..." # 强制本地 Qwen
nsh> aipet tts test "..."  # UART4 语音播报
nsh> display status        # 显示链路状态
nsh> display test 30       # 显示测试场景
nsh> display fill ff0000   # 纯色实测（绕过 LVGL）
nsh> npu info / selftest / yolo
nsh> wifi scan / connect
```

#### 3.4.5 自定义 Skill（硬性要求：至少 1 个；本作品沉淀 3 个）

| Skill | 路径 | 解决的问题 | 触发场景 |
| --- | --- | --- | --- |
| `embedded-soc-porting` | `skills/embedded-soc-porting/` | 新 SoC 冷启动缺方法论：从 BootROM 到 NSH 的分阶段检查点、寄存器布局对照、证据边界 | 开始任意新芯片/新开发板的 openvela 移植时 |
| `embedded-npu-porting` | `skills/embedded-npu-porting/` | NPU 工具链闭源、算子不匹配、精度对不上：黄金 trace → prepared 包 → 运行时执行的完整路径与失败模式 | 在 openvela 上接入任意 NPU 并验证数值一致性时 |
| `make-flashable-image` | `skills/make-flashable-image/` | 镜像打包反复踩坑：GPT 重叠、VFAT 长名逆序、text_offset 公式、三条独立校验 | 每次需要产出可烧写镜像时 |

每个 Skill 含 `SKILL.md` + `references/` 深度参考（含 `a733-lessons.md`、
`vip2-lessons.md`、`failure-patterns.md`、`golden-validation.md` 等），
把项目踩过的坑固化成可复用资产。

**运行时 Skill（AI 硬件产品创新赛道要求）**

板端运行时 Skill 目录：`/data/ai_agent/skills/`

- **定义格式**：openvela 官方 `packages_ai_agent` 的 Skill 机制，由
  `skill_loader.c` 在启动时扫描该目录并注册到工具注册表。
- **触发场景**：用户请求匹配 Skill 描述时，Agent 通过工具调用机制自动触发。
  当前已落地的运行时能力包括设备状态查询（`/dev/a733-hw`、`/dev/npu0`、
  `/dev/a733-lcd` 诊断节点）与桌宠表现控制。
- **注意**：`skills/`（仓内，开发期 AI 工具用）与 `/data/ai_agent/skills/`
  （板端运行时，Agent 用）是**两套不同机制**，不要混淆。

### 3.5 系统测试与结果分析

#### 3.5.1 测试环境

| 项目 | 配置 |
| --- | --- |
| 主开发板 | Radxa Cubie A7Z，Allwinner A733，4 GiB，无 UFS |
| 启动介质 | microSD（GPT 四分区），U-Boot `booti` |
| 串口 | UART0，115200 8N1，无硬件流控 |
| 构建主机 | Windows 11 + WSL2 Ubuntu；工具链 AArch64 bare-metal + CMake/Ninja |
| 网络 | 2.4 GHz / 5 GHz 双频 AP，WPA2-PSK/CCMP |
| AI 工具 | Codex CLI（107 会话，见 `logs/hxy299/`） |

#### 3.5.2 功能测试

| # | 测试项 | 命令 | 实际结果 | 判定 |
| --- | --- | --- | --- | --- |
| F1 | 系统启动 | 上电 | Boot0→SCP→BL31→U-Boot `booti`→EL1→`NuttShell (NSH)` | ✅ 通过 |
| F2 | 8 核 SMP | `cpucheck` | CPU0..5 = Cortex-A55，CPU6..7 = Cortex-A76，亲和掩码覆盖 8 核 | ✅ 通过 |
| F3 | 内存 | `free` | 约 4.27 GB 托管内存，procfs 已挂载 | ✅ 通过 |
| F4 | SD 多块读 | `dd if=/dev/mmcsd0 of=/dev/null bs=65536 count=16` | 无 I/O 错误 | ✅ 通过 |
| F5 | GPT + FAT | `ls /data` | `/dev/a7z-data` 读写挂载，UTF-8 长文件名正常 | ✅ 通过 |
| F6 | 温度传感器 | `sensortest -n 3 temp0` | `value:34.47` ×3 | ✅ 通过 |
| F7 | Wi-Fi 扫描 | `wifi scan` | 2.4 GHz 与 5745 MHz 5 GHz BSS 均扫到 | ✅ 通过 |
| F8 | Wi-Fi 关联 | `wifi connect <SSID>` | WPA2 关联成功，DHCP 获得 `192.168.31.27/24` | ✅ 通过 |
| F9 | 网络服务 | `ping` / `nslookup` / `curl` / SSH / FTP | 网关、公网 IPv4、DNS、TCP、UDP、wget/curl、SSH、FTP 全通过 | ✅ 通过 |
| F10 | NPU LeNet | `echo prepared=... > /dev/npu0` | CRC32 `d50f5d79`，与 Linux VIPLite 一致 | ✅ 通过 |
| F11 | NPU YOLOv5s | 同上 | 三输出 CRC `53127e9c ddb5675e 6e01633e` 全一致 | ✅ 通过 |
| F12 | NPU YOLOv8n | 同上 + 动态输入 | 六输出 CRC 全一致；dog 有框 / black 0 框 | ✅ 通过 |
| F13 | 官方 Agent | `aipet ask "..."` | 连续中文对话成功 | ✅ 通过 |
| F14 | 本地 Qwen | `aipet local ask "..."` | 中文流式输出、有限多轮记忆 | ✅ 通过 |
| F15 | UART4 TTS | `aipet tts test "..."` | `frames=4 failures=0 last=0 stage=complete` | ✅ 通过 |
| F16 | 显示链路 | `display status` | `/dev/lcd0` 注册成功，`st7735_lcdinitialize` 耗时 537 ms | ✅ 部分通过 |
| F17 | 显示颜色 | `display test 30` | 初始化序列确证执行；**仍为竖条纹，颜色未收敛** | ⚠️ 未通过 |
| F18 | UVC 摄像头 | `echo probe > /dev/a733-uvc` | xHCI 能力寄存器可读，设备未枚举 | ❌ 未完成 |
| F19 | I2S 音频 | `cat /dev/a733-audio` | 仅非破坏诊断，无 PCM 数据流 | ❌ 未完成 |

#### 3.5.3 性能测试（实测数据）

| 指标 | 实测值 | 条件 |
| --- | --- | --- |
| 网络吞吐 TCP | **4.13 Mbit/s** | iperf，30.25 s 传输 14.9 MiB |
| 网络吞吐 UDP | **10.8 Mbit/s**，丢包 5.7% | iperf，30.15 s 传输 38.9 MiB |
| 云端 Agent 首次请求 | **约 7,490 ms** | 含 TLS 首次握手 |
| 云端 Agent 复用请求 | **约 1,829 ms** | 连接复用 |
| 本地 Qwen 单 token 前向 | **约 0.615 s** | Q4_K_M，模型常驻，CPU2..7 六核工作池 |
| 本地 Qwen 首次加载 | 约 175 s | 1.1 GiB 从 SD 卡 PIO 读取（仅首次） |
| NPU YOLOv8n 六输出推理 | 12,604 µs | Linux VIPLite 参考值，openvela 结果一致 |
| NPU LeNet 推理 | 164–178 µs | Linux VIPLite 参考值 |
| 显示面板初始化 | 30 ms → **537 ms** | 加入完整初始化序列后；含 380 ms 稳定延时 |
| 内核体积 | 2,195,296 bytes | 最新构建（含显示实测命令） |
| 可烧写镜像 | 536,887,808 bytes (512 MiB) | 最小镜像格式 |

#### 3.5.4 可靠性与稳定性测试

| 项目 | 结果 |
| --- | --- |
| Wi-Fi 长稳 | v71 基线长稳通过；断线状态清理（异步 disconnect 同步清 carrier/密钥/WPA 状态）已验证 |
| 连续运行 | 多轮板测会话连续运行；8 核 SMP 下 Wi-Fi / ping / NPU / NSH 持续可用 |
| 崩溃恢复 | ST7735 应用层曾出现数据异常（`mm_forcefree`），定位为 LVGL 释放内部指针，已修复并实机确认 |
| 启动永久卡死防护 | 所有轮询均带超时（SPI FIFO、I2C、UART）；显示面板初始化已移出开机关键路径（懒加载） |
| 失败隔离 | 云端 Agent 失败自动切本地 Qwen；表现层（屏幕/喇叭/动作）失败不拖垮文字主链路 |
| 误报 / 漏报兜底 | 视觉侧以 dog 输入有框、black 输入 0 框双向验证，排除"固定输出回放"的假阳性 |
| 已知不稳项 | 8 核启动早期多核串口字符交错（进入 NSH 后正常）；长生成可能过热降频，取消路径已实现协作式 stop |

### 3.6 AI-Native 开发说明

| 指标 | 数据 |
| --- | --- |
| **AI Coding 代码占比** | **约 95%**（口径：仓内 `board/`、`patches/`、`tools/`、`docs/` 的新增与修改行中，由 AI 生成或大幅改写后再人工审校的比例。人负责：需求定义、硬件接线与实测、寄存器结论确认、真机命令执行与结果判定、最终技术决策） |
| **使用的 AI 工具** | **Codex CLI**（官方支持的 4 种采集工具之一），会话日志完整提交于 `logs/hxy299/` |
| **MCP 工具使用情况** | 未使用 MCP Server（本项目为裸机 SoC 移植，主要依赖 shell / 文件编辑 / 构建工具，未接入 VelaJS MCP 或 Figma MCP） |
| **Skills 使用与新增情况** | **使用**：官方 `contest-log-collector`（日志归集与校验）。**新增沉淀**：3 个自定义 Skill —— `embedded-soc-porting`、`embedded-npu-porting`、`make-flashable-image`，每个含 SKILL.md + 深度 references |
| **Token 使用总量** | 会话累计计数器峰值：**输入 1,551,131,164**、**输出 3,271,685**。口径说明：输入为 Codex 会话累计计数值（含每轮重发的上下文），非唯一 token 数；输出为该会话累计生成 token |
| AI 会话规模 | 107 个会话文件 / 28 个自然日 / 23,933 条事件（工具调用 19,938、助手消息 2,715、用户消息 1,280） |
| 使用模型 | `gpt-5.6-sol`（主）、`codex-auto-review`、`gpt-reserve`、`glm-5.3-flash` |
| 时间跨度 | 2026-07-27 → 2026-09-14 |

#### AI 工具对开发效率的提升

**最显著的三个方面**：

1. **寄存器级排障的收敛速度**。A733 的 CCU 与 pinctrl 布局问题，人工对照手册
   可能需要数天；AI 直接读官方 BSP 驱动源码（`pinctrl-sunxi.c` 的 `hw_info`
   表、`pinctrl-sun60iw2-r.c` 的 `bank_base`）并推导出正确布局，把这一环压缩
   到小时级。
2. **反汇编验证的自动化**。判断"修复是否真的进了二进制"需要 objdump 分析，
   AI 能直接给出"`0x5220` 模式出现 0 次 / `movk #0x702` 命中 17 处"这种
   可证伪的结论，避免"改了源码但没生效"这类最耗时的问题。
3. **文档与状态维护**。项目累计产生 100+ 篇阶段文档，AI 负责把每次板测结果
   结构化归档，保持了"源码 / 构建 / 镜像 / 真机"四级证据边界的一致性。

#### 遇到的问题与解决方式

| 问题 | 解决方式 |
| --- | --- |
| **AI 容易把"编译通过"当成"适配完成"** | 建立强制证据分级：文档中必须区分源码正确 / 构建通过 / 镜像可校验 / 真机可运行。所有硬件结论必须附串口日志原文 |
| **AI 一次改多个寄存器导致无法归因** | 强制"一次只加一个硬件检查点"，且所有轮询必须带超时，防止启动永久卡死 |
| **补丁静默失败，产出功能缺失但校验全过的镜像** | 这是本项目最深刻的一次教训。根因是 `patch` 的退出码无法区分"已应用/未应用"。解决方案：① 按文本判定补丁状态；② 加 `require_contains` 构建断言，把静默失败变成硬失败；③ 用 `difflib` 从 HEAD 重新生成补丁，消除手写 hunk 计数错误 |
| **AI 生成的反向补丁会"复制"而非"删除"代码** | `patch -R` 在 hunk 计数错误时会静默复制行（把 `config_store.c` 改成重复标签 + 未声明变量）。解决：反向应用前必须先 dry-run，且只允许在"确证可干净回退"时执行 |
| **AI 无法替代真机** | 明确分工：AI 负责推导、实现、分析；**人负责硬件接线、实测命令执行与结果判定**。所有硬件结论均由人上板确认 |

### 3.7 总结与展望

#### 成果总结

1. **三颗 SoC 的 openvela 原生移植**：Allwinner A733（ARM64 8 核）、
   Rockchip RK3506、Rockchip RV1103，全部为全新适配，非配置预设。
2. **主平台完成从 BootROM 到 AI 应用的全栈打通**：8 核 SMP、4 GiB 内存、
   SD/GPT/FAT、双频 Wi-Fi + 完整网络服务、NPU 真实推理、官方 AI Agent、
   本地 LLM、UART TTS 均已实机验证。
3. **可复现的工程化交付**：一条命令构建、三条独立镜像校验、幂等补丁应用、
   trap 恢复官方工作区、2 处内核回读逐字节比对。
4. **方法论沉淀**：3 个自定义 Skill + 四级证据边界 + 寄存器级排障范式，
   可直接迁移到下一条新芯片产线。
5. **诚实的状态边界**：明确列出 4 项未完成（UVC、I2S PCM、BT/GPU/UFS、
   显示颜色收敛），不以检查点冒充完整功能。

#### 应用前景与商业价值

**目标受众**

| 维度 | 画像 |
| --- | --- |
| 人群 | ① 嵌入式 / 机器人开发者（需要可裁剪的国产 SoC RTOS 方案）；② AI 硬件产品团队（需要离线可用的语音+视觉交互终端）；③ 教育科研（需要可完整审计的端侧 AI 教学平台） |
| 年龄 | 22–45 岁为主 |
| 地域 | 国内为主（国产 SoC 供应链与信创需求），海外开源硬件社区为辅 |
| 使用偏好 | 重视可复现性、文档完整度、上游合规，反感"只有 Demo 没有代码" |

**商业价值**

1. **对 SoC 原厂 / 板卡厂商**：一套已验证的 openvela BSP 模板。新板卡接入
   RTOS 生态的周期可从数月压缩到数周——本项目最难的部分（CCU、pinctrl、
   SMP、Wi-Fi 固件交接）已经沉淀为 Skill 与可对照的实现。
2. **对 AI 硬件产品团队**：一个断网可用的端侧 AI 参考设计。云端 Agent +
   本地 LLM 双层路由，解决了"云一断产品就废"的核心商业风险。
3. **规模化潜力**：三颗芯片分属三个不同定位（高性能 A733 / 低成本 RK3506 /
   极简 RV1103），覆盖从高端到入门的产品线。同一套方法论可复制到更多国产
   SoC，边际成本递减。

**商业模式设想**：① BSP 适配服务与技术支持；② 端侧 AI 参考设计与定制开发；
③ 向上游贡献驱动、建立技术影响力后转化为生态合作。

#### 不足与未来工作

**已知不足（诚实列出）**

1. **显示颜色尚未收敛**：面板初始化序列已确证执行（反汇编逐条核对），
   D/C 与 SPI 通路均已验证，但画面仍为竖条纹。已准备 `display fill` 纯色
   实测命令用于最终定位字节序 / 颜色格式。
2. **语音输入未打通**：本地 ASR 只有接口骨架与 mock；云端 ASR 接口已调研。
   缺少麦克风 PCM 输入（I2S lower-half 或 UVC/UAC 未完成）。**因此本作品
   未启用语音唤醒功能，也不涉及大赛规定的唤醒词。**
3. **摄像头链路未完成**：UVC 枚举、PROBE/COMMIT、帧采集均未通过；
   NPU 侧的执行器已就绪，瓶颈在采集与预处理。
4. **NPU 工具链边界**：通用 NBG 编译仍依赖授权 Linux VIPLite 工具，openvela
   侧是执行器而非编译器。
5. **本地 LLM 优化空间**：独立参考精度对照、跨请求持久 KV cache、采样策略
   （temperature/top-k/top-p）、长稳热管理均待完成。
6. **Bluetooth HCI / GPU / UFS / MIPI CSI 未适配**。
7. **上游合入未完成**：7 个公共仓补丁仍以 `patches/` 形式存在，需按官方流程
   拆分并提交 `dev-ai-contest-2026` PR。

**未来工作（按优先级）**

```text
P0  完成显示颜色收敛（纯色实测 → 字节序/格式定位 → 出图）
P0  把桌宠事件接到 LVGL（只改产品层，不动 SPI lower-half）
P1  语音输入二选一打通：I2S 标准 audio lower-half，或重建标准 xHCI platform
P1  UVC 帧采集 → RGB640 预处理 → 复用已有 YOLOv8 动态输入
P2  本地 LLM：参考精度对照、持久 KV、采样策略、热管理
P2  公共仓补丁拆分并提交上游 PR
P3  BT/GPU/UFS、Wi-Fi 长稳与 DFS 压测、生产级 DVFS 与掉电一致性
```

---

## 附录 A：评审维度自检对照

| 评审维度（分值） | 本报告对应章节 | 关键支撑材料 |
| --- | --- | --- |
| 技术难度（30） | 3.2 系统方案设计、3.3 核心算法与技术原理、3.4 系统实现、3.5 系统测试 | 三颗 SoC 全栈移植；寄存器级排障记录；8 核 SMP、Wi-Fi 固件交接、NPU ABI |
| 产品创新性（20） | 2 摘要、3.1 绪论（创新点） | 断网降级路由；标签驱动硬件；四级证据边界方法论 |
| 项目完整度（20） | 3.5 系统测试与结果分析；项目源码；作品展示照片 | 19 项功能测试（15 项通过）；107 个 AI 会话日志；构建/镜像三路校验 |
| AI 开发（10） | 3.3 核心算法（AI 算法实现）、3.6 AI-Native 开发说明 | 端侧 Qwen + NPU YOLOv8 + 云端 Agent；3 个自定义 Skill；23,933 条日志事件 |
| 商业潜力（10） | 3.7 总结与展望（应用前景与商业价值） | 三类目标受众；BSP 模板化复用；三档芯片覆盖产品线 |
| 展示效果（10） | 演示视频；海报；答辩 PPT | 见提交材料包 |

## 附录 B：提交材料清单对照

| 序号 | 材料 | 状态 | 位置 / 说明 |
| --- | --- | --- | --- |
| 1 | 技术报告 | ✅ 已生成 | `docs/SUBMISSION_TECHNICAL_REPORT.docx`（源：同名 `.md`） |
| 2 | 演示视频（≤5 分钟） | ⬜ 待录制 | 需含功能演示、交互操作、AI 能力展示 |
| 3 | 作品展示照片 | ⬜ 待拍摄 | 建议前/后/侧/俯四个角度 + 屏幕显示特写 |
| 4 | 海报 | ⬜ 可选 | 入围决赛 / 线下展示时需提交 |
| 5 | 答辩 PPT | ⬜ 可选 | 入围决赛时提交 |
| — | 项目源码 | ✅ 已入仓 | `contest2026_274_Dogking` 仓（554 个 tracked 文件） |
| — | AI Coding 日志 | ✅ 已入仓 | `logs/hxy299/`（107 会话 / 23,933 事件，官方校验器 ALL OK） |

## 附录 C：关键实测数据速查

```text
NPU CID             1000003b
LeNet CRC           d50f5d79
YOLOv5s CRC         53127e9c ddb5675e 6e01633e
YOLOv8n CRC         55d0becd ead06611 ba209c57 2637657c 9e7f743a 2eb4d5ce
Wi-Fi TCP           4.13 Mbit/s (30.25 s / 14.9 MiB)
Wi-Fi UDP           10.8 Mbit/s, loss 5.7% (30.15 s / 38.9 MiB)
Agent 首次 TLS      约 7490 ms
Agent 复用          约 1829 ms
Qwen 单 token 前向  约 0.615 s
Qwen 首次加载       约 175 s (1.1 GiB, SD PIO)
UART4 正常诊断      bgr=00010001 PJ24=TX/f4 PJ25=RX/f4
                    lcr=00000003 lsr=00000060 thre=1 usr=00000006 tfnf=1
UART4 验收          frames=4 failures=0 last=0 stage=complete
显示初始化耗时      30 ms -> 537 ms（加入面板初始化序列后）
内核 SHA-256        495e84f9133e87e3cee2099b4b57cb80b52ea35a467047d213bf64b6a8d9563a
镜像 SHA-256        6cc5e6023f0e81afee91d222fa5442d79ec613101d4ea72b4347fb2b1e6a77fb
镜像大小            536887808 bytes (512 MiB)
```
