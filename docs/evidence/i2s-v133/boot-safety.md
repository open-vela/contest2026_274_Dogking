# A733 I2S v133 启动安全修复

## 原始现象与最终归因

v132 实机有一次启动停在 `N8`，没有出现 `X0`、`X1` 或 NSH 提示符。
`N8` 位于 `nx_start()` 的 `sched_unlock()` 之后，说明 CPU0 已进入 idle
阶段，但初始 NSH 任务没有获得运行机会。该故障发生在任何 tone/capture
命令之前，不能归因于 MAX98357A 或 INMP441 的接线及数据通路。

后续更换/稳定供电后，v133 正常经过 `N8`、`X1` 并进入 NSH；用户确认原始
停顿由电源问题引起，不是开发板或固件缺陷。v133 的惰性探测仍然保留，因为
可选外设退出启动关键路径本身是正确的健壮性设计。启动输出中 `X0` 可能被八核
并发早期串口字符覆盖，但 `X1`、`PR` 和交互式 `nsh>` 已证明 NSH 完成初始化。

## 静态核对

- v131 与 v132 的 `g_readytorun`、`g_assignedtasks`、`g_running_tasks`、
  `g_idletcb`、`g_last_regs`、SMP idle 栈边界地址一致。
- v92 引入的页对齐 SMP 栈/堆边界仍在，未发生旧故障回归。
- I2S 驱动声称开机被动，但 `a733_i2s0_audio_initialize()` 实际调用
  `a733_i2s0_probe()`，在 `nx_bringup()` 中读取 PLL、I2S 和 PB 寄存器。

## v133 修复

设备初始化阶段只执行 `register_driver()`，不再访问任何 I2S、CCU 或 PIO
寄存器。硬件访问推迟至用户首次读取 `/dev/a733-audio`，或显式发出
`probe`、`init`、`tone`、`capture` 命令。这样可选音频硬件完全退出启动
关键路径，同时保留 v132 的 16 位 FIFO、原始 32 位采集及诊断功能。

## 构建与镜像验证

```text
build: quickly-openvela/cmake_out/cubie-a7z_nsh_v133_i2s_bootsafe
result: 2730/2730, build completed successfully
nuttx bytes: 3011968
nuttx.bin bytes: 2199512
nuttx.bin SHA-256: acf73e883300b635832fb063e2fc553b9184f2624a332d5c7e8ef3b69ab6b760

image: openvela-a733-cubie-a7z-sd-i2s-v133-bootsafe-candidate.img
image bytes: 2147483648
image SHA-256: f6f7fb9048f5809840d1968090036b9f6907f9d79ea093abbc06ecd76ba60b6e
embedded kernel bytes: 2199512
embedded kernel SHA-256: acf73e883300b635832fb063e2fc553b9184f2624a332d5c7e8ef3b69ab6b760
```

独立验证结果：`e2fsck -fn` 通过，`sgdisk -v` 报告 `No problems found`，
镜像内核回读哈希与构建产物完全一致。分区 4 的 2048 扇区对齐提示继承自
已验证底图，不是本次修改产生的问题。

## 2026-10-11 实机结果

v133 已完成一次正常启动和 MAX98357A 播放验证：

```text
echo init > /dev/a733-audio
state=ready initialized=1 pll-locked=1 last=0 timeouts=0

echo tone=1000,3000 > /dev/a733-audio
audible: yes, louder
fmt0=00001f37 tones=1 samples=144000 tx-count=144031

echo tone32=1000,3000 > /dev/a733-audio
audible: yes, weaker
fmt0=00001f77 tones=2 samples=144000 tx-count=144031
```

结论：MAX98357A、PB5/BCLK、PB6/LRCK、PB7/DOUT0、PLL/CCU、I2S0 FIFO 和
扬声器差分输出链路均为 **hardware-passed**。16 位 FIFO 路径与厂商 Linux
`S16_LE` 语义一致且音量更正常，应作为正式默认；`tone32=` 仅保留作兼容诊断。
此次只连接了扬声器，INMP441 采集当时仍为 **hardware-pending**。

随后单独连接 INMP441，连续两轮原始采集与 WAV 采集均成功：

```text
capture32=1000:
  samples=48000 raw-nonzero=48000 warmup=4800 timeouts=0
  first/last=ffa66b34/00355595（第二轮 0066aaab/ffab2b2c）
  /data/inmp441-raw32.bin=192000 bytes

capture=3000:
  samples=144000 capture-nonzero=144000 raw-nonzero=144000 timeouts=0
  min/max=-118/115（第二轮 -109/118）
  /data/inmp441.wav=288044 bytes
```

文件大小分别严格等于 `48000×4` 和 `44+144000×2`，证明 PB8/DIN0、声道
映射、24-bit FIFO、warmup 与文件写入链路工作。之后取得的安静、说话、拍手
三段 5 秒 WAV 均为电流噪声；离线统计的 RMS 分别约为 82.44、82.73、82.59，
各时间片能量不随输入变化，因此 v133 **不能**标记为有效录音通过。

原始 FIFO 字形如 `ffa66b34`、`00355595`：其有效 `S24_3LE` 样本在 bits 23:0，
bits 31:24 是符号扩展。v133 错误使用 `raw >> 16`，只留下最高约 8 位；正确
S24→S16 转换是算术右移 8 位。传输/时钟/FIFO 为 **hardware-passed**，PCM
语音内容在修复镜像回归前为 **hardware-pending**。

## 后续门槛

启动、扬声器播放和麦克风采集已经通过。仍建议补做三次稳定供电下的冷启动，
并把 WAV 传回电脑试听和做幅度分析：

```sh
echo capture=3000 > /dev/a733-audio
cat /dev/a733-audio
```

如果稳定供电下再次停在 `N8`，才重新开启 SMP 调度/唤醒调查；单次弱电源事件
不再计作固件回归。
