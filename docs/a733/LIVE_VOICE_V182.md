# v182：流式语音传输镜像候选与实机门槛

2026-10-25；基线为`a733-v181-voice-latency-20261025-r2`（`16266f4`）。
继续保留v178–v181、旧镜像、所有开发记录。本轮不改NPU MMU/保留区、UFS、
摄像头、STM32或I2S时钟/引脚配置；新增是I2S应用层流式消费与诊断入口。

## 为什么这里需要上板

v181的流式I2S接口此前只有模拟MMIO/ARM编译，没有真实FIFO与调度验收。
v182将它接成可执行目标端命令：必须验证麦克风采样持续性、RX无丢帧、
消费者并发调度、停止/取消、半双工扬声器连续播放，之后才适合接真实ASR推理。
不能用主机的微秒时间当成实际收音耗时，也不能仅靠编译跳过这道门。

**本镜像不是完整离线ASR→LLM→TTS交付版。** 通用ASR新NBG约41.2MB、
native I/O约4.13MB，仍缺Linux厂商运行时动态音频/35状态/完整输出及准备布局证据；
现有24MiB prepared区/2MiB tensor池不能直接装下，也不能用263词KWS代替6254词ASR。
Matcha RTOS CPU声学执行器仍缺；`local-status`保持not-installed是正确状态。
这轮没有安装伪provider、预录回答或把云服务改名成本地。

## 实现

- `audio_capture_live`：单生产者/单消费者有界802块队列，每块最多10ms/960B，
  release/acquire发布；RX只复制RAM、检查取消/上限，处理线程使用64KiB显式栈。
  队列约775KB堆，单次最长8秒；旧驱动仍有自身采集RAM，echo另用最多768KB。
  不在RX回调做文件、特征或模型计算，不丢块后继续报成功。
- 连续63tap Hamming低通，6.5kHz cutoff，48→16k decimation；系数、归约顺序、
  零填充边界和输出舍入与原整段路径一致。31个输入样本前瞻，保留跨块状态，
  结束后只补真实FIR尾，不在块边缘制造脉冲。
- 对实际生产batch FIR逐字节比较，覆盖3/30/96/4800/48000/384000样本，
  1/147/480/511样本任意分块。ASan/UBSan通过；不是重复实现的数学oracle。
- 独立线程边收边做Kaldi fbank，回传输入/输出样本、特征帧、queue-peak、
  first-pcm16和elapsed。所有这些是CPU DSP/传输指标，不是ASR或首音频回答。
- 端点是可选**诊断能量检测**：x5后的16k S16 RMS默认400，30ms连续活动，
  活动后500ms静默请求结束。非声学VAD模型；风噪/DC/音乐可误判，不触发动作。
  静音结束返回ENODATA；消费者滞后时实际结束可能晚，必须检查queue-peak/elapsed。
- capture成功还必须满足驱动计数/时长/FIFO证明、消费者排空、FIR尾、48/16k
  计数一致；中间数据是provisional，错误不能触发LLM/电机或被发布为转写。
- echo只在全部RX证明通过后、麦克风已停止时播放真实RAM录音；走新stream TX，
  与配置音量联动，不生成WAV、不联网、不调用ASR/LLM/TTS。无声学回授录放同时运行。
- 120秒协作截止时间；回调必须自行约束计算等待，不能靠外层deadline中断任意阻塞。
  join异常保留潜在在用队列，避免UAF；不建议SIGKILL或task_delete强杀。
- 原record/asr/listen/旧整段本地路径与默认provider状态保留；本版无开机自动录音。

## 新镜像与风险

实现提交`e837aed`。独立完整构建2998项通过，SDK243条路径逐字节恢复；
两个修改单元按真实NuttX/ARM64命令编译，最大静态函数帧688B，live单元192B，
不是整条调用链峰值。布局检查：内核文件2817576B、运行时范围37474304B。
内核SHA-256：`98788e66f033b4fe6adde0b787dffea08dd857658cbd67365de8bb4812e1a426`。
主机测试、模拟FIFO回归及实际内嵌内核检查的证据保留在`docs/evidence/live-voice-v182/`。

镜像候选：`../openvela-a733-cubie-a7z-sd-v182-live-voice-candidate.img`，2GiB。
整卡SHA-256：`0bb4bae686bb7132f256eee89aae7da12dd78a50c78112445a33099efb4e88c6`。
具体SHA256、内核、ELF、配置封存在`out/v182-live-candidate-20261025/manifest.json`。
仅复制已校验v178整卡模板并替换partition3内核，原模板不改，不触碰实际SD/UFS设备。
新镜像的生成/内嵌内核校验、GPT/文件系统检查不等于实际启动/硬件通过。

**整卡烧录会覆盖目标SD上的配置、录音和模型，先备份。** UFS内容不随此镜像迁移或清空。
GGUF依然单独安装；镜像不捆绑MiniCPM5、不下载模型、不加入用户凭据。
本版不修此前独立的UFS/引导偶发问题；若启动失败，保留串口与当前镜像摘要再回退。
旧v178整卡镜像和v181恢复标签仍保留。

## 一轮实机测试（无需联网/SSH/在线密钥）

接线不变：ICS43434 SCK=PB5/物理12、WS=PB6/35、SD=PB8/38；
MAX98357 BCLK=PB5/12、LRCLK=PB6/35、DIN=PB7/40。模块供电按已通过接线，
合并板VCC不能当成裸麦克风VDD推断；不要带电插拔或互换电源极性。

进入NSH后：

```sh
npu info
npu selftest
aipet audio output i2s
aipet audio volume 25
aipet audio gain 5
aipet audio stream-features 1000
aipet audio stream-echo 3000
aipet audio stream-endpoint 8000 400
aipet audio local-status
free
```

1. features时安静。应有`status=0 input48=48000 output16=16000 fbank=100`，
   实际收音约1秒＋100ms预热，不能几十毫秒结束。queue-peak反映真实处理积压。
2. echo时说话/拍手3秒，随后听约3秒回放；应清楚，录音期间不应有旧电流声。
   预期`input48=144000 output16=48000 fbank=300`，`live-echo status=0 frames=144000`。
3. endpoint时说一短句后停下。应早于8秒返回且`endpoint=1 speech=1`；
   环境噪声大时可不早停，安静不说话返回ENODATA是预期，不能将其当转写成功。
4. 可在features长录音中Ctrl+C，随后重新运行1000ms并再echo，验证可复用。
5. `local-status`当前仍报告缺通用ASR/acoustic provider，不能据此执行完整离线对话。

可将随制品提供的`v182-voice-test.nsh`放到SD `/models/`，NSH执行
`sh /data/models/v182-voice-test.nsh`。脚本只是上述显式测试，不开机自启。
返回**完整串口输出**并说明echo声音/录音期间杂音、端点与取消情况；不需要发密钥。

LLM可同一轮按[v181](VOICE_LATENCY_V181.md)运行width1/4热模型A/B，
先preload、每轮clear、同一prompt、至少交替两轮，结束恢复context3。冷读时间另记。
首token1–2秒、首音频3–4秒都仍需测量，不能根据主机13.5%推断达标。

## 开发者复现与下一步

```sh
python3 tools/test-live-capture-v182.py
bash tools/test-aipet-audio.sh
bash tools/test-aipet-mcu.sh
bash tools/test-aipet-routes.sh
python3 tools/check-a733-live-arm-v182.py out/NEW_ARM_DIR
bash tools/build-a733-live-v182.sh out/NEW_BUILD_DIR
bash tools/package-a733-live-v182.sh out/NEW_BUILD_DIR out/NEW_CANDIDATE_DIR
```

包装器使用新文件名，拒绝覆盖既有候选。源码staging/ARM摘要与镜像一一关联；
失败日志保留，不跳过检查。NPU原生取证仍要在Radxa Linux厂商运行库下完成，
现有`out/a733-linux-asr-contract-v178-20261025-r2.tar.gz`仅inspect/zero smoke，
不能冒充动态ASR golden。之后才能据实际内存预算实现prepared provider、声学执行器，
再进行完整离线语音体验验收。

本地版本标签为`a733-v182-live-voice-20261025`；同名bundle、源码归档和checkpoint
保存。只向新目录演练恢复，不重置当前工作区，也不覆盖之前通过版本。
