# v147：ICS43434 的 RXCNT 单位候选修正与波形重采

## 后续实机更新：首次基础录音通过

2026-10-15用户确认录到声音，5秒WAV、六个计数点和24MHz波形已核对；
有效左槽、静默右槽，音量偏小但未削顶。本轮不改固件或增益。
详见 [实测证据与边界](../evidence/ICS43434_V147_RUNTIME_ACCEPTANCE.md)。
下面保留原候选阶段的推理、失败证据与交付说明；其中“待测”指当时状态。

2026-10-15。永久源码只改比赛仓库的 A733 I2S 诊断层；BLE v145 继续暂停。
保留 v146 镜像和 `backup-before-ics43434-v147`（8ee2578）恢复分支。
完整 bundle `a733-before-ics43434-v147.bundle` 验证通过，SHA-256
`99cf923d3867eb4ff9a0a7e1f4a4ff12d2d8330e1bc462b3e5916bd0850d2248`。

## v146 实测失败的准确边界

用户日志已进入 NSH，明确显示 v146 驱动，不因启动旧标记 `v141-layout` 判为旧固件。
首次 ICS 命令耗时5.1067秒；内部 elapsed=5100ms、480000个输出样本、warmup=9600字、
target/consumed RX=489600、硬件RXCNT=244801、FIFO peak=2、INTSTA=0x11。
没有RX underrun/overrun位，但计数比较失败，返回-EIO，committed=0，未执行创建WAV。
所以“没有文件”的直接原因是软件计数门禁，不是 file_open/SD 写入失败。
原片段保存在 `docs/evidence/i2s-v147/v146-user-excerpt.txt`。

v146假定RXCNT每个FIFO字递增一次，主机stub采用相同假定，因此漏掉真实硬件差异。
用户结果与48kHz帧计数吻合：5100ms × 48000 = 244800；两声道软件消费489600字。
但本地 A733 User Manual V0.92 印刷1104页（PDF1124）只说 Digital Audio Engine
写入一个“sample”时递增，并没有明确写“每个WS帧一次”。**此时不能把猜测写成手册事实。**

候选v147使用该实机支持的帧单位，仍待中间计数与高采样率逻辑波形复核：

- 原始HW计数保持原值；期望HW=软件消费FIFO字数 / RX通道数，独立报告。
- 严格检查HW×通道数不得小于已读FIFO字数，不得多出超过128字；64位计算防溢出。
  不同时接受两种缩放，不将任何计数比率“自动识别为成功”。
- warmup结束和每秒都检查一次，最多保留6组FIFO字数/HW数（较长录音保留早期与末尾）。
- 时长、RXA、FIFO错误、完整样本数、RAM采集和临时文件原子提交保护全部保留。
- 报告 `counter-policy=frame-candidate`、expected/actual、counter-status与中途快照。
- 不改变引脚、PLL、BCLK极性、RX槽映射、SR24、RXOM1或PCM位移，不增加增益掩盖噪声。

此次min/max=-4895..5766、非零约一半，**与单麦克风只驱动一槽相容**，且不同于旧INMP
的满幅干扰；但没有原PCM文件，不能证明可辨识语音，也不能凭总RMS推断已修好。

## 逻辑分析仪原文件

用户确认 D0=LRCLK、D1=BCLK、D2=SD；D3..D7未使用。保持这个通道分配。
`I2S-1.sr` 是sigrok v2 ZIP：302592个8位逻辑样本，元数据**20kHz**，时长15.1296秒。
它不仅低于3.072MHz BCLK的奈奎斯特下限，也低于48kHz LRCLK，已发生欠采样。
不能根据截图/这些混叠数据解码24-bit I2S、判断边沿或证明物理时钟频率。
本轮没有从欠采样数据伪造PCM，也没有将截图中的低频节拍当成实际BCLK。

只读原文件已复制归档，SHA保持一致：

- `.sr`：`0631e59bb273c9499e149de9c799c1c7888b57fb422771d834c756643dc7bca4`
- `.pvs`：`1e0550485620b555583593f8b44b021c15ede10fa451303c92ee5432816f8bab`

重采建议24MHz以上，设备支持则50MHz优先；采样率不是BCLK频率。
约200ms warmup后抓10..50ms原始数据即可。逻辑分析仪GND接同一地，数字输入只接
3.3V信号，不接5V VCC或功放差分输出。D0/D1最好在模块实际WS/SCK接点测量，
并先核对 WS↔LRCLK、SCK↔BCLK 两组接线/通断，不单凭产品图认定板内相连。
解码配置：SCK=D1、WS=D0、SD=D2、标准I2S、一位延迟、MSB first、24有效位/32位slot、
上升沿采样。应检查每WS周期64个BCLK，而不是为了“能解码”随意翻转边沿。

## 候选上板步骤

```sh
cat /dev/a733-audio
echo init > /dev/a733-audio
time "echo icscapture=5000 > /dev/a733-audio"
cat /dev/a733-audio
ls -l /data/ics43434-stereo.wav
```

确认报告是v147。预期（不是本次已经测得）：status=0、committed=1、samples=480000、
target/consumed RX=489600 FIFO字、expected-hw=244800、actual-hw约244800加少量在途帧，
counter-status=0。中间六组可近似为：
9600/4801、105600/52801、201600/100801、297600/148801、393600/196801、489600/244801。
不同调度时点可有少量差异，必须仍在严格门限内；不是要求日志逐字节匹配。

WAV仍是960044字节、48kHz双声道S16、5秒。第1秒安静、第2..4秒说话、第5秒拍手。
若有旧同名文件，测试前另存；失败不会覆盖旧文件，所以必须核对本轮committed和内容，
不能仅见文件存在就当本轮成功。下载后用 `tools/analyze-ics43434.py` 分轨，并试听语音。
若再次失败，保留完整计数快照和高采样率波形，不取消错误检查来强行生成正常文件。

本轮没有操作实机、没有写SD。候选源码/主机测试通过不等于录音/冷启动/共存通过。
构建入口 `bash tools/build-a733-i2s-v147.sh`；封装入口
`bash tools/package-a733-i2s-v147.sh`。独立构建目录与新镜像名保留v146不变。
整盘烧录前先备份SD模型、凭据和录音。

## 本轮交付

全量2955目标构建、布局和镜像校验已通过，官方129个暂存路径恢复一致。
镜像：`openvela-a733-cubie-a7z-sd-i2s-v147-ics43434-counter-candidate.img`（2GiB），
SHA-256=`6cf150ee3ba8fb58014463ef9925496b80d7b47e92b333901f8ec96b50233b94`。
源码、构建日志、全部测试及SDK恢复清单见 [构建归档](../evidence/i2s-v147/build-summary.md)。
此状态是build-passed/image-verified，**v147实机录音、物理时序和声学仍待测**。
