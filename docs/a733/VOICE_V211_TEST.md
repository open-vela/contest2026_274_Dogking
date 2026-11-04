# v211 上板测试顺序

这是新的整卡 SD 候选镜像；先备份原 SD 上的模型、配置、录音和个人文件。
UFS 不会被镜像生成工具改写/格式化；v210 和 v209 镜像与本地恢复点保留。
测试不需要网络或云 API。不要同时启动摄像头、YOLO、其他 NPU 命令或播放器。

## 1. 通用 ASR 文件测试（不打开麦克风/扬声器）

三条录音已直接放进新镜像，不需要再解压 v190 的单独测试包：

```text
npu selftest
ls -l /data/npu/general-asr/checks
aipet audio general-asr /data/npu/general-asr
aipet audio general-wav /data/npu/general-asr/checks/cmd_open_light_16k.wav
aipet audio general-wav /data/npu/general-asr/checks/cmd_time_16k.wav
aipet audio general-wav /data/npu/general-asr/checks/neg_silence_3s_16k.wav
```

前两条应为“帮我打开灯”“现在几点了”；静音应无转写（-61），不能触发回复或动作。
新阶段日志能区分 input-open、wav-format、tokens-open、model-open/prepare、
asr-model-or-inference。任何非预期错误先保存完整串口日志并停止后续闭环，
尤其 NPU timeout、所有权 quarantine 或需重启提示，不要反复重试硬件。

## 2. 单独验证完整 TTS

```text
aipet audio output i2s
aipet audio volume 50
aipet audio gain 5
aipet audio general-voice /data/npu/general-asr /data/npu/general-tts
aipet audio local-speak-stream "你好，语音测试成功。"
aipet audio local-speak-stream "今天的语音测试已经完成，请继续下一步。"
```

核对后半句也完整播放，不能只听见“你好”。同时观察 segments/rebuffers、
CPU/NPU 耗时、首 PCM 和首 driver pull。后两者不是实测扬声器声学起音。
第一次会话建立/模型加载与第二次热运行分别记录。

## 3. LLM 常驻和文字首句

模型需由你原有文件提供，不随镜像分发。UFS 模型标准路径：
/ufs/models/MiniCPM5-1B-Q4_K_M.gguf；SD 为 /data/models/同名文件。
UFS 未挂载时运行 ufs start，先确认状态为 ready=1。

```text
ufs status
aipet local on
aipet local model auto
aipet local batch 4
aipet local context 3
aipet local preload
aipet local status
```

只用 SD 时把 model auto 改为 model sd。preload 失败不要开始录音。
不需要每条提问前 clear/unload；它们会破坏上下文/KV 或权重预热条件。

## 4. 真实离线半双工闭环

```text
aipet audio offline 1 8000 0
aipet audio offline 3 8000 400
cat /dev/a733-audio
npu info
free
```

等 MIC ON 再说话，播放结束才进入下一轮。第一条固定 8 秒仅用于排查；
第二条才测试能量端点（400 为未重新板测的诊断阈值，不是训练 VAD）。
分别说“你好”“一加一等于几”“请用一句话介绍你自己”；换成任意问题复测，
不能只以两条标准 WAV 判断通用识别成功。该入口不联网、不执行 MCU 动作。
检查连续多轮、静音、Ctrl+C 后能否正常重新开启会话；若提示需重启则不强行复用。

首次确认绑定应出现 target=6/7、actual=6/7、part=d0b、单核 mask、
confirmations=2、result=0。成功诊断只按核心输出一次；每次执行仍进行检查。
如果失败，不应显示伪造的推理成功或静默回落其他处理器。

保存完整串口日志，可在开发机运行：

```sh
python3 tools/analyze-voice-latency-v210.py serial.txt
```

它只计算 capture-stop→driver 的软件代理值和阶段时间，不宣称真实结束说话→
扬声器起音。3–5 秒目标必须另用实拍/同步采音确认，包含端点等待及推理积压；
预加载、第一次冷运行、后续热运行要分别记录，不用固定前缀或空音频冒充回复。


## 5. v211新增观察项

ASR会话关闭时的 `asr-v211` 行报告 received、staged-bytes、
qualified-batches、tail-frames；正常结束应排空 staged-bytes=0。
实际样本数和识别正文不能因为优化而减少。
在相同原始WAV/上下文/预热条件下对照v210，不能用不同说话长度比较。

LLM完成后可执行 `aipetllm cacheinfo`，查看 `llm-scratch-v211`；
复用计数会增加，工作内存不超过256KiB。卸载必须等工作池停止。
TTS内保留371200字节工作区，结束引擎会清零释放；不是模型权重泄漏。
首次真实PCM起播门槛40ms，补缓冲仍100ms，不能据此宣称墙钟必省60ms。
软件阶段解析继续用 `tools/analyze-voice-latency-v210.py`，其适用边界不变。
