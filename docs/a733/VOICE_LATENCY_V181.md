# v181：ASR / LLM / TTS 延迟优化候选

2026-10-25；基线 `7c884c0`，保留 v178–v180、原始测试记录与通过版本。
本版是**软件验证候选，不是离线语音全链路验收通过版**。不要求用户刷中间 SD 卡。
没有连接实机，没有改变 UFS、STM32、摄像头、时钟频率、NPU MMU 或全局 DMA 配置。

## 本版成果与证据边界

| 部分 | 落地内容 | 已验证 | 尚未证明 |
| --- | --- | --- | --- |
| LLM | 默认4 token批量prefill，串行最终prompt位置和decode | MiniCPM5/Qwen全logits、历史/KV一致；主机约13.5%提速 | A7Z首token提速幅度 |
| ASR | 16k PCM边接收边fbank/encoder/greedy，尾部严格对齐 | 真实CPU ONNX三条音频的fbank、全部逻辑输出和token/位置完全一致 | 目标通用ASR NPU provider、实时麦克风完整链路 |
| TTS | 连续22.05→48k重采样、独立播放线程、短句流水线 | 分块输出与原整段转换逐字节相同；模拟播放/取消/欠载回归 | RTOS Matcha声学执行器、真实NPU vocoder连续声学验收 |
| I2S | 同步FLAT流式TX、10ms live capture回调API | 实际TX函数模拟MMIO、ARM编译、旧音频回归 | 新流式接口板测、端点检测/采集队列集成 |

完整目标为“用户说完 → 首段有意义的可听回答”3–4秒，不包含冷启动和用户讲话时长。
目前**未达到/未验收**。`first-visible`、`first-phrase`、`first-pcm`、
`first-driver-pull` 都不能冒充扬声器首音频时间。主机时间也不能代替 A7Z 时间。

## 本地构建封存

实现提交`f8ee31e`。独立完整目标构建2997项通过，132个关键源码摘要与实际staging
一致，结束后SDK241条路径逐字节恢复。12个修改单元使用真实ARM64/NuttX命令编译，
最大静态函数栈帧2384B（非整条调用链峰值）；新增线程另设32/64KiB栈，仍需板测栈水位。
链接内存布局检查通过，文件2813480B、运行时范围37470208B。
内部封存：`out/v181-voice-latency-20261025/{Image,nuttx.elf,build.config,manifest.json}`。
Image SHA-256：`bb18194d9f5d9f311bc1cc35754a6e91de4c014d6aafa818ac06b07e06a672ee`。
这不是完整SD镜像，不包含新增目标通用ASR/Matcha provider。
流式ASR核心已有单元/真实CPU模型/ARM编译证明，但尚无产品provider引用，
最终ELF会GC该未使用核心；不能因为CMake编译了它就宣称板上ASR已接通。

## LLM

`aipetllm_prefill.inc`按层计算最多4个新增prompt token，量化矩阵同一权重行
服务多个输入；工作线程一次派发整批。各输入仍独立Q8_K量化、保持原有归约顺序，
因果attention不能访问未来位置；最后一个prompt位置与后续生成保持串行。
无模型权重精度、模板、RoPE或量化格式变化。保留 v180 的有界共享设备上下文。

热模型、39 prompt tokens、生成1 token、每轮清KV的主机交替测试：

| 顺序 | width | 秒 |
| --- | ---: | ---: |
| 1 | 1 | 2.623991498 |
| 2 | 4 | 2.262958755 |
| 3 | 1 | 2.577534551 |
| 4 | 4 | 2.238502592 |

平均2.60076→2.25073秒，约13.5%；每轮logits完全一致。这是主机对照，样本很少，
不能外推板上性能/稳定分位数。逐token生成速度没有因此获得同等改善。
权重驻留仍避免重复读SD，`clear`不卸载688MB模型。

未来统一板测时再运行（旧镜像不支持batch）：

```sh
aipet audio output off
aipet local on
aipet local preload
aipet local context 0
aipet local batch 1
aipet local clear
aipet local ask "你好，请简单介绍自己"
aipet local batch 4
aipet local clear
aipet local ask "你好，请简单介绍自己"
aipet local batch 1
aipet local clear
aipet local ask "你好，请简单介绍自己"
aipet local batch 4
aipet local clear
aipet local ask "你好，请简单介绍自己"
aipet local context 3
```

batch设置仅RAM生效，支持1/4，默认4；忙时拒绝。不因切换batch自动卸载权重或历史。
必须每轮clear，避免不同前缀复用量污染A/B；随后恢复context3。

## ASR

`voice_asr_stream`消费16k mono S16，最长8秒，103×80 fbank输入、96帧推进，
每次encoder返回24×512，由backend独占35个跨块状态；不是旧KWS的39输入/263词表。
参考音频以100ms分块送入，首次encoder在累计1.1秒输入时运行，录音结束前已有结果。
尾部补的是**PCM静音**，不是数值为0的log-mel；chunk start不越过真实语音帧末端。
状态输出、特征哈希和解码token/位置逐块对照；silence不产生转写。
取消/非有限输出/缺失输出/错误都是sticky，finish成功之前不发布最终识别。

真实CPU参考结果：帮我打开灯、现在几点了、3秒静音，全部一致。
这验证算法与流式调度，不证明目标NPU执行、native layout、时延或麦克风声学质量。

新增I2S live capture只做10ms mono S16回调，可请求完整wire frame处提前停止，
继续使用已通过的左槽、x5增益、S24转换、计数器/时长/FIFO校验、录音期间DOUT静默。
回调只能快速复制RAM，不允许文件IO、NPU等待、重DSP或阻塞。最终ioctl失败时，
消费过的分块仍需整体作废；不能触发LLM/动作。新API还没有接端点/VAD与采集队列。

## TTS与重叠处理

- 320/147有理重采样，32tap Hann窗sinc，320相位系数预计算一次；保留连续相位，
  不在每个vocoder块边界重新起算。16输入sample前瞻；48k输出10ms块。
- 长度1/15/16/17/2205/51200/110250/220500样本，分块1/147/512/51200，
  与旧整段WAV转换逐字节一致。不是靠改采样率标签或重复采样冒充提速。
- 3秒有界PCM环形队列、100ms初始预缓冲、独立32KiB栈播放线程；播放开始后
  读回调只读RAM，不等待模型/磁盘。若生产赶不上播放，明确EPIPE并停止，
  不注入伪静音、不忽略FIFO错误、不宣称“无缝”。10秒音频/120秒任务上限。
- LLM短句队列4项，句号/问号等分句、足够长的逗号或最多32字强制边界，
  合成线程64KiB栈；首句可与后续LLM生成重叠。严格UTF8，拒绝role/tool/JSON标记，
  仅显式纯文本模式，禁止动作/MCU/云fallback。已播出的片段不可回滚。
- 新命令`aipet audio local-speak-stream "文字"`、
  `aipet audio local-dialog-stream "问题"`；旧整段local-speak保留。
  **当前local provider未安装，新命令会明确ENODEV，不能用于完整语音演示。**
  只有真实provider就绪且I2S已选择/未静音，才可启动该路径。
- 文本前端增加有限算式、负号、小数、百分号、℃、有效ISO日期归一化。
  非完整中文TN/FST，数字按位读，10月可能读“一零月”；非法日期/多小数点/OOV拒绝，
  不静默丢词。词典、Baker模型授权条件保持不变，不分发权重。

## 尚未完成的硬门槛

1. 通用ASR三个NBG的native IO布局、动态输入和完整golden输出对照，适配prepared
   执行/provider。现有encoder39/39的KWS和263词joiner不能顶替6254词通用ASR。
   新encoder约41.2MB、native tensor约4.13MB，不能直接塞入24MiB prepared/2MiB池，
   更不能盲改全局MMU覆盖已通过YOLO/语音模型。
2. Matcha动态声学图的RTOS CPU执行器仍缺失；Windows ORT和Linux共享库不能
   直接放进NuttX。目标计划为CPU声学模型＋NPU vocoder，不是整段TTS都在NPU。
3. live capture有界队列、48→16k连续转换、端点/VAD与已资格化ASR provider接线；
   新IOCTL经过软件检查不等于板上FIFO/声学验收。
4. 合成吞吐可能低于播放速率，100ms缓冲不是万能解；要用真实模型记录RTF、
   underrun及最差耗时再选缓冲。长首句/更多上下文也会增加首音频延迟。
5. 最后一次统一板测需记录capture_end、asr_final、llm_first、phrase_first、
   tts_first_pcm、I2S_first以及真实声学录音，对20次以上求中位数/P95；
   不给预录“好的”占位音频，不用CPU oracle代替NPU链路。

## 复现与恢复

在仓库根目录用WSL执行；只读现有本地模型，不联网：

```sh
bash tools/build-voice-stream-v181.sh out/voice-stream-v181-new
python3 tools/test-i2s-stream-v181.py
bash tools/test-aipet-routes.sh
bash tools/test-aipet-audio.sh
bash tools/test-aipet-mcu.sh
python3 tools/check-a733-voice-arm-v181.py out/voice-arm-v181-new
bash tools/build-a733-voice-v181.sh out/voice-build-v181-new
```

真实ASR CPU oracle使用本地Windows ORT包及WSL bridge：
`tools/npu-voice/test-asr-stream-v181.py <新输出目录> --build <上面的voice-stream目录>`。
依赖路径同v179；不得把这些主机依赖加入固件。每次输出使用新目录，不覆盖既有证据。

本版证据保留于`docs/evidence/voice-latency-v181/`；完整日志、内部内核、
ELF与配置位于`out/`，具体清单见该目录summary。内部Image不是完整可烧写SD镜像。
独立SDK build为`cubie-a7z_nsh_v181_voice_latency`，构建前后逐路径验证SDK字节恢复。
最终tag `a733-v181-voice-latency-20261025-r2`，同名bundle/源码归档与
checkpoint保留；恢复仅到新目录，不覆盖当前仓库、模型或SD/UFS。
首个无r2标签保留作历史快照；r2只补充证据换行字节保护，不改固件。
