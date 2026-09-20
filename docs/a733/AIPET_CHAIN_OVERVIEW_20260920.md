# A733 openvela AI 桌宠总链路

更新时间：2026-09-20

本文只记录已经进入比赛仓库的实现、已有实机证据和明确保留的接口。候选代码、主机测试通过和实机通过分别标注，避免把“能编译”误写成“硬件已完成”。

## 一、当前结论

当前可用的主链路是：

```text
串口/SSH 文本
  -> aipet 输入校验
  -> 桌宠路由器
  -> 内置快速规则，或 openvela 官方 packages_ai_agent 云端 Agent
  -> 回复标签解析
  -> 终端文字输出
  -> 可选 UART4 TW-TTS（v118 实机通过）
```

其中，联网初始化、API Key 加密保存、Agent 自启动、DeepSeek 连续对话和 UART4
播报已经通过实机测试。本地 Qwen2.5-1.5B 已通过 `LocalRouteBackend` 注册到桌宠
路由器，支持强制本地调用以及断网/云端失败回退。USB/I2S 麦克风、I2S 扬声器、
动作执行和摄像头到 YOLO 的完整产品链路尚未打通；ST7735/LVGL 已形成 v120
可烧录候选，但屏幕表情仍待实机点亮和产品层事件接入。

## 二、系统分层

### 1. 输入层

| 输入 | 当前状态 | 说明 |
| --- | --- | --- |
| NSH/串口文字 | 实机可用 | `aipet ask "..."` |
| SSH 文字 | 实机可用 | SSH 登录后使用同一命令 |
| USB 麦克风 | 未完成 | USB2/xHCI 和 UAC 音频设备尚未形成可采集 PCM 的 openvela 音频设备 |
| INMP441 I2S 麦克风 | 仅诊断阶段 | 已确定 I2S0 引脚与时钟资源，尚无 capture lower-half 与 PCM 数据流 |
| 摄像头事件 | 未接入桌宠 | UVC/相机取帧仍在适配，未向桌宠路由器提交视觉事件 |

真实产品链路目前从“文字”开始，而不是从“语音”开始。

### 2. 路由层

`pet_core` 使用有限状态机：

```text
idle -> routing -> generating -> presenting -> idle
                         \-> error -> idle
```

路由顺序为：

1. 空输入直接返回“没听清”。
2. 命中 `direct_rules` 时本地立即回复，不调用网络或模型。
3. 其余请求在 `wlan0` 有 IPv4 地址时进入官方云端 Agent。
4. 离线时进入本地模型接口。
5. 云端请求失败时尝试本地模型接口。
6. 完整回复经过动作/表情标签解析，再交给输出层。

内置快速规则包括时间日期、问候、告别、感谢和简单天气提示。规则可由 `/data/ai_agent/config/routes.json` 覆盖，解析失败时请求会被拒绝，不会带着损坏配置继续运行。

当前 `online()` 只判断 `wlan0` 是否取得 IPv4，不代表 DNS、NTP、TLS 和公网一定正常。后续应改为网络管理器提供的分级状态：`link / ip / dns / time / internet / agent-ready`。

### 3. 推理层

#### 官方云端 Agent

这是当前桌宠的主推理后端，使用 openvela 官方 `packages_ai_agent` 的消息总线、上下文、LLM proxy、TLS、Skills 和工具注册机制，不是单独写一个 HTTP 请求冒充 Agent。

- 首次运行使用 `aipet init`。
- 默认支持 DeepSeek 和 MiMo。
- DeepSeek 默认模型为 `deepseek-flash`，也可输入自定义模型名。
- API Key 使用板级密钥加密保存，不写入镜像和仓库。
- 初始化完成后启用 Agent 自启动，后续开机无需再次输入 Key。
- 实机已验证连续中文请求；首次连接约 7.5 秒，复用链路后约 1.8 秒。

当前产品配置只开放必要的安全工具。联网、DHCP、NTP、CA 和 Agent ready 是发起 HTTPS 对话的前置条件。

#### 本地 Qwen2.5-1.5B

独立的 CPU 推理底座已经完成 tokenizer、Q4_K/Q6_K 张量读取、28 层前向、KV cache、模型常驻和多核工作池等阶段，并在 A733 八核 SMP 上运行过。它当前使用 CPU，不使用 NPU。

桌宠路由器通过线程安全注册口 `aipet::set_local_route_backend()` 接入
`aipetllm_infer()`。该入口直接调用本地推理库，不启动 shell、不捕获 stdout，
并与独立 `aipetllm` 命令共享常驻模型和六核工作池。因此：

- `aipet local ask "文本"` 可强制验证桌宠本地链路。
- 无网或云端失败时，`aipet ask` 自动进入本地回退。
- 默认模型为 `/data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf`，默认 32 个
  输出 token，CPU 掩码为 `fc`（CPU2..7）。
- 不会用固定文案冒充本地模型生成。

#### NPU 与视觉模型

VIP2 NPU 已完成 LeNet、YOLOv5 和 YOLOv8n PCQ 的 prepared trace 执行与输出校验。但相机取帧、预处理、动态输入、后处理和桌宠行为事件尚未连成一条持续链路。NPU YOLO 是视觉分支，不是当前 LLM 后端。

### 4. 回复解析与表现层

回复结构包含：

- `text`：要显示/播报的纯文本。
- `emotion`：默认“普通”。
- `actions`：经过白名单校验的动作。

当前动作白名单为：

```text
motor.forward / motor.backward / motor.turn_left / motor.turn_right / motor.stop
servo.rotate / servo.nod / servo.shake / servo.wave
led.on / led.off / led.rainbow
```

`SentenceStream` 已实现增量分句和标签边界保护：未闭合标签不会提前下发，取消生成会丢弃待处理内容。当前云端桥仍以完整回复为主，尚未把服务端流式片段直接接入 `SentenceStream`。

当前已经输出的是终端文字和解析后的情绪/动作数量。屏幕表情、舵机、电机和 LED HAL 尚未接入，动作只被解析，不会驱动硬件。

### 5. 语音输出

UART4 TW-TTS 已在 v118 完成实机验收：

- 设备：`/dev/ttyS4`
- 参数：9600、8N1
- 引脚：PJ24 TX、PJ25 RX
- 协议：TW-TTS 文本帧，编码选择 `0x04`（UTF-8）
- 开关：`aipet tts on|off|status`
- 单独测试：`aipet tts test "串口语音测试成功"`
- 持久标记：`/data/ai_agent/config/uart_tts.enabled`

启用后，云端和本地回复都会同时打印并发送到 UART4。TTS 失败只输出诊断，
不会破坏文字回复。实机已经验证初始化参数帧、UTF-8 文本帧和重复播报。

MAX98357A/INMP441 目前只完成 I2S0 资源梳理和 `/dev/a733-audio` 非破坏诊断。尚未注册 openvela 音频 lower-half，也没有 BCLK/LRCK、播放或采集数据流。未来应接入官方 Media Player/Recorder，而不是在桌宠逻辑里直接操作 I2S FIFO。

## 三、启动链路

正常目标启动顺序是：

```text
openvela 启动
  -> FCU760K 初始化
  -> 自动连接上次保存的 Wi-Fi
  -> DHCP 获取地址
  -> NTP 校时并准备 CA
  -> packages_ai_agent 后台启动
  -> aipet status = ready
  -> 接收桌宠请求
```

首次使用还需执行一次：

```text
aipet init
```

选择 DeepSeek/MiMo、模型名并输入 API Key 后，配置加密保存并开启自启动。私人密钥、Wi-Fi 密码和 API Key 不进入 Git 仓库或公开镜像。

## 四、目标中的完整链路

### 在线语音桌宠

```text
USB 麦克风或 INMP441
  -> openvela Media Recorder / PCM capture
  -> VAD
  -> 官方云端 ASR（先实现）
  -> 当前文字路由器
  -> 官方 packages_ai_agent
  -> SentenceStream
  -> 表情/动作调度
  -> UART4 TTS（近期）
  -> 官方云端 TTS PCM + MAX98357A（后续）
```

### 离线桌宠

```text
本地 ASR（后续）
  -> 当前文字路由器
  -> 本地 Qwen2.5-1.5B 注册后端
  -> 同一表现层
  -> UART4 TTS 或本地 PCM TTS
```

### 视觉桌宠

```text
摄像头
  -> openvela Camera/Video 采集
  -> 图像缩放、颜色与张量预处理
  -> VIP2 NPU YOLOv8
  -> 检测/人脸事件
  -> 桌宠上下文与行为策略
  -> 表情、动作、文字或语音输出
```

三条链路最终只在输入/推理后端不同，路由、会话、回复解析和表现调度应保持统一。

## 五、故障隔离与降级原则

- 快速规则不依赖网络，网络故障时仍应可用。
- 云端失败只切换到真实注册的本地模型；没有本地模型时明确报错。
- UART/I2S TTS 失败不影响文字回复。
- 麦克风或 ASR 失败不应阻塞 SSH/串口文字入口。
- 非法 UTF-8、超长输入、损坏的路由文件和非白名单动作必须在执行前拒绝。
- 动作执行必须异步，不能阻塞 Agent、Wi-Fi RX 或音频线程。
- 视觉/NPU 故障不得拖垮对话主循环。

## 六、下一阶段顺序

### 无开发板时可以完成

1. 把本地 Qwen 引擎封装为 `LocalRouteBackend`，完成桌宠真实离线回退。
2. 为每个用户/入口使用稳定会话 ID，补齐多轮历史、取消和超时语义。
3. 把官方 Agent 的流式响应接入 `SentenceStream`，降低首句反馈延迟。
4. 将文字、TTS、UI、动作统一为异步输出调度器；硬件后端默认关闭。
5. 为配置版本迁移、路由决策、云端失败、本地回退和重复对话添加主机回归测试。

### 有开发板后优先验证

1. 验证 v115 `/dev/ttyS4` 和 TW-TTS 实际发声。
2. 回传 `/dev/a733-audio` 时钟/寄存器结果。
3. 实现 I2S0 openvela audio lower-half，先 MAX98357A 播放，再 INMP441 采集。
4. 打通 Media Recorder -> 云端 ASR -> Agent -> UART4 TTS。
5. 完成摄像头取帧 -> YOLOv8 -> 桌宠事件链路。

## 七、权威文件

- 路由实现：`board/a733-cubie-a7z/openvela-overlay/apps/system/aipet/pet_routes.cxx`
- 状态机/解析：`board/a733-cubie-a7z/openvela-overlay/apps/system/aipet/pet_core.{hxx,cxx}`
- 官方 Agent 桥：`board/a733-cubie-a7z/openvela-overlay/apps/system/aipet/agent_bridge.c`
- UART TTS：`board/a733-cubie-a7z/openvela-overlay/apps/system/aipet/uart_tts.{hxx,cxx}`
- 音频输出抽象：`board/a733-cubie-a7z/openvela-overlay/apps/system/aipet/speech_output.{hxx,cxx}`
- 当前音频阶段：`docs/a733/AUDIO_UART_I2S_STAGE.md`
- 在线初始化：`docs/a733/AIPET_PROVISIONING_V101.md`
- 路由阶段：`docs/a733/AIPET_ROUTING_V102.md`

`AIPET_AUDIO_AND_LLM_PORT.md` 中“UART 引脚待确认、必须补 GB2312”属于早期记录，已被 v115 的官方 BSP UART4 映射和模块 UTF-8 协议取代；音频现状以 `AUDIO_UART_I2S_STAGE.md` 和本文为准。
