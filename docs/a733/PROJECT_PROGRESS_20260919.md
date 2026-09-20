# A733 openvela 项目进度快照（2026-09-19）

本文只记录已经取得的结果、证据边界和当前工作区状态。编译通过、镜像打包通过和真机通过分别标注，不相互替代。

## 当前决策

- USB 麦克风、USB2/xHCI2 和 UAC 工作自 2026-09-19 起暂停，不再继续生成试错固件。
- USB v113 是最后一个收到实机日志的诊断版本；v114 已构建但没有实机验证，不作为恢复基线。
- 当前优先保留已经成立的系统、网络、NPU、本地 LLM 和在线 Agent 成果。

## 总体状态

| 模块 | 当前状态 | 已取得的最强证据 | 主要剩余工作 |
| --- | --- | --- | --- |
| ARM64/openvela 启动 | 真机通过 | Boot0/SCP/BL31/U-Boot `booti` 进入 EL1 和 NSH | 生产级启动与异常恢复加固 |
| CPU/SMP | 8 核基本真机通过 | 6×A55、2×A76 身份和亲和性验证；空闲栈页对齐问题已修复 | 长稳、DVFS、热管理和压力测试 |
| 内存与系统服务 | 真机通过 | 4 GiB 分区堆、procfs、信号、Ctrl+C、reboot/poweroff | 生产级资源与异常压力测试 |
| microSD/GPT/FAT | 真机通过 | SDMMC0 多块读取、GPT 节点、`/data` FAT 读写 | 性能和掉电一致性测试 |
| 基础外设 | 真机通过 | UART0、LED、WDT、THS、I2C2/I2C7、SPI1、风扇 PWM | 按产品需求继续接执行器和显示 |
| FCU760K Wi-Fi | v71 真机稳定基线 | 5 GHz WPA2/DHCP、180 秒空闲后 20/20 网关 ping、再次扫描 29 个 BSS | 更长期压力、漫游/断线、DFS 和吞吐优化 |
| 网络服务 | 真机通过 | DNS、NTP、curl/wget、HTTPS、SSH/SCP、FTP、iperf | 权限、安全和长期并发测试 |
| VIP2 NPU | 真机基线通过 | LeNet、YOLOv5s、YOLOv8n PCQ 与 Linux golden CRC 一致；动态 dog/black 输入行为正确 | ABI v4 板端回归、完整摄像头输入链路、通用模型工具边界 |
| 本地 Qwen2.5-1.5B | 基本对话真机通过 | v95 英文单轮；v96 中文流式输出和两轮记忆，第二轮能回答 `Your name is Tom.` | 独立精度参考、持久 KV、长历史、采样、长稳和可靠取消 |
| `llm` 正式命令 | 已构建，未单独板测 | v97 已通过交叉构建、符号、镜像和文件系统校验 | 板端入口、资源释放和 stop/异常路径验收 |
| 在线官方 Agent | v101 功能实机通过 | 板端初始化、加密凭证、自动就绪和连续中文云端对话已有实机记录 | 长稳、掉线恢复、配置重载和会话连续性 |
| vision-4 文字路由 | v102 已构建，待板测 | 直接规则、云端优先、失败降级、回复标签解析通过主机测试和镜像校验 | 真实板端完整路由回归 |
| 本地 ASR | 仅适配骨架和主机 mock | sherpa-ncnn C API 适配、PCM 归一化、流式排空、尾静音和错误路径测试通过 | 依赖构建、真实模型、ARM64/openvela 识别、媒体输入 |
| USB 麦克风 | 暂停，未完成 | Type-C Host/VBUS、USB2 PHY J 状态、xHCI RUN 已观察到 | xHCI Platform Host、标准枚举、UAC 和等时 PCM 全部未完成 |
| UART4 TTS | v115 已构建，待板测 | `/dev/ttyS4`、TW-TTS UTF-8 帧、Agent 自动播报和主机协议测试通过 | 实机发声、模块兼容性与长期连续播报 |
| I2S0 MAX98357A/INMP441 | 安全诊断已构建 | `/dev/a733-audio` 只读时钟/寄存器快照；接线和 PCM 抽象已记录 | 上板测钟后启用标准 Audio/Media 播放与采集下半层 |
| 显示/动作 | 未完成 | 上层回复中的 emotion/action 标签可解析 | 实际硬件输出驱动和产品闭环 |
| Bluetooth/GPU/UFS/MIPI CSI | 未完成 | 仅有部分资料或占位 | 独立适配与板测 |

## 可依赖的恢复点

### UART4 TTS / I2S0 音频起点

- v115 镜像：`openvela-a733-cubie-a7z-sd-audio-uart-v115-candidate.img`。
- 镜像 SHA-256：`cf25ac887087e5bbad1863258225d2060c3c875706b84686d1ef8cdc6b01c834`。
- 内嵌内核 SHA-256：`2d3af39ada75f72e42f127867637c2643142ec2d949c0117b1c62d8d5df80d08`。
- 已完成完整交叉构建、ext4、GPT 和内嵌内核哈希校验；尚未取得本版本实机结果。
- UART4 是本版本可验收主链路；I2S0 仅为非破坏诊断，不会主动切换 PB4..PB8 或输出音频时钟。

### 网络

- 提交：`17b06c9 fix: drain FCU760K runtime message endpoint`
- 镜像：`openvela-a733-cubie-a7z-sd-wifi-msg-pump-v71-candidate.img`
- SHA-256：`f00fc5a48c48f7958bdf742913c6d045b56906fb54309669ed1b34d71999322f`
- 状态：真机长稳基线。

### 本地 LLM

- v95：单轮英文 ChatML 和双 token 数值兼容真机通过。
- v96：中文 UTF-8 流式输出和有限两轮历史真机通过，是当前本地对话功能的最强已验证基线。
- v97：增加正式 `llm` 命令，镜像 SHA-256 为 `d9a143eba8d511cab26888ab980ccc66bd81823bbf3fa98a0df445ea4994ed82`；只完成构建和镜像验证。
- 已知风险：不要在推理中强制 Ctrl+C；既有日志曾出现缓存保持 busy。协作式 `stop` 尚未完成全面板测。

### 在线 Agent

- v101：首次初始化、板端生成并保存加密凭证、自动启动和中文请求保护已经进入实机验证链路。
- v102：完整文字路由候选镜像，SHA-256 为 `598b8436c68f8ea7de28859e41174908971bc6eca4860f4f6b29b2a01c2edaab`。
- v102 尚无实机路由验收，因此接手时应把 v101 视为运行证据基线，把 v102 视为待测功能候选。

### NPU

- LeNet 输出 CRC：`d50f5d79`。
- YOLOv5s 三输出 CRC：`53127e9c ddb5675e 6e01633e`。
- YOLOv8n PCQ 六输出 CRC：`55d0becd ead06611 ba209c57 2637657c 9e7f743a 2eb4d5ce`。
- 这些是实机通过的黄金回归值。较新的原子运行 ABI v4 只完成构建，仍需板端回归。

## USB 暂停点

最后一次实机可确认的状态来自 v113：

- ET7304 已处于 Source/Host，CC1/CC2 检测和 VBUS 正常；
- DWC3/xHCI 能力寄存器可读，xHCI 1.20 已进入 RUN，`HCHalted=0`、`CNR=0`；
- USB2 PHY 报告 J 状态；
- 两个 xHCI `PORTSC` 仍为 `0x000002a0`，`CCS=0`，设备没有进入标准枚举；
- 当前自写代码只有最小 DCBAA、scratchpad、command/event ring 和 RUN，不具备 Enable Slot、Address Device、EP0 控制传输、Root Hub 事件、等时传输或 UAC；
- `/dev/a733-uvc` 中 `enumeration=-11` 是未执行枚举时保留的状态，不能解释为一次完整枚举失败；
- v114 只是一项未验证的 PHY reset edge 假设，已冻结。

后续其他 AI 接手 USB 时，应先取得同板 Linux 的 xHCI/USB 描述符黄金基线，再将 NuttX xHCI 公共实现移植为 A733 MMIO platform host，不能从 v114 继续累加寄存器试验。

## 2026-09-20 仓库整理更新

- 仓库分支：`a733-cubie-a7z-share`。
- v115 音频/UART 源码提交为 `002a310`；桌宠链路总览提交为 `4ed3adb`。
- USB v105-v114 实验源码、Combo PHY 表、阶段文档和镜像分区复制修正已经纳入
  本次仓库整理，但 v114 仍明确标记为未实机验证候选。
- manifest 已补齐 NPU、桌宠、ASR 和本地 LLM 应用映射；标准构建入口会临时
  应用并恢复公共仓 patch。
- `archives/`、镜像、ELF、模型和恢复 bundle 已由 `.gitignore` 排除，继续只作
  本地恢复材料。
- 当前 manifest 指向个人协作仓库；正式提交组委会前必须按
  `docs/SUBMISSION_CHECKLIST.md` 恢复官方仓库和分支。

在整理或切换任务前，不应直接 `git reset --hard` 或清理未跟踪文件。USB 实验源码、构建脚本修改和本地归档应分别保存，避免连带删除已经有效的打包器修正或恢复材料。

## 当前产品链路

已成立的最长链路是：

```text
Cubie A7Z 启动 openvela
  → FCU760K Wi-Fi / DHCP / TLS
  → 终端文字输入
  → 本地规则或官方 Agent
  → 文字回复、emotion/action 标签解析
```

另一条已验证的独立能力是：

```text
TF 卡 GGUF
  → openvela 本地 Qwen2.5-1.5B
  → 中文流式回答和有限历史
```

以及：

```text
A7PM / 动态 RGB 输入
  → VIP2 NPU
  → YOLOv8 输出与检测框
```

尚未形成的完整产品闭环是：

```text
真实语音输入 → ASR → Agent → TTS/扬声器
摄像头实时帧 → NPU → 屏幕/动作执行
```

## 建议的下一步优先级

1. 有开发板后先验收 v115 UART4 TTS 和 `/dev/a733-audio` 诊断。
2. 无开发板时把本地 Qwen 注册为桌宠 `LocalRouteBackend`，并补齐稳定会话和
   流式 Agent 输出。
3. 修复本地 LLM Ctrl+C/stop 后资源状态，再扩展本地模型能力。
4. 本地 ASR 只有在真实模型和依赖能在 openvela 上运行后再接媒体输入。
5. USB 麦克风保持冻结，直到新的接手方案具有 Linux 黄金基线、正式 xHCI platform 架构和分阶段验收标准。
