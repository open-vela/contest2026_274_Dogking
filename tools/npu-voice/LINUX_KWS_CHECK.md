# Linux 第三阶段：真实语音 KWS 原生张量诊断

这不是完整 ASR/TTS 交付，也不修改 openvela 固件。Linux 第一/二阶段已通过
原生接口、joiner 零输入与合成输入敏感性/重复性；此包开始执行 encoder/decoder。
仅使用已有本地模型和库，无网络调用、安装、设备权限改写、NPU 复位或重启。
厂商库/模型/参考音频仅用于本用户本地板测，仍在 Git 外，不应直接再次分发。

## 使用

用官方 Linux 卡启动，关闭其他 NPU 程序。解压进入包目录后：

```sh
sudo bash test-linux-kws.sh --run --npu-idle-confirmed
```

不需要说话、接麦克风或设置 API key，使用包内 `wake_nihao_xiaorui_16k.wav`
参考语音与同长度静音。两组都要跑完；本轮调用比前两轮多，结束时会打印 `REPORT:`。
把对应的 `results/voice-report-*.tar.gz` 整包发回，包括报错时生成的包。
不要只发送最后一段音素，不要反复启动失败任务。

只采环境可用 `bash test-linux-kws.sh --env-only`（无 NPU 调用）。
依赖 Bash、Python3（仅标准库）、GNU timeout、tar、sha256sum、ldd、flock 等基础工具；
fuser 可选，缺少时仍必须手动确认设备空闲。可信私有包可用 sudo；未知包不要以 root 执行。

## 实际执行与验收边界

1. 原 7 个原生资产 SHA-256 校验不变；另核对完整第三阶段文件清单、词表、
   前端特征、固定 I/O 合同。再次查询三模型的实际接口并精确比对。
2. 开发主机从真实 16k/mono/S16 WAV 生成特征；同一个 v153 `voice_fbank.cxx`
   和项目保存的 Kaldi 前端，不是另写近似 FFT。S16/32768，80 bins，25ms/10ms，
   Povey、dither=0、snip_edges=false、高频7600Hz，末尾统一追加320ms零样本并 finish。
   完整输入音频/PCM/特征哈希、前端日志与导出程序哈希保留。不是实机实时前端验收。
3. `29x80` 特征窗，每次前进16帧；encoder输出 `4x320`。所有39路输入/输出
   必须 FP32、无量化、字节数和维度匹配首次板测合同；未知布局直接拒绝，不猜转置。
4. 首次38路状态均为零；每次原生输出1..38按字节传给下一块，对齐/尺寸不截断。
   标量38采用参考源码默认的 **feature-offset**：每块覆盖为 `0,16,32,...`（FP32）。
   原始模型标量输出始终记录。库中还实现明确的 feedback 策略供后续对照，
   但本包装器默认不启用。不能把两种含义认定为已证明等价。
5. 每组首块额外原输入重复一次，比较全部39路输出字节；不同则停止。
6. 每个encoder输出帧调用 decoder（两 token 浮点输入）和 joiner，
   做每帧最多一个符号的贪心诊断。blank=0、unk=2不进入上下文；
   非blank token保留重复，不能做CTC式简单去重。
7. 保存所有块、所有模型的完整输入/输出文件及哈希、CRC、运行调用时间、
   标量状态、逐帧上下文、top1音素与概率、最终音素序列。真实语音与静音的
   encoder输出序列必须有差异，否则停止诊断，不能称正确识别。

**输出是 KWS 音素，不是中文 ASR 文本。** 贪心诊断不是参考工程带关键词偏置的
beam/trie算法，不冒充“你好小瑞”唤醒已通过；参考工程自己也记录“小瑞”尾音素
偏弱、需要CPU对照/标定。全blank、未命中或错音素都要回传，不能靠加boost伪造模型准确率。
后续须依据原始输出选择标量策略，核对关键词解码、ASR独立模型和CPU/NPU一致性。

## 超时、权限与数据

每个原生进程60秒，TERM后5秒KILL；网络内设置5秒超时。
Python调度总预算1200秒/1000次调用，停止时额外有界清理宽限；每次重新加载网络，
不是持久网络暖态基准，不能用其总耗时宣传ASR RTF。
SIGINT/SIGTERM时尽力终止当前原生进程组并归档；杀进程不是硬件复位，
如果Linux驱动不可中断等待，用户态不能承诺恢复。超时/挂起后保留日志并冷重启再判断。

结果 `COLLECTED_REAL_KWS_GREEDY_DIAGNOSTIC` 只表示本轮张量采集与检查完成。
不代表关键词准确率、通用ASR、TTS、麦克风实时链路或openvela端NPU通过。
不执行 vocoder（包内仍保留它以满足前两阶段统一资产锁），不播放数据。
结果归档按既有脚本生成独立目录；sudo时只将归档所有权交回登录用户。
输入参考音频可听且将随日志回传；不要把自己的私密录音直接混进此固定测试包。

## 可复现构建与测试（开发主机）

```sh
bash tools/test-aipet-fbank.sh
python3 tools/test-linux-voice-script.py
python3 tools/test-linux-joiner.py
python3 tools/test-linux-kws.py
# 已有上一阶段的固定 Bullseye 原生二进制与合法本地参考模型/库后：
bash tools/npu-voice/build-linux-kws-kit.sh /absolute/path/to/out/NEW_KIT
```

构建只在开发主机运行CPU特征导出，不调用板端NPU。只接受新目录/新归档，
保留host-build导出程序与前端记录，不覆盖旧通过包。
`kws-contract.json` 来自用户第一阶段实际三模型接口日志，名称不用于猜状态映射，
固定模型 SHA-256 与逐index合同共同约束它。模型/库版本变化需要重新审查。
