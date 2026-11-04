# 小派 A7Z 桌宠 v1.0 候选版（v217-audit2）

日期：2026-10-31。目标是第一代可整机验证的产品集成，不是宣称所有硬件、云 API 或延迟指标已经通过。
v204 摄像头通过版本、v216 语音质量候选、历史过程文档和用户原有总结均保留。

## 1. 产品链路

| 输入 / 功能 | 实际执行路径 | 输出 |
| --- | --- | --- |
| 联网聊天 | ICS43434 → 端点检测 → 官方火山 ASR → 官方 Agent / 已配置 DeepSeek 或 MiMo → 官方火山 TTS | I2S MAX98357 + 表情 |
| 离线聊天 | ICS43434 → NPU 通用 ASR → CPU MiniCPM5 → CPU 声学模型 + NPU vocoder | I2S MAX98357 + 表情 |
| 语音识物 | 所选 ASR → 严格识物规则 → 13M214 新采集 → 六路输出 YOLO → 同帧 SPI 画面 / 框 → 所选 TTS | 真实类别中文名称；不让 LLM 编造图像 |
| 语音运动 | 所选 ASR → 严格白名单 → UART4 二进制协议 → STM32 | ACK / 拒绝 / 超时的事实播报；不用 LLM 文字控制电机 |
| 常驻交互 | 初始化一次 → 安静等待 → 自动多轮 → 回到等待 | 显式开始、协作停止，半双工 |

联网/离线不是“只切 LLM”。`auto` 要求 Wi-Fi IPv4、合理时钟、官方 Agent 已附着、ASR/TTS 后端已注册和独立语音凭据齐备，才选择云端。
这些检查**不等于公网 API 已实测连通**。强制 `cloud` 时缺配置/网络会在打开麦克风前失败。

每轮在开始监听前选择后端，此轮不在执行动作或播出部分语音后自动换后端重放。
自动模式的云 ASR 失败，可以在**尚未分发任何规则/动作前**，对保留的 RAM 音频重新执行完整本地识别，再走本地后续链路；日志会明确标明。
其他云端失败不重放动作或已播放语音，进入短期冷却，下一轮可走本地。强制 `cloud` 不做本地替代。

审计版在模式文件缺失／损坏时默认 `local`，需要明确保存 `aipet mode auto` 才会自动选择云端。
使用唯一临时配置文件避免断电残留 `.tmp` 阻塞；不宣称 FAT 的文件替换具备掉电事务性。
相机取消／超时不能覆盖所有权清理失败；动作分发前检查取消，前台 Ctrl+C 采用协作生命周期。

本地和云端各有最多三对 RAM 对话历史；**本地历史不会因为恢复网络自动上传云端**。
云端仅加入其自身已经在线处理过的最近完整对话，按官方桥接 4096 字节上限丢弃最旧的完整对，不切 UTF-8 字符。
重启清空历史。`aipet memory clear` 同时清理两侧历史，不卸载 LLM 权重。

## 2. 先检查接线（断电操作）

### I2S 合并模块：ICS43434 + MAX98357

| A7Z 信号 | 40-pin 物理针脚 | 模块扬声器端 | 模块麦克风端 |
| --- | --- | --- | --- |
| PB5 / BCLK | 12 | BCLK | SCK |
| PB6 / LRCK | 35 | LRCLK / LRC | WS |
| PB7 / DOUT0 | 40 | DIN | 不连接此线 |
| PB8 / DIN0 | 38 | 不连接此线 | SD |
| GND | 6 / 9 / 14 / 20 / 25 / 30 / 34 / 39 | GND | 共同地 |

**PB5 不是 LRCLK，PB6 不是 BCLK**，此前无声就是这两条线接反。
模块 `SD` 是麦克风数据输出，不是 MAX98357 的 `SD_MODE`。
合并模块 VCC 按模块厂家说明供电；ICS43434 裸芯片 VDD 只能是 1.65–3.63V，不能把裸麦克风直接接 5V。
扬声器接模块 SPK+/SPK−，差分输出的负端不是 GND。不要把两端中的任何一端接地。
当前采集为 48kHz / 两个 32bit 时隙，BCLK 3.072MHz；应用 ASR 使用经滤波转换的 16kHz 单声道。
默认麦克风数字增益 x5，音量由应用 0–100 调节。杜邦线尽量短，必须共地；LA 抓取建议至少 24MHz。

### SPI ST7735

| 屏幕 | A7Z | 物理针脚 |
| --- | --- | --- |
| VCC / BL | 3.3V | 1 或 17 |
| GND | GND | 任一 GND |
| SCL / SCK | PD11 | 23 |
| SDA / MOSI | PD12 | 19 |
| CS | PD10 | 24 |
| DC | PL5 | 22 |
| RST / RET | PB0 | 7 |

无 MISO；128×160 / RGB565 / mode 0 / 最高 12MHz。沿用已验证 direct 8-bit 和 LVGL 路径。
v1.0 的启动脚本继续保留 SWOUT1 的受控检查、读改写和读回验证，不由应用抢占 SCP 的 PMIC 总线。

### STM32 UART4

| A7Z | STM32C8T6 USART1 |
| --- | --- |
| Pin 16 / PJ24 / TX | PA10 / RX |
| Pin 18 / PJ25 / RX | PA9 / TX |
| GND | GND |

115200 / 8N1 / 3.3V TTL。旧 UART TTS 模块必须断开，不能与 STM32 共用串口。
电机/舵机用合适的独立电源，信号地共地；不要从 GPIO 给电机供电。
`向左看`/`向右看`/`回中`是舵机动作；未安装舵机时不能靠软件让它运动，底盘转向说`左转`/`右转`。

### 13M214

V1.11 板使用 J5 的 31-pin FPC / MCSIB，J3 是 PCIe，不是摄像头接口。
沿用已经通过的 IMX214 RAW 采集、软件色彩/几何处理和 YOLO 路径；不宣称硬件 ISP 或 13MP 实时视频已经实现。
不要带电插拔 FPC。

## 3. 模型与首次设置

```text
/ufs/models/MiniCPM5-1B-Q4_K_M.gguf              首选；也可 /data/models/
/data/npu/general-asr/{asr-encoder.a7tv,asr-decoder.a7tv,asr-joiner.a7tv,tokens.txt}
/data/npu/general-tts/{model-steps-3.basic.ort,vocoder.a7tv,lexicon.txt,tokens.txt}
/data/npu/yolov8n-pcq-v3.a7pm
```

本轮私有测试镜像还包含 SD 上的 MiniCPM5 备用权重，内容与 v216 真实 GGUF 参考测试相同（688065920 字节，SHA256 `81b64d05a23b17b34c475f42b3e72fbde62d4b92cc34541f7a8031d0752deafa`）。
UFS 上原有模型仍然优先，不搬移或覆盖；UFS 不可用时，SD 备用避免直接报模型缺失，但首次加载可能明显较慢，应观察分段加载日志。
YOLO 八兆模型也从锁定旧镜像继承并回读核验；不能只看模型文件名就假定是正确的 COCO80 权重。

ASR/TTS 可以整套放 `/ufs/npu/general-asr` 和 `/ufs/npu/general-tts`；完整 UFS 集才会被选中。
存在但残缺的 UFS 集会报错，不静默混用 SD 版本。YOLO 优先选择非空的 `/ufs/npu/yolov8n-pcq-v3.a7pm`，否则使用 SD 路径。
启动本地路径会请求已有的、一次性有界 UFS handoff / 挂载流程；不会格式化、迁移、擦除或自动重试失败设备。

首次准备可能较慢，日志显示 UFS、声学资源、LLM 预加载、ASR/vocoder 各阶段。`READY MIC ON` 后可直接说话，不需要每轮再输入命令。
没有做免初始化承诺；没有常驻服务之前也不会私自录音。

```sh
ufs start
ufs status
ls -l /ufs/models/MiniCPM5-1B-Q4_K_M.gguf
aipet local model auto
aipet local on
aipet local context 3
aipet audio output i2s
aipet audio volume 50
aipet audio gain 5
```

联网配置仍使用既有加密入口，串口/SSH 隐藏输入：

```sh
wifi connect 你的SSID
wifi time
ntpcstatus
aipet init
aipet audio setup
aipet start
aipet status
aipet mode
```

`aipet init` 的 DeepSeek/MiMo LLM API key **不是**火山 ASR/TTS 凭据。
`aipet audio setup` 需要独立 ASR app/access token 和 TTS API key 等配置；沿用板端加密保存，不在日志、Git 或演示视频中打印。
没有语音服务凭据时先用 `local`；`auto` 也会明确选择本地，不能当作云端验收。

## 4. 按顺序整机验证

刷写前备份 SD 卡上的个人配置、录音和模型；选择 SD 卡，不要误选 UFS 或电脑硬盘。
本轮构建和封装工具只生成普通镜像文件，没有写入任何物理存储。
先用 `aipet help` / `aipet audio service status` 确认产品入口及 `v1.0(v217-audit2)`；
启动早期的 `v141-layout`、`boot188` 或底层音频驱动的 `v212` 是各模块的历史检查点，不是产品镜像版本。
以下产品验收使用 `aipet mode`、`aipet run` 和 `aipet ask`；旧的 `local ask` / `audio offline` 等单项命令只用于诊断，不能替代整条路由验收。

### 4.1 硬件和单链路

```sh
service status
ufs status
npu info
npu selftest
echo init > /dev/a733-audio
echo tone=1000,500 > /dev/a733-audio
display raw8
camera selftest
aipet local check
aipet local preload
```

蜂鸣音必须实际可听见，`status=0` 不是声学验收。`camera selftest` 是软件测试，不是实际出图证明。
不要在任何模型/音频/摄像头任务仍运行时反复初始化、复位或强杀。

### 4.2 本地常驻聊天 + SPI 表情

```sh
aipet mode local
aipet run
aipet audio service status
```

等待串口出现 `READY MIC ON`。常驻工作日志写 `/dev/a7zconsole`；从 SSH 启动后断开 SSH 不应终止工作任务。
依次说：“你好”“我叫小明”“我叫什么名字”“今天有一点难过”“请说 Hello，然后说语音测试成功”。
检查多轮自动恢复 MIC ON、姓名上下文、安慰表情、英文后中文没有丢失。不是识别到“你好”就判全链路通过。

本轮说话至多约 8 秒，保留约 400ms 开头预录，使用能量端点和约 700ms 静音收尾。
过长语音会整轮丢弃，不用半句话触发电机。当前是**半双工**：识别收尾、思考、播报时 MIC OFF 是正常的；没有 AEC / 打断播报。

### 4.3 语音识物

保持常驻服务，依次说：“识别物体”“看看这是什么”。
把狗图片、自行车/汽车图片或其他 COCO 类别物体放到镜头前。
应看到：ASR 完整文本 → 释放 ASR/vocoder 会话 → 新帧采集和同帧 YOLO 框 → 类别中文播报 → 恢复表情和监听。
查看 `product-vision-v1` 的 token/frame/CRC/boxes；不能拿旧帧或纯软件预览冒充本轮识别。
默认播报置信度至少 0.5 的最多七个显示候选。没有检测到并不代表场景中没有物体，也不具有人脸身份/品牌/颜色判断能力。

### 4.4 STM32（先架空履带，远离桌边）

```sh
aipet mcu status
aipet mcu ping
```

确认 UART、能力位、故障码。如果没有 TOF/边缘防护，**禁止把故障清零当作已经获得防跌落功能**。
确需无传感器台架测试时，操作者确认风险后才运行既有 `aipet mcu clear-fault blind-ack`；本产品不会自动执行它。

```sh
aipet mcu arm session
```

这是一次明确的连接会话使能，避免以前每五分钟反复手工 arm；也保留原来的 `aipet mcu arm` 五分钟租约。
使能后说：“前进一百二十三步”“后退123步”“左转”“停止”。
1–1024 步、有限速度/舵机角度/时长约束保持。`一百二` 等歧义步数拒绝，不猜距离；否定句、复合句和不明确指令不执行。
没有实际舵机就跳过看左/看右测试。

**急停、失联、复位、故障、配置变化、ACK 超时仍会撤销使能，重新连上也不自动 re-arm。**
收到 ACK 只说“确认接收”，不声称物理动作完成；运动命令不自动重发。
半双工播报期间不能靠语音打断急停，应保留物理停机手段，也可用独立控制台：

```sh
aipet mcu stop
aipet mcu disarm
```

### 4.5 停止、清历史和恢复

```sh
aipet stop
aipet audio service status
```

等到 `active=0`，不是看到 `stop=1` 就认为已经结束。NPU/DMA 不用 `kill` / `task_delete` 强制销毁。
如果显示 ownership uncertain / quarantine，不能继续复用该设备，保留日志并重启。

```sh
aipet memory clear
aipet run
aipet audio service status
```

再次 READY 后再说话。停止会撤销运动使能；恢复服务不会自动 re-arm。
如需卸载权重，先停止服务并确认 inactive，再运行 `aipet local unload`。

### 4.6 在线 / 自动路由

先停止服务，完成独立云语音凭据配置，确认 NTP 和官方 Agent ready，再运行：

```sh
aipet mode cloud
aipet run
aipet audio service status
```

应明确打印 `frozen=cloud` 和 ASR backend=cloud、official-cloud TTS；即使本地 TTS 已预加载，也不能偷偷替代。
聊天、英文朗读、识物和 STM32 事实播报都重复一遍。没有实际听到、没有云 API 成功日志不能认定在线通过。

```sh
aipet stop
aipet mode auto
aipet run
```

在**轮次之间**分别连接/断开网络，检查后续轮次的选择和原因。不在动作/播报中途自动重放另一条链路。
网络模式切换后的本地首次准备可能存在一次性等待；不是免预加载或自动全双工。

### 4.7 连续摄像头模式（先停语音）

```sh
aipet stop
aipet vision start 30
aipet vision status
aipet vision stop
aipet vision status
```

等 `active=0 cleanup=0` 后才重新 `aipet run`。
连续模式沿用已优化的相机/预处理/NPU/SPI 流水线，但与语音 NPU 会话互斥；不宣称两套模型已在同一个 NPU 上无约束并行。
只想拍一次并播报，也可停止服务后输入 `aipet vision once`，或服务内直接说识物口令。

## 5. 验收记录必须区分

- 主机故障注入 / ARM 编译 / 链接 / 镜像回读：是软件证据，不是物理摄像头、扬声器或云 API 验收。
- 摄像头验收：实际出图、六路输出校验、同一帧画面与框、重复识物、连续采集、停止及错误后恢复。
- 音频验收：实际可理解的 ASR、没有词语/后半句丢失的 TTS、连续多轮、上下文和英文。
- STM32 验收：物理接线与功能安装相符、ACK 与动作完成分开、故障/失联/停止撤销权限。
- 延迟：冷启动准备、结束说话到识别完成、LLM 首字、数字 PCM 首包、真实扬声器首音分别记录；完整播放总时长不能拿来当首音延迟。

目前没有 v1.0 新板测、云 API 实测或整机首音 3–5 秒达标证据。云 ASR 使用端点后的官方批量接口，云 LLM 等官方完整回复；没有伪称云端逐 token/音频流式已完成。
本地保留 v216 的 LLM 首短语→TTS 重叠、完整短语排队和 vocoder 窗口连续输出。
本地模型仍有有限输出预算；超过预算是明确失败，不会把不完整回答记入历史。

## 6. 构建与恢复

开发基于官方 openvela/NuttX、官方 Agent 云服务、既有 A733 BSP，C/C++/LVGL；不依赖板上 Python、Linux .so 或伪造推理。
完整依赖、ORT 构建、模型转换、许可证和 Linux golden 过程继续见历史文档；不删除旧记录。
新版构建入口：

```sh
bash tools/build-a733-product-v1.sh out/product-v1-build-r1 out/ort-static-v185-r2
```

仅在已确认合格的同一个 CMake cache 复用时才传 `--incremental`；默认要求新目录，不删除已有 cache。
旧 v216 cache 不会因为传入环境变量就自动启用新增 Kconfig 项；v1.0 首次必须新建 cache，包装器和最终检查都强制验证实际 `.config` 含 `CONFIG_A733_PRODUCT_V1=y`。
`A733_PRODUCT_BUILD_DIR` 可指定 cache；构建包装器临时暂存 SDK 后必须逐文件恢复并验证。

软件回归：`tools/test-product-v1.sh`、`tools/test-product-service-v1.py`、`tools/test-aipet-mcu.sh`，加全部 v216 语音和既有摄像头回归。
实际 NuttX ARM 编译：`tools/check-a733-product-arm-v1.py`；完整 ELF / ABI / ISA / 摄像头：`tools/check-a733-product-v1.py`。
封装入口 `tools/package-a733-product-v1.py` 只接受**新建普通镜像文件**，不写物理 SD/UFS。
封装前设 `A733_PRODUCT_LLM=/实际路径/MiniCPM5-1B-Q4_K_M.gguf`，只接受上述已验证的大小与 SHA256，不自动下载或换模型。
镜像内使用 `camera-v217.scr`；最终镜像 SHA256、源码 commit 和本地恢复目录以发布证据为准。

UFS 上残留旧 U-Boot 的问题仍保留，**本轮没有擦除 UFS 引导区，也没有格式化模型盘**。
若误进旧 U-Boot、SD 被错误显示为 30MiB，不继续写盘；这是启动源问题，不通过改摄像头/音频参数掩盖。
通用 TTS 的模型可能具有非商业条款，发布/商用前必须复核；原有模型许可证随私有资产保留，不把模型和凭据推到 Git。
当前带模型镜像用于本项目私有板测；对外发布镜像前，还必须复核 MiniCPM5 和各语音模型的分发许可，不能把源码许可证当作权重许可证。
