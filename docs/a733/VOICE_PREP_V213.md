# v213：MIC ON前准备时间优化候选

2026-10-30。保留v212镜像和`a733-v212-voice-partial-pass-20261030`恢复标签。
不修改模型、NPU ABI/MMU/DMA预留、I2S时钟/接线、摄像头或全系统调度。
本页描述候选实现，不代表新版本实测速度或完整闭环验收。

## v212实机基线

附件`1538844a-e99c-4f04-a38a-3fd5c67138b3/已粘贴的文本.txt`：

- stream-echo 5秒：status=0，输入240000/输出80000，回放240000帧。
- general-stream 8秒：ASR准备10946119us，正确识别“一加一等于几”。
- offline固定8秒：LLM热预热297600us，ASR/vocoder准备11017076us；录音/消费失败-75。
- offline三轮：LLM热预热297800us，准备11023131us；只有第二轮有非空转写“加一等于几”，
  LLM回复“加一等于1。”并播放，其余两轮无转写。session result=0不等于三轮均成功。
- 非空轮停止采集781414370us、转写完成782262903us、首driver pull786731016us；
  差值5.316646秒是软件代理值，不是实测停止说话到扬声器起音，且不能忽略漏字/回复质量。

MIC ON前的主要已测等待是每次命令重新prepare的约11秒，不是硬件麦克风上电本身要11秒。
旧prepared日志合并ASR与vocoder，没有足够信息将这11秒全部归因于文件I/O或CRC。

## 实施

1. ASR打开时的词表、三个client查询/分配、模型读取和校验进入CPU6/A76作用域，
   使用原有双身份/迁移确认；结束恢复原亲和性，失败不开放麦克风。
2. TENSOR_PREPARE在私有CPU-only快照读完一个LOAD区域后，改用已存在的raw IEEE CRC
   dispatcher计算全部区域字节，替代通用file helper中的libc CRC。
   不重复读取文件，不删除任何区域/输入CRC校验；不支持硬件指令时仍软件回退。
   未改其他模型加载器的通用文件函数，不增加缓存预算，不跨NSH task group持有fd。
3. asr-prepare-v213拆出词表、每个role的query/DMA分配与prepare时间；
   voice-prepare-v213拆出ASR与vocoder时间，明确此时MIC仍关闭。
4. live-capture-v213在采集失败后打印driver/consumer状态、帧数、队列读写位置。
   只在停止采集之后输出，不在实时FIFO轮询中增加串口日志。

第一次冷启动仍需真实加载。命令间仍关闭NPU fd，不能用跨任务复用fd换取启动速度。
同一offline命令中的后续轮保留原有模型复用和全部35路跨块状态/轮间清零。
本候选**没有宣称修复-75或漏字**，没有删掉FIFO错误、丢弃积压音频或把MIC ON提前打印当作优化。
后续需要根据新的失败阶段和`cat /dev/a733-audio`定位硬件RX溢出/消费调度问题。

## 上板对照

保持这次已成功的接线和模型目录，先结束其他摄像头/NPU/播放器任务：

```text
aipet audio output i2s
aipet audio volume 50
aipet audio gain 5
aipet audio general-voice /data/npu/general-asr /data/npu/general-tts
aipet audio general-stream 8000 0
```

与旧约10.95秒对照`asr-prepare-v213`/`voice-prepare-v213`；看到MIC ON再说同一句话。
再准备已有本地模型并测试：

```text
ufs start
ufs status
aipet local on
aipet local model auto
aipet local batch 4
aipet local context 3
aipet local preload
aipet audio offline 1 8000 0
cat /dev/a733-audio
aipet audio offline 3 8000 400
cat /dev/a733-audio
```

UFS已经ready=1时不重复start；所有准备失败先停，提示重启时不强行复用。
记录全部日志和每轮实际转写/听音；400仍是能量阈值，不是400ms。
不能只凭一次有声音就认定端点/连续多轮/3–5秒达标。

## 复现入口

```text
bash tools/build-a733-voice-v213.sh out/NEW-v213-build out/ort-static-v185-r2
python3 tools/test-voice-v213.py out/NEW-v213-tests --build <本次SDK构建目录>
python3 tools/check-a733-voice-v213.py --build <同目录> --build-report <本次build报告> --output out/NEW-v213-full --firmware-commit <实际源码提交>
python3 tools/package-a733-voice-v213.py <本次封存目录> ../openvela-a733-cubie-a7z-sd-v213-voice-prepare-candidate.img out/NEW-v213-package --firmware-commit <同提交> --voice-gates <语音summary.json> --full-gates <完整summary.json>
```

## 本轮完成状态和候选制品

固件源码提交`4d2bb0ffa078db0ee152086276f79943049736a6`。
15组语音回归、16个SDK/ARM编译单元、22组摄像头回归、最终ELF布局/ISA/ABI检查通过。
包括实际runtime adapter的20组模拟文件/硬件测试、损坏payload拒绝、prepare失败清理、
39路tensor与状态回放、35路ASR跨块状态、超时/取消/保留所有权和录放回归。
这些是主机/ARM编译或既有捕获重放，不是新模型在板上执行的证明。
完整构建wrapper=0，SDK298路径逐字节还原；构造器24个，runtime end=0x45de7000。

新镜像：`../openvela-a733-cubie-a7z-sd-v213-voice-prepare-candidate.img`

- 大小：2147483648字节。
- SHA256：`9850b49225e54c64f5b1ea54955bfb3d36b0a35cc4998f7114ea14c0eaf53cec`。
- 内核7164552字节；SHA256：`0fb2b1d0944af4f0a4e17c5b1fde819d80eda4ff1d1a2afe3bdc6082d985ecf9`。
- 原生Windows SHA256与打包器一致；GPT、ext4、脚本和内核回读、49项私有资产及3条标准WAV回读校验通过。
- v210封存普通镜像作受控打包基底；v212修复源码和SWOUT1护栏全部保留。
- 新U-Boot脚本为camera-v213；驱动仍可能打印v212、早期启动仍有boot188等历史标签，
  用`asr-prepare-v213`/`voice-prepare-v213`区分此轮，不因历史标签误判刷错。
- 不写物理SD/UFS、不改变模型/接线、不覆盖v212镜像。整卡烧录前备份SD个人文件/配置/模型。
- 封存在`out/v213-release-20261030`；报告在`out/voice-build-v213-r1`、
  `out/voice-regression-v213-r1`、`out/voice-full-v213-r1`、`out/voice-package-v213-r1`。
- 原始文本证据另存`docs/evidence/voice-v213`，包含本轮v212实机基线；不含模型或录音二进制。

实际启动快多少、-75是否再次出现、识别漏字和连续多轮稳定性仍待本镜像上板验证。
