# v147 ICS43434：首次基础录音通过

日期：2026-10-15。来源：用户提供的串口输出、I2S-2.sr/.pvs、原始 WAV，
以及用户明确反馈“终于录到声音了，音量有一点小”。助手本轮只做本地主机分析，
没有 SSH 录音、烧录、修改固件或增益，没有自行试听/用 ASR 判读语音。

判定：**单次启动/5秒双槽采集/文件一致性通过；用户确认可听到声音。**
不是音质/SNR标定、长稳、全音频框架或 Agent/ASR 全链路验收。
此前失败日志、候选构建记录和镜像全部保留；下文是后续实测更新。

## 固件与恢复点

驱动自报 v147；早期启动标记 M0 v141-layout 不作为驱动版本判断依据。
本次不改变已验证的引脚、PLL、极性、槽映射、计数门禁和 PCM 位移。

- 编译源码：2d3acdc49152af5bfc19eae882f3b7e102c4d5f2。
- 内核 SHA-256：70ac9b0db7a5f5065b10a2f5474f58480fff8dc6e8861875fdee46ffac57e1b1。
- 已交付镜像：openvela-a733-cubie-a7z-sd-i2s-v147-ics43434-counter-candidate.img。
- 镜像 SHA-256：6cf150ee3ba8fb58014463ef9925496b80d7b47e92b333901f8ec96b50233b94。
- 原候选恢复标签 a733-i2s-v147-counter-candidate 不移动、不覆盖。
- 本轮证据保存提交可作为“基础录音通过”恢复点；尚未增加增益，不需要重烧镜像。

上述镜像哈希来自交付时的构建/封装记录，不是本轮从开发板回读的整盘哈希。

## 用户串口关键原文

```text
nsh> time "echo icscapture=5000 > /dev/a733-audio"
5.6712 sec
A733 I2S0 MAX98357A/INMP441/ICS43434 acceptance driver v147
state=ready initialized=1 pll-locked=1 last=0 last-op=ics43434 probes=3 tones=0 captures=1 timeouts=0 samples=480000
stream-proof: tx-count=0 rx-count=244801 capture-nonzero=239035 raw-nonzero=239938 warmup=9600 first-raw=00007940 last-raw=00000000 capture-min=-2189 capture-max=1946
capture-proof: mode=pcm16 slot=2(stereo) status=0 committed=1 requested=5000 ms elapsed=5100 ms target-rx=489600 consumed-rx=489600 hw-rx=244801 fifo-peak=2 intsta=00000011
capture-config: bclk-polarity=0 lrck-polarity=0 PB8-pull=2(down) fmt0=00001f57 rxmap3=00000100 pb-pull=00160005
capture-stats: units=S16 zero=240965 sign-transitions=163280 sum-square=24666259689 raw-low55=0 raw-lowaa=0
capture-output: /data/ics43434-stereo.wav
counter-policy=frame-candidate expected-hw=244800 actual-hw=244801 counter-status=0 points=6 FIFO-words/HW=9600/4801,105600/52801,201600/100801,297600/148801,393600/196801,489600/244801
nsh> ls -l /data/ics43434-stereo.wav
 -rw-rw-rw-      960044 /data/ics43434-stereo.wav
```

六个中间点均支持本次双槽条件下 RXCNT 按帧计数，软件 FIFO 按字计数；
不是取消校验放行。保留源码 frame-candidate 名称，不把单一条件推广到所有硬件模式。
内部5100ms包括100ms warmup；文件保留5000ms，命令5.6712秒还包括文件提交等开销。

## 原始文件身份与隐私

输入均在仓库外的 Openvela/音频测试文件/，不修改原件，不将原始语音、
逻辑数据或分轨 WAV 提交 Git/上传远端。I2S-2.pvs 为分析仪工程配置。

| 文件 | SHA-256 |
| --- | --- |
| ics43434-stereo.wav | 9535479c0d4d9a066c0c14db432f66721a080a8a0db1ef706a1d427f225bf492 |
| I2S-2.sr | 3ce6a4c7c720111516bccbeebc39ac4707662722500de8bbc4c158baa8e8611a |
| I2S-2.pvs | 729d9e6e1c1ed652b5ff7f457d63587918c6510eea4a6fcc58aac6d10ad24ef3 |

分析/未放大分轨保存在同级 v147-ics43434-analysis-20261015/：
analysis.json、windows-100ms.csv、left.wav、right.wav。
这是本地分析目录，不是可发布的语音样本库。

## WAV 实际检查

960044字节，48000Hz、双声道 signed-16 little-endian，240000帧，**恰好5秒**。
交错样本总数480000；非零239035、零240965、范围-2189..1946、
平方和24666259689、相邻符号变化163280，均与本次用户串口统计一致。
因此不是只看到旧同名文件存在就宣称成功。

| 指标 | 左声道 | 右声道 |
| --- | ---: | ---: |
| 样本数 | 240000 | 240000 |
| 非零数 | 239035 | 0 |
| 最小/最大 | -2189 / 1946 | 0 / 0 |
| RMS（S16单位） | 320.5871 | 0 |
| 去均值 AC RMS | 319.5305 | 0 |
| 满幅削顶样本 | 0 | 0 |

这是当前模块的**有效左槽、静默右槽**实测，不据此声称所有同名模块
都采用相同 L/R 硬件连接。后续单声道入口应明确选择左槽，不盲目平均 L/R。
如果使用 (L+R)/2，下混结果会额外降低约6dB；尚未验证用户播放器是否这样下混。

左声道绝对峰值2189，即满幅6.68% / -23.50dBFS；RMS=-40.19dBFS。
幅度偏小且未削顶，与用户听感反馈相容。录音距离、声压和环境噪声未标定，
不能仅凭 RMS 判定麦克风灵敏度不合格或计算真实语音 SNR。

## 24MHz 原始时序检查

D0=WS/LRCLK，D1=SCK/BCLK，D2=SD，由用户确认；不改通道来追求匹配。
sigrok 元数据24MHz、1000000个8位样本，捕获41.6667ms。
按固定标准I2S：上升沿采样、一位延迟、MSB先行、S24/32位槽解码。

- 127997个BCLK上升沿；边沿跨度估算3071928.58Hz，约3.072MHz。
- WS下降沿跨度估算47998.90Hz，约48kHz。
- WS完整周期：1976个500采样点、23个501采样点。
- 3999个完整半帧均为32个BCLK，即完整帧64个BCLK。
- 完整解码1999个双槽帧：左非零1983，范围-20..255；右全零。
- 解码S16LE双槽数据 SHA-256：
  208dbc9ab00d6a03136ad25fc4cdee26c98d47077ab12e5d06a373f1984be211。

这些频率是分析仪采样时基估算值，不是校准仪器的绝对精度结论，也不证明模拟信号完整性。
**该短段没有与5秒WAV形成整段逐字节匹配**；捕获起点/是否同一次操作未明确，
可能落在丢弃的warmup内或不同时刻，具体原因未证实。不得写成“总线与WAV bit-exact”。
此限制不改变WAV与串口统计一致、用户确认有声音的独立证据。

只读复现（在仓库根目录，输入路径请替换为本地实际路径）：

```sh
python tools/test-i2s-wire.py
python tools/check-sigrok-i2s.py ../../音频测试文件/I2S-2.sr
python tools/analyze-i2s-wire.py ../../音频测试文件/I2S-2.sr --reference-wav ../../音频测试文件/ics43434-stereo.wav
python tools/analyze-ics43434.py ../../音频测试文件/ics43434-stereo.wav --output-dir ../../音频测试文件/new-analysis --expected-seconds 5
```

比赛仓库位于 A733-A7Z-ALL-Files/ 下，上述路径对应 Openvela/音频测试文件。
分轨工具要求输出目录不存在，防止覆盖已有分析。合成测试已通过：
正负样本/左右槽/一位延迟/32时钟半帧、欠采样拒绝、精确PCM匹配和原件不变。
合成的3MHz/46875Hz节拍仅测试算法，不充当硬件3.072MHz/48kHz证据。

## 小音量的后续方案（未实现）

当前驱动 S24 算术右移8位转 S16，不附加增益/AGC。保留这一原始采集接口。
下一阶段可在独立处理层提供选左槽的mono输出、可配置×4（约+12dB）起步、
饱和限幅和可观测削顶计数；按实测需要再考虑×8（约+18dB）。
对本文件数学检查：×4/×8无超量程样本，×16有17个超量程样本；
这不保证之后更大声的输入不会削顶。数字增益同时放大底噪，不会改善固有SNR。

后续仍待：安静/说话/拍手对照、多次冷启动与重复录音、收播共存/系统负载、
DMA和标准openvela audio lower-half、连续流和官方ASR/Agent接入。
