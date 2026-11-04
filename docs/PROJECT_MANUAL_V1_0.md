# 小派 A7Z／Dogking A733 项目说明书

2026-11-03 使用提醒：最新开发候选 [v3.0pre／v227](PRODUCT_V3_0PRE_V227.md)新增云语音传输/延迟诊断、情绪与有界模型动作，并将 UI/语音桌宠步统一×20；原始脉冲改用 move-raw。新功能和顺序板测请读对应说明，不能套用下方 v1.0 历史单位。v219仍为最新稳定，v227真实讯飞 TTS、机械位移和整机首音仍待验收。

2026-11-01：**最新稳定版已更新为 [v1.1／v219](PRODUCT_V1_1_V219.md)**，本轮用户板测确认正常；[v1.2／v220 预览](PRODUCT_V1_2_V220_PREVIEW.md)另行优化本地延迟，默认不带聊天历史但保留人设／KV／常驻权重。请使用对应版本的测试和制品说明；以下仍保留 v1.0 原说明及当时的候选状态。

后续维护：针对 v1.0 部分板测发现的常驻对话 EBUSY、未触发语音和 Wi-Fi 握手问题，开发 [v1.1／v219 修复候选](PRODUCT_V1_1_V219.md)。该记录提供最新制品/复测说明，并保留 v217 镜像；本说明书仍作为 v1.0 历史说明，不覆盖其原始验收边界。

后续独立候选：[v218 多服务商云语音](CLOUD_VOICE_PROVIDERS_V218.md)增加讯飞／腾讯和可选 Edge 电脑网关，完整构建及软件回归通过，见第 12 节。本说明书的 v217 镜像与验收边界保持不变；尚未生成 v218 SD 镜像，新命令需要部署新固件。

版本：v1.0，v217-audit2 审计候选；更新：2026-10-31。
对象：作品评审、使用者、测试者及后续开发者。
本文说明项目是什么、如何接线运行、怎样判断成功和如何维护；技术全景见 [完整总结](PROJECT_FULL_SUMMARY.md)。
镜像、SHA256、源码 commit、恢复目录以 [发布记录](a733/PRODUCT_V1_0_RELEASE.md) 为准。

## 1. 项目简介

本项目把 openvela/NuttX 原生移植到全志 A733／瑞莎 Cubie A7Z，再在平台上形成语音、视觉、表情和受控动作的桌面 AI 终端。
主板为 6 个 Cortex-A55＋2 个 Cortex-A76、4 GiB 内存，VIP2 NPU 用于语音／视觉中的适合部分。
它不是依赖 Linux 后台服务的薄客户端：离线模式在 openvela 上完成真实模型执行。

典型交互包括：聊天与情绪陪伴、语音问答、显示表情、识别眼前常见物体并播报，以及经显式使能控制 STM32 下位机。
本项目当前是研究和参考设计，不是已经完成安全认证、全天候可用的商业家电控制器。

### 1.1 模式区别

| 模式 | 语音输入 | 聊天 | 语音输出 | 条件 |
| --- | --- | --- | --- | --- |
| `local` | 本地通用 ASR，NPU 执行模型部分 | CPU MiniCPM5 | CPU 声学模型＋NPU vocoder | 本地资产完整，不要求网络 |
| `cloud` | 官方云 ASR | 官方 Agent／已配置 DeepSeek 或 MiMo | 官方云 TTS | 网络、时钟、Agent 和独立语音账号齐备 |
| `auto` | 每轮选择整套后端 | 与本轮输入一致 | 与本轮后端一致 | 明确保存 `auto`；云条件不足时本地 |

缺失／损坏的策略文件在审计版默认 `local`，不是自动上传。
规则可以直接处理时间、问候、识物和机械命令，不强制每句话都调用 LLM。
云端不是只切 LLM；DeepSeek/MiMo Key 也不能替代云 ASR/TTS 的凭据。

### 1.2 已实现与待验收

旧版本的启动、网络、存储、LLM、录放音、显示和相机／YOLO 有历史实机成果。
本候选完成代码集成、软件回归、目标编译、镜像绑定及恢复体系；**新整机声画／云 API／动作仍需实测**。
不承诺首音 3–5 秒已达标、所有 AI 均在 NPU、30 FPS 检测、全双工、GPU／ISP／MP4 或 BLE 双板业务已完成。

## 2. 硬件组成和准备

- Radxa Cubie A7Z，已确认板版本 V1.11；符合开发板规格的稳定电源。
- 可烧写当前镜像的 microSD，烧录前备份原数据。
- 1.8 英寸 128×160 ST7735 SPI 屏。
- ICS43434＋MAX98357 合并模块及匹配扬声器。
- Radxa 13M 214 摄像头及正确 FPC。
- 可选已焊接 Kingston 128 GB UFS，作为数据／模型盘。
- 可选 STM32F103C8T6 下位机及对应固件、执行器和独立电源。
- 串口终端；联网后可使用 SSH。普通杜邦线应尽量短并保持可靠共地。

断电接线；不要热插 FPC。电机不能从 GPIO 取电。不要把扬声器差分负端接地。
没有 STM32 或执行器也可以先测试聊天、录放音和视觉，不应伪造动作成功。

## 3. 接线

以下是 A7Z **40-pin 物理针脚编号**，不是 Linux GPIO 全局编号；先确认 Pin 1 方向。

### 3.1 麦克风和扬声器

| A7Z | 物理针脚 | 合并模块扬声器端 | 合并模块麦克风端 |
| --- | --- | --- | --- |
| PB5／BCLK | 12 | BCLK | SCK |
| PB6／LRCK | 35 | LRCLK／LRC | WS |
| PB7／DOUT0 | 40 | DIN | 不接 |
| PB8／DIN0 | 38 | 不接 | SD |
| GND | 任一 GND | GND | 共地 |

PB5 不是 LRCLK；PB6 不是 BCLK。两条时钟接反曾导致“驱动成功且有波形，但完全无声”。
若模块两部分分别引出时钟，一根 PB5 需要分到 BCLK 与 SCK，一根 PB6 分到 LRCLK 与 WS。
模块 `SD` 为麦克风数据，不是功放 `SD_MODE`。若功放单独引出 `SD_MODE`，按厂家要求使能。
合并模块 VCC 按模块规格供电；裸 ICS43434 的 1.65–3.63 V 规格不能等同于整块模块输入电压。
扬声器接 SPK+/SPK−，二者均不能接 GND。

### 3.2 SPI 屏幕

| 屏幕 | A7Z | 物理针脚 |
| --- | --- | --- |
| VCC、BL | 3.3 V | 1 或 17 |
| GND | GND | 任一 GND |
| SCL／SCK | PD11 | 23 |
| SDA／MOSI | PD12 | 19 |
| CS | PD10 | 24 |
| DC | PL5 | 22 |
| RST／RET | PB0 | 7 |

不需要 MISO。当前模式为 SPI mode 0、RGB565 MSB、direct 8-bit 数据传输。
SWOUT1 由匹配启动脚本检查、受控开启和读回；若白屏，先检查接线、供电与启动脚本，不盲改芯片寄存器。

### 3.3 摄像头

使用 13M214，V1.11 接 **J5 31-pin FPC／MCSIB**，J3 为 PCIe，不能接错。
检查排线触点方向、锁扣和型号，断电插拔。寄存器匹配不等于已实际测量供电或 MCLK。

### 3.4 STM32

| A7Z | STM32 USART1 |
| --- | --- |
| Pin 16／PJ24／UART4 TX | PA10 RX |
| Pin 18／PJ25／UART4 RX | PA9 TX |
| GND | GND |

115200、8N1、3.3 V TTL，TX/RX 交叉；旧 UART TTS 必须断开。
电机／舵机独立供电，信号共地；当前镜像没有替下位机烧录固件。
参考源固定于 `hxy299/STM32C8T6Mini-Play` 的 `e83a9cac8a0e4a775a38910bdaf08c68bfdffc8c`。

## 4. 镜像、模型和启动

1. 按发布记录核对文件名、2 GiB 大小和 SHA256。
2. 备份 SD 的个人配置／录音／模型，再用可靠烧录工具写入 **SD 卡**，不要选中电脑盘或 UFS。
3. 使用镜像配套脚本启动，不随意把别版 Image、DTB、脚本拼接。
4. 串口进入 NSH，检查 `aipet help` 和服务状态的 `v217-audit2`。

`v141-layout`、`boot188`、底层音频 `v212` 等是历史模块标签，不是产品版本。
若进入残留 UFS U-Boot、显示错误 SD 容量／分区，先修正启动源，不继续尝试格式化或写盘。

模型位置：

| 数据 | 默认／备用路径 |
| --- | --- |
| MiniCPM5 | `/ufs/models/MiniCPM5-1B-Q4_K_M.gguf`；备用 `/data/models/` |
| ASR | `/data/npu/general-asr/`，可选完整 `/ufs/npu/general-asr/` |
| TTS | `/data/npu/general-tts/`，可选完整 `/ufs/npu/general-tts/` |
| YOLO | `/data/npu/yolov8n-pcq-v3.a7pm`，可选 `/ufs/npu/` |

私有镜像已含本地所需资产及 688065920 字节 MiniCPM5 备用，无需先下载云服务才能启动本地测试。
通用 ASR 需 encoder/decoder/joiner 和 tokens；TTS 需 ORT 声学、vocoder、lexicon 和 tokens，不能只复制一个文件。
存在但残缺的 UFS 语音集会报错，不自动混合两个版本。

本地准备会请求既有 UFS 有界挂载；也可先检查：

```sh
ufs start
ufs status
ls -l /ufs/models
ls -l /data/npu/general-asr
ls -l /data/npu/general-tts
```

UFS 失败不会自动格式化。大模型复制后校验大小和摘要，零字节文件必须视作失败残片。

## 5. 首次本地使用

### 5.1 先做单项检查，不同时开多个资源所有者

```sh
service status
ufs status
npu info
npu selftest
echo init > /dev/a733-audio
echo tone=1000,500 > /dev/a733-audio
cat /dev/a733-audio
display raw8
camera selftest
```

提示音必须真正听到、色条必须真正看到。`camera selftest` 只是软件图像测试，`npu selftest` 也不能替代通用 ASR／YOLO 验收。
可继续检查实际录放音：

```sh
time "echo icsvoice=5000 > /dev/a733-audio"
ls -l /data/ics43434-voice.wav
echo play=/data/ics43434-voice.wav > /dev/a733-audio
```

应录到约五秒真实声音并可听见完整回放。麦克风增益默认 x5，保持静音时也不应反复触发聊天。

### 5.2 设置产品并开始常驻

```sh
aipet local model auto
aipet local on
aipet local context 3
aipet local clear
aipet audio output i2s
aipet audio volume 50
aipet audio gain 5
aipet mode local
aipet run
aipet audio service status
```

首次会装载／预热，观察串口的 UFS、模型、LLM、ASR/vocoder 分段日志。
**READY MIC ON 后说话**，不必每轮再输入命令；空闲持续等待，静音不产生回复。
初始化时间和热轮次分开测。不是刚运行 start 就已监听，也不是每次发言都重新加载全部模型。

可依次说：

1. “你好。”
2. “我叫小明，请记住我的名字。”
3. “我叫什么名字？”
4. “我今天有点难过。”
5. “请说一句包含 Hello 和 OpenVela 的话。”

验证相关性、情绪、上下文和**整句话**，而不是只检查第一段有声音。
不要把固定问候规则的快速回复当作 LLM 性能。
MIC OFF 在收尾、回复和播放阶段是半双工设计，不能边播报边插话；当前无 AEC／barge-in／自动唤醒词。

### 5.3 停止、恢复、清记忆

```sh
aipet stop
aipet audio service status
```

等 `active=0`。停止请求不等于已释放资源；NPU 所有者不能强杀。
如果日志提示 ownership uncertain／quarantine，保留日志、重启恢复，不能马上重复 start。
正常停止后可执行：

```sh
aipet memory clear
aipet run
```

清记忆不卸载权重；重启会丢失 RAM 对话历史。模型更换或卸载应先确认服务已停止。
前台 `aipet ask` 与常驻服务互斥，先 stop 再运行，不在另一个 shell 强行并发抢模型。

## 6. 视觉与识物

常驻服务中说“识别物体”“看看这是什么”或“你看到了什么”。
系统关闭语音 NPU 会话，拍新画面，执行 YOLO，显示同帧底图与框，再以本轮后端播报真实类别。
回答基于 COCO80 常见类别和置信度，不是通用视觉理解；未检测到不等于没有东西。
用两种不同场景反复测试，再测移开物体／空画面，检查不残留上一帧结果。

停止语音后，前台单次识物：

```sh
aipet stop
aipet audio service status
aipet vision once
```

连续视觉需独占资源：

```sh
aipet vision start 30
aipet vision status
aipet vision stop
aipet vision status
```

等相机 `active=0 cleanup=0` 后才能再 `aipet run`。
相机 FPS、YOLO FPS 与 SPI 显示 FPS 不同；不要把预览帧率当检测帧率，连续预览也不是 MP4 录像。

## 7. STM32 受控动作

先完成下位机固件、接线和安全机械安装，无执行器可跳过运动测试。
服务 READY 后检查并显式使能：

```sh
aipet mcu status
aipet mcu ping
aipet mcu arm session
```

确认连接、状态与权限后，说简单单一口令，如“前进十步”“左转”“停止”。
“向左看／向右看／回中”是舵机命令，不安装舵机当然不会运动；底盘与头部不要混淆。
会话使能无需每轮输入，但停止服务、失联、复位、故障和超时后会撤销，不自动重连恢复权限。
歧义／否定／组合／越界命令被拒绝，LLM 文本无动作授权。

```sh
aipet mcu stop
aipet mcu disarm
```

保留物理急停和独立控制台；播报时不能用语音打断来保证急停。
ACK 是接收确认，必须观察机械状态才能判断动作完成。不要因为没听到播报就重复发送运动命令。

## 8. 联网与自动路由

没有云账号可以一直用本地。配置云功能先正常停止服务，随后：

```sh
wifi connect 你的SSID
ifconfig
wifi time
ntpcstatus
aipet init
aipet start
aipet status
aipet audio setup
aipet mode cloud
aipet run
```

按向导填写各自凭据，输入隐藏；不要把 API Key 贴到日志、仓库或演示视频。
Agent `ready` 表示本地引擎已附着，不意味着云 API/TLS 已成功；第一轮必须观察真实 API 与声音。
确认云 ASR、官方 Agent、云 TTS 均成功，再做英文、上下文、识物和 MCU 事实播报。
云语音接口未在本次开发中实际付费调用，不能把这些命令当作验收结果。

```sh
aipet stop
aipet mode auto
aipet run
```

在轮次之间连接／断开网络，检查后端与原因；不能在动作／已播出语音后重放另一条链路。
本地历史与云历史各自保存在 RAM，恢复网络不会自动把本地历史上传。

## 9. 维护与故障定位

SSH 首次使用 `sshd --setup openvela`，之后 `sshd &`；查看 `sshd --status`。
电脑用 `ssh openvela@板端当前IP`。不要使用会失败的 `sshd start` 语法，也不要把 `service` 写成 `services`。
FTP 使用 `ftpd setup`、`ftpd start`，按提示账号及板端自设密码连接，默认根目录 `/data/models`。
不在说明书预设弱密码，不全局关闭 TLS／SSH 主机校验。

| 现象 | 优先检查 |
| --- | --- |
| MIC 迟迟不 READY | 分段加载停在哪、模型路径／完整性、UFS 状态、后台任务与实际进度 |
| 完全无声 | 音量／输出 i2s、PB5/6 时钟接线、DIN、共地、模块电源／使能、扬声器差分接法 |
| 录音噪声或过短 | MIC 硬件、通道、SCK/WS/SD、真实计时、FIFO 计数和 PCM 数据，不只看非零 |
| TTS 半句／英文异常 | 文本、完整分句日志、模型／词典配套、队列完成、数字 PCM 与实际声音 |
| 后几轮答非所问 | 单独复测上下文、清 RAM 历史、记录输入／模型身份／终止状态，不假定 1B 无限理解 |
| 屏幕白／无变化 | 接线、SWOUT1 VERIFIED、实际 SPI 波形、正确镜像；ready 不等于显示成功 |
| 相机／NPU -110 | 哪一阶段超时、IRQ／guard／cleanup、真实模型与供电；禁止重复强杀／reset |
| -2／-19／-12／busy | 分别查缺文件、设备/provider、内存池／准备区、现有资源所有者 |
| MCU 不动 | 物理接线／固件／心跳／使能／故障、执行器是否实际安装；不要只看 ACK |
| 网络失败 | 当前 IP、网关、同网段、AP 隔离／波动、NTP、凭据与 API 错误分开定位 |

提供日志时包含：镜像 SHA／版本、完整操作顺序、`service status`、`ufs status`、音频服务／驱动状态、相机状态、`npu info`、`ps`、`dmesg`，并说明实际听到／看到什么。
先脱敏凭据。逻辑分析仪必须共地；3.072 MHz I2S 建议至少 24 MHz 采样，低采样截图不能可靠解码。

## 10. 性能和验收要求

要分开记录冷启动模型加载、热 ASR 收尾、LLM 首字、首完整短语、声学模型、vocoder、首 PCM 与真实扬声器首音。
数字 PCM 返回或打印日志的时刻不是实际发声时刻；完整播放耗时也不能当首音。
3–5 秒首音仍是目标，须按真实时间点测量，不由主机回归推算达标。
视觉记录采集／预处理／CRC／模型准备／NPU／解码／显示，区分预览与识别。
压力测试要包含多次启停、多轮、静音、长句、中英、切网、失联、取消、资源占用和温度；不能只测一句你好。

## 11. 开发、许可和恢复

仓库永久维护 overlay／patch，SDK 临时应用后恢复；不要直接在完整 SDK 散改却不回收源码。
GCC 13.4.0、LVGL 9.1.0、ORT 1.23.2 等版本与来源见完整总结及第三方 provenance。
正常开发使用新报告目录，首次产品构建使用新缓存；复用已验证配置才用 incremental。
软件测试／目标编译／最终链接／镜像回读／实机各自独立，不降低六路 CRC、输入动态性或所有权检查。

Git 标签、源码 bundle、工作树补丁和外部文件摘要一起形成恢复点；只恢复到全新目录。
旧过程文档、v204／v216／旧 v217 保留，审计镜像不覆盖旧镜像。
Git 不保存私人 Key、板端密码或大模型；权重与厂商运行库分发须独立核对许可。
本地带模型镜像目前用于私有测试，不能把源码开源等同于所有二进制权重可公开再分发。

**结论：v1.0 是可复现、可恢复、具有明确功能和安全边界的桌宠参考项目；最终整机验收仍必须由真实硬件、真实声音和真实云 API 完成。**

## 12. v218 可选云语音扩展（独立软件候选）

本节命令仅适用于部署 v218 后，不能在旧 v217 镜像上据此判断功能失败。
火山 ASR/TTS 保留；讯飞和腾讯新增板端原生 ASR/TTS，可独立组合。Edge 只提供实验性 TTS，需要额外常在线电脑的 HTTPS 网关及 FFmpeg，不能作为 ASR，也不等同于微软正式 Azure 服务。

没有语音凭据时继续 `local`；不需要现在购买账号、提供密钥或把 LLM 密钥填进语音配置。
已有账号时先停止语音服务，等待 `active=0`，确认网络、时钟、CA 及 Agent `ready` 后再配置。例如：

```text
aipet audio providers
aipet audio setup tencent
aipet audio setup xfyun
aipet audio providers asr tencent
aipet audio providers tts xfyun
aipet audio providers
```

这是“腾讯识别＋讯飞合成”的配置示例，不必把所有服务都配置一遍。密钥隐藏输入、加密保存，Enter 保留旧值，Ctrl+C 取消；配置／选择不会自动录音或改变产品模式。
`credentials=present` 不是云端成功证明。账号就绪后，显式 `aipet audio cloud-speak "你好，English 测试。"` 使用所选云 TTS；`aipet audio asr 5000` 会上传录音并打印识别，不执行电机，测试可能计费。
普通 `audio speak` 兼容旧的本地 TTS 行为，不能用来证明云合成已调用。

完整配置、Edge 网关证书与令牌、安全停止、云／本地恢复步骤见 [v218 使用说明](CLOUD_VOICE_PROVIDERS_V218.md)。
本候选的完整构建、ARM 编译、签名对照及本机协议回归已通过，[证据](evidence/cloud-voice-v218/README.md)和本地标签 `a733-v218-cloud-voice-candidate-20261031` 已保存。
**真实云 API、Edge 合成／MP3 解码、板上听感和全链首音仍待测试；没有新增 SD 镜像，旧通过版本不覆盖。**

## 附：术语速查

| 术语 | 本项目中的含义 |
| --- | --- |
| ASR | 把采集到的声音转成文字，不是理解并回答问题 |
| LLM | 根据文字和有限上下文生成回复；本地为 CPU MiniCPM5 |
| 声学模型／vocoder | 前者把文字变成声学特征，后者把特征变成可播放波形 |
| NPU | 对适合的神经网络计算加速，不代表整个应用或所有 AI 都在其上运行 |
| KV cache | 保存已计算的注意力状态，减少重复计算；不等于永久记忆 |
| CRC／SHA256 | 用于内容核对的校验／摘要，不单独构成硬件成功或可信签名证明 |
| ACK | 下位机确认收到请求，不保证机械动作已经完成 |
| golden | 在受支持参考系统得到的真实输入／输出基准 |
| 半双工 | 本版录音和播报交替进行，不能在播放期间靠语音打断 |
| quarantine | 资源所有权不明后的隔离状态，不能强杀后继续复用 |
