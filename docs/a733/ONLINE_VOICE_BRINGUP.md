# 在线语音链路：适配目标与硬件验收门槛

目标：USB 麦克风 → openvela 媒体采集 → 官方豆包云端 ASR → 桌宠路由/官方 Agent → UART4 TW-TTS。

本文是源代码核对记录和待实现清单，不代表语音链路已经运行。

## 本次实机日志与第一批修正

用户已确认同一 DP Type-C 口、转接头和 USB Hub 在 Linux 正常，因此优先定位板级初始化，而非要求厂家麦克风驱动。
附件日志显示 `reset=-110`、`VID:PID=0000:0000`、DWC3 快照为零。尚未取得麦克风描述符，不能判断 UAC 版本。

第一批改动只修正安全性与错误定位：HCRST 前等待 CNR 清除；分别记录 halt、复位前 CNR、HCRST 超时；记录复位前控制器身份；Type-C 初始化失败立即停止后续控制器操作；删除读取全一值时误执行 USB0 EHCI 的回退；明确节点注册不等于 audio-ready。

这些改动不包含 xHCI 传输环、UAC 采集或复位根因修复。根因还需要核对完整 Combo PHY 初始化及下一次分阶段寄存器证据。不能盲目开启未经参考确认的时钟位，也不能用 EHCI 地址取代该 Type-C 的 xHCI 控制器。

构建前恢复点：上级 `a733-before-usb-audio-work.bundle`。

构建问题记录：第一轮构建脚本的 staging 清单漏掉 `a733_usb_camera.c`，因此构建 exit=0 不证明 USB 改动被编译。已补入清单；该文件仍按原有 EXIT trap 备份/恢复。必须在新内核中核验新增诊断字符串，之后才能提供实机诊断镜像。

## 已确认的代码状态

- 官方 `packages/ai_agent/src/voice/audio_capture.c` 使用 `media_recorder`，忽略传入的设备路径，由媒体框架路由。不能传入一个 USB 设备名就得到音频。
- 官方 `voice_asr.c` 有通用整段识别后端接口；流式封装直接委托 `volc_asr_stream_*`。
- 官方豆包 ASR 实现配置 `/api/v2/asr`，需要独立 App ID、Token、Cluster；大模型 API Key 不能代替 ASR 凭据。实际服务可用性需要账户测试。
- 当前 A733 defconfig 仅启用 16550 UART0；`a733_serial.c` 的轮询控制台也是 UART0。不能将 Linux `/dev/ttyAS4` 直接假定为 openvela `/dev/ttyS4`。
- 仓库已有 `uart_tts.*` TW-TTS 帧封装及 `speech_output.*` 输出抽象，但当前 aipet CMake 未编译它们，路由的 present 仅打印回复。
- `UartSpeechOutput` 需要 UTF-8 → GB2312 转换回调；缺失时明确返回 ENOSYS，不得直接发送 UTF-8 冒充 GB2312。
- 原始 Linux 项目中 TW-TTS 使用 UART4、9600 波特率；原始目录仅只读检查，未修改。
- 当前搜索的 USB host 驱动目录没有现成 USB 音频类驱动；A733 外部 USB 控制器仍需要核对等时传输和 UAC 枚举支持。

## 实现顺序

1. 确认麦克风 VID/PID、UAC 版本、接口 alternate setting、采样格式、端点类型及实际连接端口。先完成枚举与固定时长 PCM 录音，验证长度、波形、采样率，不直接联调云端。
2. 在项目仓库新增/保存 UAC 及控制器适配，将采集注册到 openvela 音频/媒体路径。录音增益先按 USB 麦克风验证，不盲用官方 DMIC 的默认六倍增益。
3. 独立测试官方 ASR：PCM 文件 → 云端识别文字；配置凭据不通过命令参数或日志暴露。验证证书、系统时间、断网/超时/取消/重复请求。
4. 为 UART4 配置时钟、复位、实际针脚 pinmux 和字符设备；保持 UART0 控制台不变。验证 9600 8N1 和模块反馈；先测试固定中文短句。
5. 编译接入 GB2312 转码及现有 TW-TTS 输出。长回复按字符边界分段，明确处理不支持字符、写超时及模块忙状态，不静默丢字。
6. 把 ASR 文字送入现有桌宠路由，云端请求继续走官方 Agent。输出只绑定 UART TTS，不同时启动官方 PCM TTS，以免重复付费/重复播报。
7. 首版采用显式固定时长/按键录音；验收之后再启用持续监听、VAD 和打断，避免无边界上传、回声或无限付费。

## 实机信息收集

可以暂时把麦克风插到 Linux 机器，执行 `lsusb` 确认 VID/PID，再执行 `sudo lsusb -v -d VID:PID` 和 `arecord -l`。提交输出即可，切勿提交 API Key。

若只有当前 openvela 板，可先插入麦克风执行 `echo probe > /dev/a733-uvc`、`cat /dev/a733-uvc`、`dmesg`；这是已有 USB 诊断入口，名称包含 uvc，但不能据此宣称支持麦克风或一定能完整枚举。

## 验收与产物要求

- 至少连续十轮语音对话，无采集线程/内存/连接泄漏。
- 记录录音结束到 ASR、LLM 首响应、UART 首播的分阶段延迟。
- ASR/LLM 失败不朗读伪造成功内容，凭据和原始音频不默认写日志。
- 本地 LLM/ASR 保留接口但继续暂停；I2S 不在本阶段实现。
- 每次构建前保存代码恢复点；官方源码只临时 staging，构建后恢复。
- 硬件采集和串口未验收前不把镜像称为“全链路完成版”。
