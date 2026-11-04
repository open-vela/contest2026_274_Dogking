# openvela 本地流式 ASR 模块

桌宠移植暂时暂停，独立开发本模块；不修改原 Linux 开发目录或官方源码。

## 当前真实状态

已实现流式 PCM 会话管理与官方 Media 录音适配源码。
**尚未接入模型和推理引擎，不能识别语音，没有注册可运行 ASR 命令，也没有生成镜像。**
`local_asr_open(NULL,...)` 返回 `-ENODEV`，不生成假文本。

## 架构与接口

- `local_asr.h` / `local_asr.c`：独立会话 API，s16le/16kHz/单声道 PCM；接受任意字节块，包含跨块的半个采样，按 320 样本/20ms 提交引擎；缓存固定，不累积整段录音。
- `local_asr_partial` 返回中间文本，`finish` 提交最后不足帧与结束信号，`abort` 中止，`destroy` 释放；失败后不重试同一输入，需销毁重新创建。
- 引擎 `result(...,final=1)` 必须执行输入结束、解码排空和最终结果返回；引擎内部需有解码进度及资源上限。
- 状态接口记录字节数、样本数、帧数及错误，不记录原始音频或对话。
- `openvela_capture.c` 直接使用官方 Media Recorder buffer 模式。当前为最多 30 秒的同步采集适配，未实现可中断后台录音；官方 read_data 可能阻塞，不能把音频时长限制当成墙钟超时保证。
- 16kHz 为当前协议；其他采样率须先重采样，不偷偷解释为 16kHz。
- 当前单一所有者线程调用，非并发安全；中间结果不能直接触发动作。

## 模型与推理候选（尚未集成）

优先评估 Sherpa-ncnn 中文流式 Zipformer-transducer，小模型 CPU/ARM64 路线，不默认承诺 NPU 加速。
官方说明其可离线运行，并有中文流式模型；嵌入式 Linux 支持不等于已经兼容 openvela。

- 上游文档：<https://k2-fsa.github.io/sherpa/ncnn/index.html>
- C API：<https://github.com/k2-fsa/sherpa-ncnn/blob/master/sherpa-ncnn/c-api/c-api.h>
- 需冻结 sherpa-ncnn、ncnn、特征提取依赖的版本与许可证；先关闭 Vulkan、Linux 麦克风示例、Python、OpenMP 依赖，审查线程/文件/NEON 与 openvela C++ 运行时。
- 模型资产需要 encoder/decoder/joiner 的 param/bin 与 tokens，不能用 Qwen GGUF 代替 ASR 模型。
- 初期考虑 CPU6/7 A76、少量线程；必须测 RTF、延迟和与 LLM 同时运行时的资源竞争，不能先宣称实时。

## 官方接入路线

上层录音：`quickly-openvela/frameworks/multimedia/media/include/media_recorder.h`。
官方 ASR 扩展：`quickly-openvela/packages/ai_agent/include/voice/voice_asr.h`。
未来可注册 local 后端的整段 recognize 适配；真正流式接口在当前本地官方实现里直接委托火山，不能只 register 就宣称支持本地流式。
因此先保留独立本地流接口，再用本仓可恢复补丁或服务适配连接官方 Agent，保持官方环境不受永久改动。
USB 麦克风底层依然需要 UAC 与控制器等时传输支持；模型测试先走文件 PCM，独立于缺失的 USB 音频驱动。

## 验收顺序

1. 主机流会话测试（mock，不算识别）。
2. 依赖移植与 ARM64/openvela 编译、模型文件检查。
3. 同一录音在上游与板端识别对照、实时倍数与内存检查。
4. partial/final、连续会话、取消、静音和损坏文件验证。
5. USB 采集 → 官方 Media → 本地 ASR 真机闭环。
6. 再恢复桌宠移植，接本地 ASR 文本输入。

测试源码：`tools/test-local-asr.c`，覆盖全部 643 个分块边界、最后短帧、缺引擎、奇数字节结束、后端失败与取消状态。
