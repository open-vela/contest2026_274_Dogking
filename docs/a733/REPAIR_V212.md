# v212：v209/v210 实机故障修复候选

日期：2026-10-30。保留 v204 通过基线及 v209/v210/v211 镜像、历史文档。
修复前恢复标签：a733-v212-pre-fix-20261030，指向 9d6261f。
本版不改全局调度器、轮转间隔、CPU频率、NPU硬件ABI、模型或UFS数据。

## 实机证据与边界

- v209/v210 的 A76 CPU6/7、硬件CRC与全部六路YOLO输出对照已通过。
  连续管线第一帧成功，第二帧CSI -110；MIPI无错、cleanup=0，最终DMA状态0x83。
- 单次35ms刷屏与CSI的21ms休眠上限冲突；控制kthread又继承CPU6单核掩码。
  这是软件误超时的强证据，但新版本仍须实机确认重复采集和停止。
- 通用ASR两条文件已正确转写、静音-61，不能说NPU整体不可用；
  不能用这两条文件替代任意语音、实时收音和连续会话验收。
- UFS MiniCPM5已经读入688065920字节，用时9.84秒；之后停在首轮预填充，
  不是UFS加载未完成。旧线程池的请求/回复没有截止时间。
- TTS、非流式TTS和提示音均无声。旧日志只有PCM消费/计数，缺少有效幅度及
  播放期间引脚读回。尚不能确定软件修复即可恢复实际声音，不能假定模块损坏。

## 修复

1. CSI不再把成功MMIO的单次墙钟耗时或休眠超过21ms当总线故障。
   仍保留整帧2秒、清理独立2秒、访问/轮询次数、回调错误、冻结时钟、
   DMA/守卫/映射/晚到错误检查。记录max-pause/max-access/delays-over21。
   控制broker恢复为本任务ff掩码，避免意外继承infer/display的CPU6限制。
2. LLM线程创建时显式设置各自单核掩码、局部RR策略与32KiB栈；
   首次任务跨阻塞yield核对身份，热矩阵仍做快速身份复核，不每矩阵休眠。
   请求/回复用单调时钟15秒截止，输出service/slot/tid/phase。
   工作线程只写池内输出缓冲，全部完成才复制给调用方。
   超时不释放可能仍被访问的Q8、权重和输出；拒绝后续任务/unload，要求重启。
   空闲service的事件等待本来就应该阻塞；调用方不再无期限等待回复。
3. I2S每次播放恢复PB5/6、TX映射、slot、位宽、模块时钟和分频，
   PB7只在预填充后回到DOUT；录音/空闲仍保持GPIO低，不撤回防电流声修复。
   播放开始读回PB5/6/7、CTL/CLK/DIV；不匹配就停。保留播放期快照，
   输出PCM samples/nonzero/peak；修正流式音量诊断沿用旧25%的误导。
   TTS同时报告重采样/音量处理前的真实PCM幅度。
4. 离线LLM预加载失败时明确提示麦克风没有打开，不再用-19冒充设备缺失。

## 上板顺序

这是整卡SD候选镜像，烧录前备份SD数据；LLM权重不随镜像分发。
不要同时运行摄像头、语音、其他NPU工作负载；失败后按提示重启，勿强行重试。

先独立验证音频，不先跑摄像头：

```text
echo init > /dev/a733-audio
echo tone=1000,1000 > /dev/a733-audio
cat /dev/a733-audio
aipet audio output i2s
aipet audio volume 50
aipet audio general-voice /data/npu/general-asr /data/npu/general-tts
aipet audio local-speak-stream "你好，语音测试成功。"
cat /dev/a733-audio
aipet audio local-speak-stream "今天的语音测试已经完成，请继续下一步。"
cat /dev/a733-audio
```

需要实际听见声音。tx-proof status=0/nonzero>0只证明软件样本和寄存器读回，
不是实际波形/声学验收。若仍无声，保留报告并用逻辑分析仪采板端PB5/BCLK、
PB6/LRCK、PB7/DOUT：至少24MHz采样、共地。应为3.072MHz/48kHz，
这是期望配置频率，不是已测量值。需进一步查模块电源、DIN/共地等实际输出链。

LLM单独冷启动测试，先确保UFS ready=1和实际模型存在：

```text
ufs start
ufs status
ls -l /ufs/models/MiniCPM5-1B-Q4_K_M.gguf
aipet local model auto
aipet local batch 4
aipet local preload
aipet local status
```

保留所有llm-pool-v212日志。超时会返回并保留工作线程可能访问的内存，
不是允许继续运行；要求重启时，不unload、不重复preload。对照实验只能
在另一次冷启动将 `aipet local cores fc` 改为 `aipet local cores 40`
（单CPU6，非无条件降级）。诊断完恢复fc，不用单核耗时代表默认性能。

摄像头在另一次冷启动测试，按镜像内camera-v212脚本启动并保留PMIC护栏：

```text
camera live-cpu
camera live-verify 5 1
camera live-pipe 30 1
```

后台任务结束后：

```text
camera live-status
cat /dev/a733-camera-control
```

前述通过后才测试任意实时语音及离线闭环，沿用VOICE_V211_TEST.md。
整机3–5秒、连续多轮及声音恢复均需新板测，不能由主机回归代替。
