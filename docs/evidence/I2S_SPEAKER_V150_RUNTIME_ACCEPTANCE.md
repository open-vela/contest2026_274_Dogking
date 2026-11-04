# v150 用户实机：ICS43434 录音 → MAX98357A WAV 回放通过

2026-10-16。用户确认拍手声、说话声均能播放，而且比较清楚。
本次仅归档实测及说明现有音量接口，不改驱动、不重新生成或覆盖镜像、不推送远端。

## 来源与恢复点

- 用户串口原文：[runtime-20261016.log](i2s-v150/runtime-20261016.log)。
- 原文字节 SHA-256：`5c8e1ec8bb0d41126b9824db09701b396906d493d1e4e35fa99bc5440f7cf7b1`。
- 原附件：`4d7dfe72-30bc-45eb-84ef-5069bce4630b/已粘贴的文本.txt`。
- 板端报告驱动 v150；早期 `M0 v141-layout` 是保留的布局标记，不作为驱动版本。
- 编译源码、工具链、配置、镜像恢复与构建证据见 [原交付归档](i2s-v150/build-summary.md)。
- 保留镜像 `openvela-a733-cubie-a7z-sd-i2s-v150-speaker-candidate.img`，不改历史文件名。
  SHA-256：`07d16f91575677063f79e63b2c5885ab7161a1c6c9f228e38d012f80575ae130`。
  此为原交付哈希，本轮未从开发板读取整卡复核。
- 声学结论来自用户现场听感；本轮未提供新 WAV，助手没有独立试听或做音质标定。

## 已通过的有限范围

同一次启动，先后两次 `icsvoice=5000`，默认麦克风增益 ×5；
每次录音后设置 `volume=50` 并回放 `/data/ics43434-voice.wav`。

| 项目 | 第一次录音后的回放 | 第二次录音后的回放 |
| --- | --- | --- |
| 连续回放次数 | 3 | 2（累计 plays=5） |
| 外部 time | 5.0750 / 5.0748 / 5.0749 秒 | 5.0719 / 5.0718 秒 |
| playback status / volume | 0 / 50% | 0 / 50% |
| 源 WAV | 48000 Hz、mono PCM16 | 48000 Hz、mono PCM16 |
| 输出 frames / sent / elapsed | 240000 / 240000 / 5000 ms | 240000 / 240000 / 5000 ms |
| 原始 TXCNT / INTSTA | 240001 / 00000050 | 240001 / 00000050 |
| 录音 committed / status | 1 / 0 | 1 / 0 |
| 录音 gain / clipped | 5 / 16 | 5 / 0 |

两次录音均内部 5100ms（含 warmup），FIFO words=489600、HW RX=244801，
六个计数检查点和 counter-status=0。没有报告超时。
最终排空后 INTSTA=0x50 包含尾端 TXU；驱动保留该原始值，不将有意排空时的
欠载误当成中途供数失败。此记录仍不证明所有负载下都不会中途欠载。
第一次录音 16/240000 个样本饱和（约 0.0067%），第二次为零；
不能从日志确定是哪种声音导致削顶，降低播放音量也不能恢复已削顶的录音。

## 音量已可调，无需换镜像

```sh
echo volume=30 > /dev/a733-audio
echo play=/data/ics43434-voice.wav > /dev/a733-audio
echo volume=50 > /dev/a733-audio
echo play=/data/ics43434-voice.wav > /dev/a733-audio
cat /dev/a733-audio
```

`volume=0..100` 为线性 PCM 数字幅度：0 静音，100 不衰减；不是百分比声学响度。
默认 25%，仅保留到本次关机/重启；不是持久配置。本轮仅 50% 有声学实测。
设置作用于后续 WAV 播放；整个播放持同一互斥锁，不能边播放边实时调音量。
不影响麦克风默认 ×5、tone/tone32 诊断或 UART4 TTS。
越界/非十进制参数拒绝，不修改旧音量。

## 负面记录与未通过边界

最初两次播放 `/data/models/speaker-test-v150-16k.wav` 立即结束，
`status=-2 plays=0`：路径打开报 ENOENT，需先确认测试文件已上传。
不能将其计为 16kHz 测试音发声通过，也不影响后续五次真实录音文件回放的证据。

本轮状态：boot-passed / 48k-mono-record-and-playback-hardware-passed（用户实测）；
尚不是 stress-passed。8/16/24kHz、stereo 源、音量全范围、冷启动循环、
Wi-Fi/BLE/LVGL 并发、取消/kill 清理、双工、标准 audio/Media、ASR 和
官方 Agent 云 TTS → I2S 均未由本轮验收。旧失败、候选、构建文档全部保留。
