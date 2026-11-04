# v190：离线语音会话与 ASR/TTS 等待优化候选

2026-10-27。功能源码最终基线 `59808e18c3749fd0fc2e5a28e86e7d02a3acb61d`。
保留 v188/v189、历史镜像、开发过程文档与 Git 恢复点。未操作板上 SD/UFS，
未改 NPU 模型、MMU/DMA、时钟/电压、I2S 寄存器、摄像头、BLE 或运动保护。
**软件连接和参考测试不是整机验收；当前没有新版本实机时延，不能宣称 3–4 秒达标。**

## 连接方式

ICS43434 → I2S 48k S16 / x5 → 连续 FIR 16k → 通用 Zipformer NPU
→ 本地 Agent 路由 / MiniCPM5 CPU、上下文/KV → 真实回复首句流水线
→ Matcha CPU 声学 → VIP2 NPU vocoder → 连续重采样 48k → MAX98357。

入口 `aipet audio offline <1..10 turns> [100..8000 ms] [endpoint:0..5000]`。
这是有界、显式开启的半双工会话，不是开机常录音/唤醒词/AEC/全双工。
不需要云 API、Wi-Fi、NTP，不回退云端，不下发 STM32 动作或自动 arm。

开始时若没有完整 provider，选择标准 UFS 模型目录（存在 ASR encoder 时）或
标准 SD 目录；选中的目录不完整会报错，不偷偷换模型。显式 `general-voice`
安装的完整 provider 保留不变。先进行真实 LLM preload，缺模型/禁用/失败时
**麦克风保持关闭**；再准备 NPU，打印 **MIC ON** 后才是说话窗口。
首次加载与预热可能较久，不把预热挪到开始前就称为冷启动提速。

## 实际优化

1. **采集侧端点**：原端点在 NPU 消费回调内，推理积压时停止判定一起落后。
   现在在 48k 原始 PCM 的 10ms 窗口中做有界整数能量判断，不等待 NPU。
   连续活跃 30ms 后确认 speech，至少 300ms 录音、连续静音 500ms 后停止。
   所有已捕获样本仍排空、识别，绝不丢积压音频来伪造低延迟。
   400 是诊断阈值，已从旧的 FIR 后能量改为原始 PCM 能量，须实机重新确认；
   它不是训练 VAD。0 可关闭端点，以固定时长排查。
2. **同命令模型复用**：三路 ASR、词表和 vocoder 在一次 offline 命令内准备一次。
   下一轮重建前端/greedy，清空全部 35 路状态和 decoder 上下文，模型不重读。
   真实 Linux 捕获重放验证了两句中文、静音交替两遍，三路 client 始终复用。
   四模型 immutable snapshot 合计 69234688 字节，低于既有 96MiB resident 预算；
   最大单次 DMA 工作模型仍是原来的 encoder，不扩大地址/内存预留。
3. **A76 计算窗口**：CPU 声学 Run 和 ASR NPU 调用的 CPU staging 只在调用期间
   选择 CPU6/7（板测确认的 A76），退出恢复原亲和性；不改系统调度、频率或优先级。
   ORT 仍是单计算线程，避免持久线程池跨 NSH task group 生命周期。
   亲和性设置或恢复失败明确报错，不伪称已加速。
4. **LLM 预热与首句播放**：复用已实现的 RAM 权重、精确 KV 前缀和上下文，
   沿用既有标点/有界字符分句与 v189 缺 PCM 段间补缓冲。没有预生成回答，
   没有将 WAV/关键词识别冒充实时通用语音。长句无标点时现有分句仍可能等待，
   CPU 声学、NPU staging 与 LLM 竞争 A76 的实际收益须目标机量测。

单条 `local-speak-stream` 也有 A76 声学优化，但仍按调用打开/关闭 vocoder。
只有受控的 offline 会话才复用该 NPU fd；不能把这两种计时混为一谈。

## 所有权与并发

ASR 会话 open/listen/close 必须来自同一命令 owner 线程，配置替换被 lease 拒绝。
NuttX 实际 `libs/libc/sched/task_getpid.c` 返回 task-info 的 `ta_pid`；
`sched/group/group_create.c` 将其设为 group pid，pthread 共享该所属组。
vocoder 用 engine mutex 序列化，并检查 getpid；其他 task group 不能读取/关闭该 fd。
不会把一个 NSH 任务的 fd 缓存在 RAM 中供另一任务重用。
正常退出后所有 NPU fd 关闭；CPU immutable 声学 Session 可依既有配置继续驻留。

错误、取消不重试 NPU，不发云请求；捕获/播放/phrase 的 join 所有权不明时，
保留整个 ASR/TTS/配置 owner，报告 EOWNERDEAD，进入需重启的隔离状态，
不是“无泄漏恢复”。录音和消费线程全部结束后才提交 LLM/开始播放。
空转写不提交 LLM、不说预置成功语、不控制设备。无端点的空转写可以结束一轮
并继续显式指定的剩余轮数；循环资源正常结束的 rc=0 本身不代表有成功回复。
端点开启且无语音返回 ENODATA，结束当前会话。

## 回归与证据边界

- `test-aipet-audio.sh`：预热失败先于录音、会话只开一次、连续三轮、空转写不路由、
  本地错误无云降级；实际模型与设备为明确 mock。
- `test-voice-session-v190.sh`：实际配置/session 代码，mock ASR/TTS/采集，
  跨线程拒绝、复用/清理、失败粘滞、隔离保留和亲和性故障注入。
- `test-general-asr-v184.py`：生产前端/状态/后端代码，216 条 Linux 原生捕获重放，
  六个交替 case，共 428 次 mock 调用；不是新 NPU 执行。
- `test-tts-engine-v185.py`：真实 CPU 声学与五段既有 mel 对照；NPU 为 STUB。
  验证三次合成一次 prepare、跨 group 使用/关闭拒绝、完整输出/取消/错误清理。
- `test-live-capture-v182.py`：FIR 逐字节保持；人为阻塞 consumer 250ms 后，
  producer 端点仍按音频时间停止，所有 PCM 排空；无语音、错误和复用回归。
- 流式播放/分句/CPU 前端、实际 I2S RX/TX 函数模拟 MMIO 和 STM32 安全路径回归。
  MCU 编译曾因 host-only 未使用变量告警失败，修正 harness 后通过；不改运动逻辑。
- 真实 CPU ONNX 三条标准录音的所有逻辑 encoder 输出、token/位置与参考完全一致。
  对用户旧麦克风录音另做本地 CPU 对照，得到非空中文；不是当前板上录音或 NPU
  接口验收。个人录音和完整私有诊断结果仅在 ignored out，未放 Git/测试包。
  Windows 子进程调用 WSL 的初次沙箱限制失败另保留，获工具执行许可后重跑通过。

## 上板：先 ASR 文件，再实机闭环

镜像不包含 MiniCPM5；需保留用户已有 GGUF，推荐放在已通过挂载的 UFS，
也可用 SD。**烧录前备份 SD 的模型/配置/录音；不能认为重刷保留 data。**
不清 UFS bootloader、不格式化。先检查：

```text
aipet local status
ufs status
ls -l /ufs/models/MiniCPM5-1B-Q4_K_M.gguf
ls -l /data/models/MiniCPM5-1B-Q4_K_M.gguf
```

两者至少一个真实存在。只在 SD 有模型时可显式选择 `aipet local model sd`；
自定义路径使用已有 `aipet local model /absolute/model.gguf`。没有文件先复制，不改名造假。

独立测试包 `out/voice-board-checks-v190-r1.zip` 仅包含三条原始标准 WAV、摘要与说明。
解压后将 **checks 目录**复制到 SD data 分区 `npu/general-asr/checks/`，
不覆盖模型。包 SHA256 为
`d394f654c8918d9bc31166c15b52822df9941755e9dbe723a97c6dab446d1960`。

```text
npu selftest
aipet audio general-asr /data/npu/general-asr
aipet audio general-wav /data/npu/general-asr/checks/cmd_open_light_16k.wav
aipet audio general-wav /data/npu/general-asr/checks/cmd_time_16k.wav
aipet audio general-wav /data/npu/general-asr/checks/neg_silence_3s_16k.wav
```

前两条期望分别为“帮我打开灯”“现在几点了”；静音期望 -61，不应回答或有动作。
文件测试不打开麦克风/扬声器，不代表实时录音成功。

```text
aipet audio output i2s
aipet audio volume 50
aipet audio gain 5
aipet audio offline 1 8000 0
aipet audio offline 3 8000 400
cat /dev/a733-audio
npu info
free
```

先固定 8 秒排除能量阈值，再测能量端点三轮；每轮都等 **MIC ON** 再说话，
等播完后才是下一轮收音。若模型在 UFS 或自定义目录，先用既有 `general-voice`
显式安装两目录，再运行 offline。这个配置仍 RAM-only，不自动开机录音。

若文件能转写但实时为空：

```text
aipet audio record 5000
aipet audio wavinfo /data/aipet-record.wav
aipet audio general-wav /data/aipet-record.wav
cat /dev/a733-audio
```

此处只在明确 record 命令时保存录音，普通 offline 不保存/上传个人 PCM。
再测 Ctrl+C、取消后重启会话、静音、错误后再次调用、多轮上下文和录放切换。

## 计时与后续门禁

保留 `mic-stopped-mono-us`、`transcript-ready-mono-us`、LLM first-visible、
first-phrase、`tts-start-mono-us` 与 `first-driver-pull-mono-us`。
它们用于定位 ASR 积压、LLM 等待和 TTS 软件首包，**不能替代真实停止讲话到
扬声器首个有意义语音的声学测量**。端点本身约 500ms 静音等待也须计入目标。
没有 A7Z 新版数据前，不给虚构倍速、实时因子或 3–4 秒通过结论。
`asr-compute-v190` 的角色计数/耗时在关闭 session 时打印，复用后反映最后一轮；
每一轮均有 mic-stop/transcript-ready 标记。此角色耗时包含 CPU staging 和 NPU
调用，不是纯硬件运行时间；主机重放日志会明确写 HOST，不冒充 A76 计时。

本版 SDK 构建目录独立 `cubie-a7z_nsh_v190_voice`，沿用 v188 启动流程；
开机标记仍为 `M0 v188-boot-candidate`，应用用 `offline-v190`、
`voice-session-v190`、`asr-compute-v190` 识别。R1 中间 ELF 独立保留，
最终版本必须包含后补的 task-group/engine-mutex 所有权加固。

```sh
bash tools/build-a733-voice-v190.sh out/NEW-build out/ort-static-v185-r2
# 已有 v190 构建必须明确 --incremental，并给新的报告目录；不隐式删除/覆盖。
python3 tools/package-a733-boot-v188.py out/general-voice-build-v190-r2 ../../quickly-openvela/cmake_out/cubie-a7z_nsh_v190_voice ../openvela-a733-cubie-a7z-sd-v190-offline-voice-candidate.img out/voice-image-v190-r1 --base-version 189 --purpose voice-pipeline
```

## 最终构建与制品

R2 包装器退出 0，281 个 SDK 路径逐字节恢复；file=7012048、runtime=92008448
字节，链接布局检查通过。完整构建日志 `out/general-voice-build-v190-r2/build.log`
SHA256 为 `37b04b5d265dced28509672cbec73a266cb6cdae9915feef6b84aa5232eecb9a`。
R1 为 `6169dcc` 中间版本，bin SHA256
`d72cd91754813f185aeb289f3b3a7d4da9516fe95fedd973c1e6a59d4052baca`，
已单独封存 `out/general-voice-build-v190-r1/kernel-first-pass`，未打包供上板。

最终私有镜像：`../openvela-a733-cubie-a7z-sd-v190-offline-voice-candidate.img`，
2147483648 字节，SHA256：
`6082caf2fd059d06d1057abad1cf2bb726dbb4ab776f85c8a4f320c0b00f1ccb`。
kernel SHA256：`cbc2c3c10ef2ba928cd2da1ac1311528be5107eca44195ce4556d1465b414666`；
ELF SHA256：`21649b1843b3bdb721ccbf72875af894bf2258098b85ea2312d8e4c8163a580c`。

基于摘要锁定的 v189 镜像独立打包；内核读回、GPT/ext4/启动调用关系与全部非内核
分区摘要检查通过，原 ASR/TTS 模型 FAT 字节未变；未写物理 SD/UFS、未覆盖旧镜像。
结构检查不代表实机启动、实时 ASR 或完整听音通过。模型仍受私有/非商业许可约束，
不公开上传整卡制品。证据保存在 `docs/evidence/voice-v190`，含原始回归日志。

本地标签与 checkpoint：`a733-v190-offline-voice-candidate-20261027`。
最终 ELF/bin/map/config 另外封存 `out/v190-release-20261027`，恢复包引用封存副本，
不依赖未来可能继续增量构建的 SDK 输出。另有源码归档、Git history bundle、
旧 v189 镜像/新 v190 镜像/ELF/bin/测试 ZIP 五项外部摘要引用。

```sh
python3 tools/checkpoint-a733-project.py verify out/checkpoints/a733-v190-offline-voice-candidate-20261027 --check-references
python3 tools/checkpoint-a733-project.py restore out/checkpoints/a733-v190-offline-voice-candidate-20261027 out/restore-check-v190-r1 --with-git
```

恢复要求新目录，不 checkout/reset 原工作区，不删除历史开发过程文档。
