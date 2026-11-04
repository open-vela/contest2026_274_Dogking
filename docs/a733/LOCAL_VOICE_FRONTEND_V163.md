# v163：真实 WAV 数值定位与录音电平检查

本版延续 v162 已通过的音频驱动，新增应用级诊断，不改 I2S 时钟/引脚/播放逻辑，
不改默认 x5、模型、fbank 数学编译选项、唤醒阈值和严格 golden oracle。
**新增命令不录音、不执行 NPU、不联网、不触发 Agent/TTS/设备动作。**
启动 `M0 v141-layout` 和音频驱动 `v162` 字样有意保留；新诊断打印 `frontend-audit: v163`，
不能仅用旧启动标记判断是否烧入新内核，需核对内核 SHA 和新命令。

## 为什么先做这个

v161 六组 WAV 的前五组特征 CRC 与参考不一致，静音组一致。
主机 FMA 敏感性实验只能说明浮点结果可能受运算实现影响，尚不能定位板端差异。
因此先导出板端实际 float32，再逐元素比较；不能直接放宽 CRC 宣称通过。

v162 用户确认电流声消失，但 3 条录音存在软件增益饱和，1 条增益前已到满量程。
WAV 的电平统计可辅助排查，但不能只凭峰值断定前级失真原因。
本版没有擅自加入 AGC、归一化、降噪或限幅器来掩盖问题。

## 新命令

```text
aipet audio wavinfo /data/mono.wav
aipet audio fbankcheck /data/mono.wav /data/reference.f32
aipet audio fbankexport /data/mono.wav /data/NEW.f32
```

- 输入沿用生产 WAV 解析器：16k 或 48k、单声道、PCM S16LE、0.5～10 秒。
  有界读取、RIFF/块/数据长度检查；48k 使用原 63-tap FIR 降至 16k。
- `wavinfo`：采样数/时长/PCM CRC、峰值、RMS、DC、dBFS、到达正负轨的样本数及最长连续段。
  全静音 dBFS 用 -200 哨兵表示，非物理测量下限；RMS 包含静音/DC，不等同于响度 LUFS。
- `fbankcheck`：同一个生产前端，输出实际特征 CRC、逐字节差异数、首差异帧/bin、maxabs、RMSE。
  参考必须同帧数，有限值且 |x|≤1000。**只要字节不同即返回 -EILSEQ/命令失败**，
  即使误差很小；不会改写参考、不把容差结果升级为 NPU golden。
- `fbankexport`：只允许 `/data` 的直接子文件、`.f32` 后缀、O_EXCL 新建、0600 请求权限。
  最大 320000 字节，29～1000 帧×80，little-endian float32，无文件头。
  已有文件/符号链接绝不覆盖。常规写入/fsync/close/取消错误清理本命令新建文件；
  如果掉电或清理失败，可能留下部分文件，需核对长度并使用另一个新文件名，不能把它当完整导出。
- 输入最终文件使用 O_NOFOLLOW；读输入可含 `/data` 内子目录，拒绝空/`.`/`..` 路径段、反斜杠/控制字符。
  不声称输入父目录符号链接已被完全沙箱化。导出禁止子目录，避免父目录链接穿越。
  输入以 O_NONBLOCK 打开后检查普通文件，FIFO/目录快速拒绝，避免在类型检查前阻塞；
  原 kwswav 的打开流程也补同一保护，未改变合法 WAV 的数学处理。
- 支持在 Agent 未启动、audio.conf 损坏时诊断；同一应用操作锁和 SIGINT 延迟清理策略保持。
  特征本身也可能携带语音信息，均只落本机，不自动上传；需分享时先确认隐私。

共享 `kws_extract` 还将 FIR 临时缓冲、KNF 窗口/FFT 状态在 NPU prepare 前释放；
实际特征缓冲保持至推理完成后清零。这里只优化生命周期，不声称推理速度已提高。

## 板测前准备

独立镜像：`openvela-a733-cubie-a7z-sd-v163-frontend-audit-candidate.img`（2147483648 字节）。
SHA-256：`b02e966eb62d1701bd27b03be52e896b8b7bb1b01a4386a5eb2c4cb01e8ab92d`。
内核 2692936 字节，SHA-256：
`ea6228e2e4e12b09575da35ebf35c8f52e3a8a54ea8b147155a2ccaf074cc335`。
配套 `.img.sha256` 可用于校验；新镜像仍需下面实机验证，不承诺所有功能一次通过。

1. 保留 v162 通过镜像。v163 是独立候选，制作/主机检查不能代替新镜像实机验收。
2. **整卡烧录会清空整个 /data**。先备份模型、Wi-Fi/SSH/Agent 配置、密钥和录音；不要把密码发聊天。
   若只更新内核，仍按已有安全更新流程执行，本文不提供自动烧录/分区命令。
3. 原 KWS 模型包保持 `out/a733-openvela-kws-v158-r2.tar.gz`；
   WAV 包保持 `out/a733-kws-wav-v161-r2.tar.gz`。按原流程放至：
   `/data/npu/voice/kws/features0.f32`～5、`/data/npu/voice/kws/wav/wave0.wav`～5。
   不需要重新转换模型，也不用 Linux 卡。

## 一次性数值测试（串口/SSH 均可）

先开串口日志保存。下面是 **NSH 命令**，不是 Windows 命令。若 `fbankcheck` 失败，
先继续完成其余对照/导出并保留日志；数值不一致就是要收集的证据，不要改模型或阈值。

```sh
free
npu info
aipet audio wavinfo /data/npu/voice/kws/wav/wave0.wav
aipet audio fbankcheck /data/npu/voice/kws/wav/wave0.wav /data/npu/voice/kws/features0.f32
aipet audio fbankcheck /data/npu/voice/kws/wav/wave1.wav /data/npu/voice/kws/features1.f32
aipet audio fbankcheck /data/npu/voice/kws/wav/wave2.wav /data/npu/voice/kws/features2.f32
aipet audio fbankcheck /data/npu/voice/kws/wav/wave3.wav /data/npu/voice/kws/features3.f32
aipet audio fbankcheck /data/npu/voice/kws/wav/wave4.wav /data/npu/voice/kws/features4.f32
aipet audio fbankcheck /data/npu/voice/kws/wav/wave5.wav /data/npu/voice/kws/features5.f32
aipet audio fbankexport /data/npu/voice/kws/wav/wave0.wav /data/v163-fbank0.f32
aipet audio fbankexport /data/npu/voice/kws/wav/wave1.wav /data/v163-fbank1.f32
aipet audio fbankexport /data/npu/voice/kws/wav/wave2.wav /data/v163-fbank2.f32
aipet audio fbankexport /data/npu/voice/kws/wav/wave3.wav /data/v163-fbank3.f32
aipet audio fbankexport /data/npu/voice/kws/wav/wave4.wav /data/v163-fbank4.f32
aipet audio fbankexport /data/npu/voice/kws/wav/wave5.wav /data/v163-fbank5.f32
ls -l /data/v163-fbank0.f32
free
npu info
```

0～3 各 67200 字节（210 帧），4～5 各 106240 字节（332 帧）。
旧主机参考 CRC：6a969644、b9197206、ea0f1594、d6d3798e、f7b2359f、27184580。
这是对照值，不是对 v163 板端结果的预判。重跑导出时换新名字，禁止覆盖。

若 FTP 根目录仍是 `/data/models`，可在确认这些目标尚不存在后，将六个 f32 用 `cp`
复制到 `/data/models` 再取回；不是新增后台同步。FTP 配置和真实连接地址沿用板上当前值。
回传六个导出文件和完整日志，保留参考特征文件不变。

## 录音削顶定位（明确自愿录音，再执行）

`record` 会替换固定 `/data/aipet-record.wav`；需要旧录音时先按自己的备份流程保存。
不必反复设置增益，先确认仍为 x5，在平常说话距离录 5 秒，避免贴近吹气/拍打麦克风。

```sh
aipet audio status
aipet audio record 5000
cat /dev/a733-audio
aipet audio wavinfo /data/aipet-record.wav
aipet audio play /data/aipet-record.wav
cat /dev/a733-audio
```

一起保留 WAV 和驱动 processing 的 input-min/max、gain、clipped；
WAV 只有处理后数据，不能恢复增益前波形。只在必要且明确选择时用现有 `gain` 命令对照，
不因本指南自动永久改默认值。后续再做带标签的安静/说话/噪声 KWS 正负样本验收。

## 主机离线对照

```sh
python3 tools/npu-voice/compare-fbank.py reference.f32 actual.f32 --json NEW-report.json
```

退出码 0：字节精确；2：有限合法但不一致；1：格式/IO/报告已存在错误。
输入只读，报告必须新建。输出文件 SHA/CRC、首差异、maxabs/RMSE；
不认证文件来源、NPU 执行或 ASR/声学准确率。

## 软件回归与构建

```sh
bash tools/test-frontend-audit.sh out/a733-kws-wav-v161-r2/voice/kws/wav out/a733-openvela-kws-v158-r2/voice/kws
python3 tools/test-fbank-compare.py
bash tools/test-aipet-audio.sh
bash tools/test-kws-wave.sh out/a733-kws-wav-v161-r2/voice/kws/wav out/a733-openvela-kws-v158-r2/voice/kws
bash tools/test-aipet-fbank.sh
python3 tools/test-aipet-npukws.py out/a733-openvela-kws-v158-r1
A733_TENSOR_EXPERIMENTAL=1 A733_BUILD_DIR="$OPENVELA_WORKSPACE/cmake_out/cubie-a7z_nsh_v163_frontend_audit_r2_candidate" JOBS=8 bash tools/build-a733-wapi.sh
```

使用新输出目录；完整环境/工具链沿用项目构建文档。官方 SDK 暂存后恢复校验，
当前脏工作树必须用最终源码包复现，不能仅依赖 HEAD。
