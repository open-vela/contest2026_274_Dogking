# v212：时钟纠正后的I2S接线与离线语音测试

2026-10-30。用户纠正PB5/PB6时钟反接后确认扬声器完全正常；
附件`33da1ebb-851a-4cee-933e-4091b225afb7/已粘贴的文本.txt`
包含1kHz提示音和两条完整TTS命令result=0。
短句首PCM=1793112us，长句=2442572us，不是整机首音频时延。
本次不改代码/镜像，不把TTS通过扩展为实时ASR或闭环通过。

## 接线（物理40针排针编号，不是GPIO线性编号）

| A7Z物理针脚 | GPIO | MAX98357功放端 | ICS43434麦克风端 |
| --- | --- | --- | --- |
| 12 | PB5 | BCLK | SCK |
| 35 | PB6 | LRCLK | WS |
| 40 | PB7 | DIN | 不接SD |
| 38 | PB8 | 不接DIN | SD |
| 6 | GND | GND | 共地 |

PB5要将位时钟送到BCLK和SCK，PB6要将帧时钟送到LRCLK和WS。
若模块板内已连通两组时钟无需重复跳线；未证实连通时不能只接功放时钟就假定麦克风也收到。
断电改线；VCC保持本次已验证可出声的正确模块供电，不擅自改变电压。
裸ICS43434不能直接用5V；模块VCC不等于裸芯片VDD。
麦克风SD是数据输出，不是功放SD_MODE。喇叭接模块SPK接口/差分两端，不接开发板GND。

## 1. 初始化和麦克风回放

先确认摄像头、YOLO、其他NPU推理和播放器均已结束；同一时刻只运行一个测试。

```text
echo init > /dev/a733-audio
aipet audio output i2s
aipet audio volume 50
aipet audio gain 5
aipet audio status
aipet audio stream-echo 5000
```

看到MIC ON再说“麦克风测试，一二三”。采满5秒后回放自己的声音。
这是本地RAM录音回放/CPU前端测试，不调用ASR/LLM/TTS，也不保存WAV或上传。
如果录音无声/全噪声，先停在此处排查WS/SCK/SD，不直接进入闭环。

## 2. 配置模型及实时ASR单测

沿用本次TTS已经成功使用的SD模型目录：

```text
npu selftest
ls -l /data/npu/general-asr
ls -l /data/npu/general-tts
aipet audio general-voice /data/npu/general-asr /data/npu/general-tts
aipet audio general-stream 8000 0
```

等MIC ON后说“一加一等于几”，看`general-asr transcript:`是否准确。
8000是固定8秒录音，0关闭能量端点；初始化/识别收尾另计，不是8秒内必定返回。
配置RAM-only，重启后需重新安装；音量、增益、输出配置保存后可跨重启。

如ASR单测失败，可另测标准文件以区分模型执行和麦克风输入：

```text
aipet audio general-wav /data/npu/general-asr/checks/cmd_open_light_16k.wav
aipet audio general-wav /data/npu/general-asr/checks/cmd_time_16k.wav
aipet audio general-wav /data/npu/general-asr/checks/neg_silence_3s_16k.wav
```

前两条应为“帮我打开灯”“现在几点了”；静音无转写（-61）属预期。
如文件不存在，先核对目录，不用空文件代替。

## 3. LLM常驻准备

使用本次已经可加载的UFS模型；ready=1时不需要再启动挂载：

```text
ufs status
ls -l /ufs/models/MiniCPM5-1B-Q4_K_M.gguf
aipet local on
aipet local model auto
aipet local batch 4
aipet local context 3
aipet local preload
aipet local status
```

UFS尚未挂载时先执行`ufs start`再检查ready=1；只用SD模型时改为
`aipet local model sd`并确认`/data/models/MiniCPM5-1B-Q4_K_M.gguf`真实存在。
preload失败时不进入录音；提示需重启时不反复preload/unload。

## 4. ASR -> 本地LLM -> TTS -> 扬声器

```text
aipet audio offline 1 8000 0
```

等MIC ON再说“一加一等于几，请简短回答”；固定8秒后应先打印转写，随后生成并播放真实回复。
通过后测试端点和连续三轮：

```text
aipet audio offline 3 8000 400
```

每轮等MIC ON再说话，依次测试“你好，请简短介绍自己”“一加一等于几”“我刚才问了什么”。
播完再说下一轮，不边放边说。这是半双工、有界会话，不是常驻唤醒/AEC。
400是诊断能量阈值，不是400ms；当前实现连续静音约500ms后结束，需根据真实环境验证。
空转写不提交LLM；无语音时不回复属预期，不能把session result=0本身当作成功对话。
全链路ASR由NPU执行，LLM在CPU，TTS为CPU声学+NPU vocoder；不联网、不下发MCU动作。

## 5. 结果收集

```text
aipet audio status
cat /dev/a733-audio
npu info
free
```

保存从命令到结束的完整日志及实际听感，分别核对ASR正文、LLM回复、完整播放、第二轮重开和上下文。
Ctrl+C用于取消；遇NPU timeout/quarantine/REBOOT REQUIRED等提示，保存日志并按提示重启，不强行复用。
不使用旧的`aipet audio asr/listen/loop`替代这些本地入口，它们包含云端路径。
软件first-PCM/first-driver-pull不能证明结束说话到真实扬声器起音3–5秒达标，仍需实拍/同步采音验证。
