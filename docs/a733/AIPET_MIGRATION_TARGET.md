# 桌宠新移植目标与备用接口

## 当前工作切换

按用户最新要求，暂停本地 LLM 和本地 ASR 开发，恢复桌宠联网链路，优先使用官方 AI Agent 引擎。
模块与验收清单见 `board/a733-cubie-a7z/openvela-overlay/apps/system/aipetasr/README.md`。
原 UART-first 与后续 I2S 计划保留，不在此次切换中删除或启用 I2S。

## 当前主目标：UART4 版本

先跑通 Linux vision-4 的功能，不以引入框架为理由改变产品行为。
顺序：键盘/USB 麦克风输入 → 规则路由 → 官方 Agent 消息总线/云端 → 表情/动作解析 → UART4 TW-TTS 与屏幕。
保留直接规则回复、本地与云端共存、离线降级和云端请求失败降级。
本地 Qwen 与本地 ASR 仅保留调用接口；不把接口存在误称为本轮离线闭环已完成。
新硬件仍需驱动与板测，不用 Mock 或“有接口”宣称功能完成。

## 官方能力复用选择

本地源码根：`quickly-openvela/packages/ai_agent`，以下均相对此根。

| 能力 | 官方位置 | 接入原则 |
| --- | --- | --- |
| 云端 LLM | `src/llm/llm_proxy.{h,c}`、`llm_parse.c` | 优先复用请求/响应；保留原桌宠路由 |
| 在线 ASR | `include/voice/voice_asr.h`、`src/voice/volc_asr.c` | 实现/注册原版服务适配，或配置火山服务，不强制更换服务商 |
| 录音上层 | `src/voice/audio_capture.c` | 复用官方 Media Recorder，USB UAC 驱动另行实现 |
| 消息总线 | `src/core/message_bus.{h,c}` | 后台对话单一所有者；遵守消息内存所有权 |
| 调试追踪 | `src/core/agent_trace.{h,c}` | 轮次 ID、耗时、错误；调试开关和脱敏 |
| 未来 PCM TTS | `include/voice/voice_tts.h` | 返回 PCM，不假装 UART 发声是 PCM 合成 |
| 未来 PCM 播放 | `include/voice/audio_playback.h`、`src/voice/audio_playback.c` | 官方 Media 播放；I2S 下半部独立适配 |

官方仓库：<https://github.com/open-vela/packages_ai_agent>。
暂不引入 ReAct 全工具、社交 Bot、Hub/Node、QuickApp；这些不是当前闭环必需项。
`ai_chat` 实时对话示例作为参考，不取代本地 LLM 与 UART TTS。
本地没有 `packages/demos/mimo`，上游已有，不能混淆本地可构建状态与上游文档能力。

## 已知接口限制

当前本地流式 ASR/TTS 直接委托火山实现，不是注册任意后端后自动支持流式。
network_is_connected 主要检查 IPv4 地址，不代表 DNS、互联网或模型服务可达；必须结合限时请求和退避。
本地 vela_tls.c 使用 VERIFY_OPTIONAL，接入前必须核对 CA、主机名和验证结果，正式链路需要可靠证书验证。
llm_proxy 使用全局配置，云端后端切换须统一所有权，不能多个请求竞相改全局设置。

## 本轮预留的输出契约

代码：`board/a733-cubie-a7z/openvela-overlay/apps/system/aipet/speech_output.{hxx,cxx}`。

- `SpeechOutput::speak(UTF8)`、`stop()`：业务层只依赖文本输出契约。
- `UartSpeechOutput`：当前主路线。调用受控 UTF-8→GB2312 转换回调与 UART transport；没有转换器返回 ENOSYS，不原样发送 UTF-8。
- `PcmSpeechOps` / `PcmSpeechOutput`：未来备用路线，合成/播放/停止回调可连接官方 voice_tts 与 Media。未提供回调时 ENODEV，默认不启用设备。
- PCM 预留格式 s16le、16kHz、单声道，整句缓存上限 320000 字节；长语音需后续分块接口，不静默截断。
- 当前 UART stop 协议未验证，显式返回 ENOSYS；不能打印“停止成功”冒充实际停止。
- 当前备用契约并不表示已调用官方服务、已移植 I2S、已完成后台 worker 或已注册可运行 aipet 应用。

测试：`tools/test-aipet-speech.cxx`；验证未配置失败、PCM 参数与长度边界、UART 未配置转换器失败。
正式调试日志默认关闭；实际后端和板端全链路仍待验证。

## 后续 I2S 阶段（当前不开发）

先完成 UART4 版本完整验收，再加入 MAX98357A 与 INMP441。
资料：`D:\My-Program\AI-Agent-Program\cc-program\Radxa_A7Z_Sencond_Pro\Old-Program\a7z_i2s_audio\README.md`。
未来仅替换录音/播放适配层，沿用路由、云端与本地推理、表情和动作核心。
官方代码保持原样；临时构建映射由可恢复脚本处理，所有项目修改保存到本仓。
