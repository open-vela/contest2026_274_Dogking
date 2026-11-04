# v214：常驻半双工本地语音候选

2026-10-30。顺序：常驻 → 端点/开头 → 英文/OOV → 减少重复准备与首句等待。
这是待上板候选，不是量产服务；没有测得整机首音频 3–5 秒达标。

## 基线、范围与恢复

改动前标签 `a733-v213-before-resident-voice-20261030` 指向
`e0b349905acf608532c740e3386dfbe5ad0d51d7`。保留 v213 镜像、封存和 checkpoint，
不删除历史开发文档/失败日志。新版本另存 Git 提交、标签、源码归档/bundle。
未改 NPU MMU/DMA/模型预留、模型内容、CRC 校验、I2S 频率、摄像头、UFS 或 PMIC。

## 命令

先停摄像头/YOLO/其他播放器，保留已通过接线和模型。

```text
aipet audio output i2s
aipet audio volume 50
aipet audio gain 5
aipet local on
aipet local status
aipet audio service start
aipet audio service status
```

需要多轮记忆可在 start **之前**执行 `aipet local context 3`，会清除旧历史但不卸载权重；
不要每轮重设 context。首次 start 默认安装 `/data/npu/general-asr`、`/data/npu/general-tts`，
也可在启动前显式指定：

```text
aipet audio general-voice /data/npu/general-asr /data/npu/general-tts
```

缺少真实模型会在开放 MIC 前失败，不下载或伪造文件。
**ASR/TTS 不要指定 `/ufs/npu/`**：底层 tensor 路径校验仍只接受已审计的 `/data/` 路径。
LLM 可以沿用已经正常工作的 UFS/SD 配置。本次没有实现 ASR/TTS 的 UFS 路径扩展。

首次仍需加载和准备模型。等到：

```text
voice-service: READY MIC ON; continuously waiting, up to 400ms RAM pre-roll
```

此后不用每轮输命令：持续等待 → 边录边识别 → 端点后提交真实转写 → LLM 流式首句 TTS/I2S
→ 自动恢复 READY。空转写不调用 LLM、不消耗正常回复轮数。没有唤醒词，背景人声也可能触发。

从串口或另一个已认证 SSH shell 停止：

```text
aipet audio service stop
aipet audio service status
```

等 `active=0` 再改配置、跑旧测试或断电。stop 是协作取消，当前冷预热/计算可能需要先结束其有界调用。
不要强杀 NPU/ORT owner。后台日志和 NSH 提示符可能交错，不等于重复执行。
worker 的输入固定为 `/dev/null`，日志输出固定为板载串口 `/dev/console`，不依赖启动它的 SSH
会话；SSH 里通过 service status 看状态，串口看详细日志。启动 shell 退出不应使流式输出遭遇 SSH 的 SIGPIPE。
本版不默认开机录音，每次启动服务时 start 一次；服务停止后释放 fd，显式重启会重新准备资源。

## 实现

### 常驻与所有权

独立 builtin task 持有 ASR session 和 pinned vocoder；不是短命 NSH 的 detached pthread，
不跨 task group 复用 fd。配置 lease 覆盖整个服务，状态/停止以外的音频配置和普通 LLM 命令被排斥。
状态分 preparing-MIC-OFF / calibrating / READY-idle-MIC-ON / speech-MIC-ON /
asr-finalizing-MIC-OFF / reply-MIC-OFF / off。RX 返回后立即标记 MIC OFF，不在等待 ASR 积压消费时
继续冒称收音；延迟到达的 READY/speech 通知也不能把它重新改成 ON。
READY 在硬件预热、初始噪声校准后由消费线程报告，不能证明电气/声学验收。
静音没有固定 8 秒窗口；第一块真实提交的 PCM 前刷新 pristine ASR 的执行期限，
因此几分钟静音不会耗尽 120 秒推理期限，执行中的期限不延长。
worker 无法确认退出时保留完整 task/session/回调，进入 quarantine 要求重启，不 reset/retry/free 不确定资源。

### 端点和开头

- 新增连续 RX ioctl，不落盘。400 ms 环形前滚，静音不送 ASR；起声触发时一并提交前滚。
- 400 ms 初始噪声校准；去 DC 能量、自适应噪声底、双阈值、160 ms 起声确认、700 ms 安静尾段。
- 正常多轮只复用校准噪声底，语音/端点状态每轮清零；不再逐轮重等 400 ms。
  空转写的误触发会重新校准。刚恢复时前滚可能不足 400 ms，不补假音频；仍保留 100 ms 硬件预热，
  第一轮和恢复时都以实际 READY 为准，不能声称预热/播放期间也已采到讲话。
- 500 ms 句中停顿不结束；160 ms 短词也能结束，不会因不足 240 ms 一直等上限。
- 原始 FIFO 生产者、异步 FIR/ASR 消费者分离；RX 使用身份验证的 CPU1/A55 作用域，
  ASR 仍用 CPU6/A76、CPU 声学仍用 CPU7/A76；恢复亲和性，不改变全局调度/DVFS。
- FIFO 等待有界，预热另有 250 ms 上限，持续 RX 有滚动采样率、帧计数回绕和溢出验证。
  直接 cat/echo 音频节点在占用时返回 busy，不挂在无限静音的互斥锁后。
- **每段 ASR 音频上限仍为 8 秒，含前滚和尾静音，真实连续说话预算约 7 秒。**
  达上限会明确丢弃并重新监听，不将截断半句送 LLM，不是长篇连续听写。

这是 CPU 能量门控，不是经过验收的神经 VAD、远场降噪或 AEC。
持续 PIO 收音会持续占用一个 A55 核心；长时温升、噪声鲁棒性和其他负载共存待板测。
`/dev/a733-audio` 的旧 capture-proof 描述最后一次显式 capture；常驻流程看
`voice-service-v214`、`dialogue-rx-v214` 和 `voice-stage-v214`，不要套用旧 capture 的录音统计。

### 英文、未知字符与延迟

v213 实机回答含 MiniCPM 时，前端 -61 导致后半句取消。
v214 将 ASCII 字母按中文名称拼读，**不是自然英文 G2P**；词表外有效 Unicode 读作
“未知字符编码”加十进制码点，不静默删除、不改终端原文。
音素按 127 blank-expanded IDs 上限分段，不截断。无效 UTF-8、角色/工具注入和真实设备错误仍失败。
文本长度、总执行时间和每次播放上限继续有界，不能无限展开。

LLM 只在服务启动预热一次，后续保留权重、上下文、精确前缀 KV，不用逐轮 warmup 覆盖有效前缀。
ASR/vocoder 同一 task 保留准备结果。流式首句预算计入英文展开的成本，避免第一包过长。
新增采集停止、消费完成、转写可用的绝对单调时间；保留 LLM 首字、TTS 首 PCM/driver pull。
**driver pull 不等于物理起音；本版没有实测新的 LLM 延迟或整机 3–5 秒达标。**

## 上板验收

1. start 后静音至少 3 分钟仍是 READY，再说短句，核对开头不丢字。
2. 连续 10 轮，含“你是谁”“一加一等于几”“记住我叫小王”“我叫什么名字”；
   回复完整并自动 READY，不逐轮 prepare/preload。
3. 句中停顿半秒，后半句仍属于同一转写；分别测安静和环境噪声。
4. 超长讲话只计 overlong、不发 LLM/MCU，之后短句正常。
5. 在空闲/收音/生成/播放时 stop，最终 active=0，无继续录放。
6. 停服务后测试完整中文、英文和 OOV：

```text
aipet audio local-speak-stream "你好，语音测试成功。"
aipet audio local-speak-stream "我是MiniCPM系列模型，由团队开发。接下来继续回答你的问题。"
aipet audio local-speak-stream "出现🦊后，这一句也应该完整朗读。"
```

7. 回传完整串口日志，区分冷启动到 READY、停说到转写、LLM 首字、首 driver pull 和实听起音。

本版 **半双工**：识别收尾/LLM/TTS/播放时 MIC OFF，结束后恢复；不支持 AEC、播放中插话/打断或全双工。
未录到的插话不能算 ASR 成功。没有云回退，没有 MCU 动作，音频只在 RAM 暂存。

## 可复现验证与构建

```bash
python3 tools/test-voice-v214.py out/NEW-v214-regression --build <构建目录>
A733_V214_BUILD_DIR=<同目录> bash tools/build-a733-voice-v214.sh out/NEW-v214-build out/ort-static-v185-r2 --incremental
python3 tools/check-a733-voice-v214.py --build <同目录> --build-report out/NEW-v214-build --output out/NEW-v214-full --firmware-commit <源码提交>
python3 tools/package-a733-voice-v214.py out/NEW-v214-seal ../NEW-v214.img out/NEW-v214-package --firmware-commit <源码提交> --voice-gates out/NEW-v214-regression/summary.json --full-gates out/NEW-v214-full/summary.json
```

每次用新目录；SDK 暂存测试不要和完整构建同时运行。保留旧回归，增加生产常驻生命周期、
真实 FIR/前滚、端点/无限静音、实际 monitor 循环模拟 MMIO 故障注入。
原五条 CPU 声学 mel golden 仍严格比较；新增英文/OOV 用真实 CPU 声学和有限 mel/native 输入校验，
**NPU 输出在主机测试中仍是 stub，不保存/冒充音频**。另检查 17 个实际 ARM 编译单元、栈、
摄像头/ISA/链接、SDK 恢复、GPT/文件系统/内核脚本/私有资产回读。
主机通过不等于板上 NPU/麦克风/扬声器的端到端通过。

新制品名 `../openvela-a733-cubie-a7z-sd-v214-voice-resident-candidate.img`；
完成后的摘要、封存和报告见本页发布记录。不自动推送，不写实际 SD/UFS 或格式化。
整卡烧录前另存 SD 上的个人模型、配置和凭据。

## 本次封存与软件结果

固件源码提交：`0890208eede1fc819bd61a84a890b0ed597f280b`。前续实现提交
`6b5343d`、校准/排他 `c287bc5`、MIC OFF 状态 `fcdacb7` 均保留。
最终恢复标签：`a733-v214-voice-resident-candidate-20261030`，覆盖源码及发布证据，不覆盖 v213 标签。

- 镜像：`../openvela-a733-cubie-a7z-sd-v214-voice-resident-candidate.img`，2147483648 字节。
- SHA256：`c9569622ad928b0730bd79c8be6f17156249b5f5f9e4e858ea3aa1df9ce0bfa6`。
- 内核：7172800 字节，SHA256：`e2da629baee4fd6418784dc59c1601029b9963efb7bf6342576bff0f990ac1a5`。
- Windows 原生 Get-FileHash 与 WSL 打包器独立一致；v213 旧镜像仍为
  `9850b49225e54c64f5b1ea54955bfb3d36b0a35cc4998f7114ea14c0eaf53cec`，没有被替换。
- 17 项语音回归、17 个实际 ARM 单元通过，最大静态栈帧 2384 字节。
  新连续监测/前滚/校准复用/多轮/取消/迟到事件/组排他/stdio 隔离用生产实现和显式 mock 验证。
- 2057 项完整目标构建通过；SDK 暂存 298 路径逐字节恢复。
- 摄像头 22 项主机回归、原生 ARM、完整链接、ISA/亲和性/有界等待检查通过。
  构造器仍 24 个，runtime end=`0x45de9000`，未越过保留区。
- GPT、ext4、内嵌内核/启动脚本回读、49 项私有资产和 3 条标准 WAV 回读通过。
- 本轮没有上板执行，不宣称真实拾音、播放、长稳或首音频延迟通过。

封存：`out/v214-release-20261030`，含 ELF/bin/map/config、模式/许可证和启动脚本/manifest 共 10 项。
报告：`out/voice-build-v214-r2`、`out/voice-regression-v214-r6`、
`out/voice-full-v214-r1`、`out/voice-package-v214-r1`；摘要/日志另存 `docs/evidence/voice-v214`。
早期回归 r1/r2 是旧 OOV 失败预期/固定 mel 长度夹具不适用于新读法的记录，未删除；
修正夹具保留原五条中文严格 golden，新读法另行验证。最终镜像只绑定 r6 和 build r2，不混用早期中间产物。
早期 boot188/M0 v141 和驱动 v212 标签保留；根据 `voice-service-v214`、`dialogue-rx-v214`、
`voice-stage-v214` 及 `camera-v214` 启动脚本识别新版本。

恢复包：`out/checkpoints/a733-v214-voice-resident-recovery-20261030`，包含 Git bundle、
源码归档、当前工作树补丁与外部制品索引，用户未跟踪总结也保留原字节。
工具 `tools/checkpoint-voice-v214.py` 以 Windows 原生 Python 创建/核验，外部引用包含
v213/v214 镜像、v210 受控打包基底、ORT 静态库/清单、封存和全部最终报告。
仅向新目录恢复，禁止 reset --hard 或覆盖当前工作区：

```text
python tools/checkpoint-a733-project.py verify out/checkpoints/a733-v214-voice-resident-recovery-20261030 --check-references
python tools/checkpoint-a733-project.py restore out/checkpoints/a733-v214-voice-resident-recovery-20261030 out/NEW-restore-v214 --with-git
```

源码恢复后还需保留外部模型/SDK/工具链；外部索引是摘要，不把 2 GiB 镜像塞进 Git 或源码包。
本机另做新目录恢复校验。不自动推送远端、不刷实际 SD/UFS。
