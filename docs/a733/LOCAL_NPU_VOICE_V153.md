# v153：新版 Model Zoo 审查与本地语音特征前端

2026-10-19。延续 [v152](LOCAL_NPU_VOICE_V152.md)，不删除历史记录、
通过镜像和恢复标签。本阶段使用 embedded-npu-porting / embedded-soc-porting
分级验证：CPU 前端、NPU 执行和应用语义分别验收，不将编译通过冒充全链路。

## 新资料

用户提供 `1789038584543_awnpu_model_zoo-v1.1.0-20260910-675a8dcb.tar.gz`，
SHA-256 `7023c82789956db0ee7cab31c6c0a2c4bcdcc274a7f6c5a7d3fd4260186aa0be`。
WSL tar 完整解到工作区 `references/awnpu_model_zoo-v1.1.0-20260910-675a8dcb`；
Windows tar 首次因中文文件名失败，不以那次部分解包为依据。

- 有 `examples/zipformer` 源码、转换配置、Kaldi 前端、NPY 校准集、test.wav、tokens.txt。
- **没有 ASR encoder/decoder/joiner 的 ONNX 或 NBG**；`model/` 仅 WAV 和词表。
- 没有 `examples/tts`；v152 另备的 KWS float/vocoder int16/Matcha ONNX 仍保留。
- ASR 编码器36输入（x + 7组×5状态），输入103×80，offset96，输出24×512；
  KWS 是另一套39输入、29×80、offset16、输出4×320。不能混用模型/词表/状态。
- ASR 词表6257项中，最后 `#0/#1/#2` 为6254..6256；joiner6254输出
  不应不加区分按6257维解码。实际输出布局/量化/缓存均待 Linux golden 验证。
- 源码的 NetworkItem、超时/错误处理、整数状态回灌不能直接照搬进 RTOS。

### 后续补齐的上游源图（不是 NBG）

已从 Zoo README 引用的
[k2-fsa 上游](https://huggingface.co/csukuangfj/k2fsa-zipformer-bilingual-zh-en-t/tree/e2382758de9a0219b4efe682b95af30b399db3b8/exp/96)
固定提交 `e2382758de9a0219b4efe682b95af30b399db3b8` 下载三份 float ONNX，
合计115187227字节，LFS SHA-256逐份校验。保存在仓外
`references/zipformer-asr-upstream-e238275`，不冒充厂商改图或已转换NBG。
静态解析已确认 encoder36入36出、`x=[N,103,80]`、chunk96；decoder输入是
INT64 `[N,2]`，joiner6254输出。解码器校准使用 `dataset.txt` 内10个NPY，
不是按输入名寻找 `y.npy`；整数输入和NBG预处理后的类型不能混为一谈。
完整接口/类型/状态与校准形状见 `asr-source-static-audit.json`。
没有运行 ONNX Runtime、没有载入 external tensor data、没有NBG转换或NPU推理。

可复现：`python3 tools/npu-voice/fetch-asr-source.py --download NEW_DIRECTORY`；
中断后 `--download --resume DIRECTORY` 只跳过已核验完整文件，绝不复用/覆盖.part。
`--verify DIRECTORY` 完全离线校验。第一次joiner在建立响应时超时，前两模型保留；
重试后第三模型通过，不把超时当作损坏权重或更改哈希。
静态审查需主机 onnx1.17.0/protobuf5.29.5/numpy；不加入目标固件。
Windows长路径导致首次依赖复制不完整，随后在工作区短目录 `.hostdeps-v153`
安装成功，非系统Python修改。

## 新实现

移入 Apache-2.0 的 Kaldi-native-fbank 子集（保留许可证与 Ooura FFT 单独许可），
来源及每个原始文件的 SHA-256 见应用 `third_party/kaldi-native-fbank/PROVENANCE.json`。
删去无关 MFCC/Whisper 模板；日志兼容无 C++ 异常，保持内部检查开启。数学算法不改。

第一次目标构建暴露 libc++ 未启用 localization，厂商 option dump 的 iostream
无法编译。已保留该次失败日志与161路径SDK恢复记录（attempt1），随后增加
`knf-port.h/rtos-log.h`：NuttX只用stdio日志并省略选项/滤波器调试文本，
不打开全局locale/iostream。数值计算不分叉；主机另测该无异常、无iostream分支。

`voice_fbank.hxx/cxx` 接受 mono S16LE/16k，按 `/32768` 归一化；
80维 log-power fbank，25ms Povey窗、10ms步进、preemphasis0.97、去直流、
512FFT、20..7600Hz、dither0、snip_edges=false，与下载的 Zipformer/KWS 配置一致。
接口支持分块接收、完成、范围检查及逐帧复制；单实例单所有者，最多10秒/1000帧，
分块临时缓冲320 float。不是持续无限流、端点检测或识别器。

应用命令 `aipet audio features [100..10000毫秒]`：
ICS43434既有48k mono/x5采集 → 既有48k→16k FIR → Kaldi前端 → 帧数/范围/和。
不要求 Agent 初始化、不调用网络/ASR/TTS/NPU、不保存录音、不修改音量与后端配置。
使用既有操作锁与 Ctrl+C 延迟清理，不使用板级诊断节点。
`record/asr/listen/speak/play/play22k` 的原语义不变。

内存限制：NuttX 当前关闭 C++ exceptions/RTTI；标准容器 OOM 可能 abort。
输入有界不等于已具备可恢复 OOM；主机异常捕获不代表 RTOS 分配失败恢复验收。
PCM 临时缓冲沿用擦除，但不承诺第三方容器释放前安全擦除所有音频残留。

## 验证与构建

```sh
bash tools/test-aipet-fbank.sh
bash tools/test-aipet-audio.sh
python3 tools/test-local-voice-audit.py
python3 tools/audit-a733-local-voice.py \
  --reference ../references/cubie-a7a-voice-assistant-main \
  --zoo ../references/awnpu_model_zoo-v1.1.0-20260910-675a8dcb
# 预期返回2：NPU语音尚不完整，不代表包校验失败。
bash tools/build-a733-local-voice-v153.sh
```

主机测试覆盖真实 DSP、ASan/UBSan、无异常/RTTI模式、逐样本长度边界、
整段/97样本分块逐浮点完全相同、直接调用参考类的幅度/特征一致、静音log floor、
变频输入差异、重复flush、容量/范围错误、10秒上限，以及未初始化/取消后无上传和不覆盖录音。
这只证明移植前端和应用控制流，不是独立 NPU golden 或语音识别率。

构建使用独立 `cubie-a7z_nsh_v153_local_voice_frontend`，临时部署 overlay 后
恢复官方 SDK；日志位于 `docs/evidence/local-voice-v153/`。最终结果见该目录摘要。
不覆盖 v150/v151 镜像，不发布名为“离线 NPU 全语音通过”的镜像。

## 下一道门槛

2026-10-19 补充：用户以另一张卡运行官方 Linux，已回传4模型接口查询和
KWS joiner 两次零输入（253/269 us）完整记录；输出1052字节逐字节相同。
原始回传位于工作区仓外 `Linux测试-Npu测试文件/`，未删除/覆盖。
[接口、库/驱动版本、资产及输出哈希、第二阶段步骤](../../tools/npu-voice/LINUX_JOINER_CHECK.md)。
第二阶段随后回传 `voice-report-EviWxFpk.tar.gz`，两路合成输入分别影响joiner输出，
重复encoder向量/零输入结果逐字节一致，与首测零输入基线相同；原始输出重新校验通过。
第三阶段已备好 [真实语音 KWS 状态/音素诊断包](../../tools/npu-voice/LINUX_KWS_CHECK.md)，
38路状态完整回灌、feature-offset标量策略明确、两个fixture每组12块、约221次原生调用。
前端为主机用同一v153代码生成，不是现场麦克风采集；贪心音素不是关键词beam或中文ASR。
第三阶段实际板测待回传；本次没有新固件或全链路识别验收。

第三阶段随后回传 `voice-report-rAyPoGAl`：221次进程/218条实际推理记录，
全原始张量CRC/哈希、39路首块重复、状态回灌、decoder上下文和joiner连接只读复核通过；
语音/静音各48帧均blank，语音非blank候选最高0.2518，未认定关键词识别通过。
用户要求合并其余测试，已备好 [Linux综合包](../../tools/npu-voice/LINUX_VOICE_ALL.md)，
含24场景矩阵、暖态重载、非法输入拒绝、真实CPU声学mel到NPU声码器和待听音WAV。
实际综合板测待回传，ASR缺NBG及openvela多张量执行器未完成的事实不变。

现已备好上游ASR三份源图，需要核对厂商改图并使用新版 Zoo 配置转换，随后在官方 Linux A733 下
对 KWS、ASR、vocoder 采集完整 native tensor/quantization/padding 与逐帧状态回灌 golden。
当前 openvela A7PM 仍限4输入/8输出/16区域、单文件动态输入、输出CRC/短预览；
不能直接执行 NBG/39路缓存。后续须设计完整张量执行ABI并验证超时/恢复，
Matcha CPU Runtime、文本前端和本地唤醒/ASR/合成串行所有者均未完成。
离线命令不得静默调用已有云 ASR/TTS。

## 后续实测与搜索修正（2026-10-19）

本文历史记录保留；本节为最新状态。
综合回传 `voice-report-fXyxidmo.tar.gz` SHA-256
`46ccbec1c7ef48a716e93d4f52a69087f29138051fb9343694e806f3c087498e`，98983483字节。
1939次原生进程调用完成；1930条实际推理轨迹、33519个输入/输出张量哈希复核一致；
4模型各10次暖态及两次独立进程重载的所有输出一致。
热态调用中位数 encoder26678us、decoder627us、joiner315us、vocoder57192us，
不含模型加载、文件往返和整条链路。非法一字节输入在运行前拒绝，之后正向任务继续成功；
这不是硬件挂起复位恢复/长时间内存泄漏认证。

归一化/feature-offset组合下“你好小瑞”命中；“小瑞”“小瑞小瑞”漏检，
命令/噪声/静音负样本无命中；其余3个幅度/状态组合的3条正样本均漏检。
不能把24场景收集成功当作准确率通过。

用户已分别试听“你好”“测试成功”，确认音频内容完全正确。
声码器原始输出SHA-256分别为
`39318c27c3d9378dce74e006f5912af8fc88d3830dee0de98cd468437187274b`、
`6e8ce4218fddfe1db250dd92c28c88e48ff4a803e8d8d92afab02f7df3f6a5a0`。
仅认证“开发主机CPU声学mel→Linux A733 NPU声码器→正确WAV”，
不认证声学模型已在A7Z运行或openvela实时TTS链路。

后续已加入独立有界C搜索核心，保持词条boost/threshold、不使用旧额外+5，
支持多路径、过期重启、短长词前缀等待。只加入应用构建清单，未连到openvela NPU。
详见 [v154搜索说明](../../tools/npu-voice/LINUX_KWS_SEARCH.md)。原固件/镜像、SDK与原始回传不改。
