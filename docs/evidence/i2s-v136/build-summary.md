# v136 INMP441 RX FIFO 节拍修复候选

## v135 实机失败证据

v135 的三段 `silence/voice/clap` WAV 都是 48 kHz、单声道、S16LE，数据区
480000 字节，文件头声明时长精确为 5.000 秒；但这只说明软件循环写满了
240000 个样本，并不证明硬件实际采集了 5 秒。

离线取证得到：

- 三段音频 RMS 分别约为 21120、21169、21052，最大差异仅 0.048 dB；
- 约 72.4%～72.9% 的样本与前一个样本完全相同；
- 按连续值变化次数估算，三段文件只有约 1.36～1.38 秒真实硬件更新；
- 没有语音包络或拍手瞬态，试听均为接近满幅的数字电流声；
- 板端原始 1 秒抓取虽然由软件写出 48000 个字，硬件 `RXCNT` 却只有
  13234，和“空 FIFO 被反复读取”完全一致。

因此 v135 被降级为 **hardware-failed**，不能再用于麦克风验收。

## 决定性根因

A733 用户手册的 I2S/PCM FIFO 状态寄存器说明：

- bit 8 `RXA`：FIFO 至少有一个可读字；
- bit 7 `PLACE_HOLDER`：固定为 1、没有 FIFO 计数含义；
- bits 6:0 `RXA_CNT`：接收 FIFO 计数；
- 空 FIFO 的复位/空闲值包含低字节 `0x80`。

旧驱动把 `FIFOSTA & 0xff` 当成接收计数。空 FIFO 的固定 bit 7 因而被误读为
“有 128 个样本”，`capture=5000` 不等待 48 kHz 硬件节拍，而是以 CPU 速度反复
读取陈旧 `RXFIFO`，快速拼出一个尺寸正确、内容错误的 WAV。

A733 官方 Linux BSP 和 INMP441 数据手册还确认：

- A733 使用四个通用 `RXCHMAP0..3`，channel 0 位于 `RXCHMAP3`，不是旧式的
  两寄存器布局；
- INMP441 输出 24-bit 二补码、每声道 32-bit slot、标准 I2S 一位延迟；
- `L/R=GND` 选择左声道，启动后必须等待至少 2^18 个 SCK，3.072 MHz 下约
  85 ms；
- 官方 Linux `S24_3LE` 硬件路径使用 sample resolution 24、slot width 32、
  `RXOM=1`；APB 字右对齐且符号扩展，转 S16 应算术右移 8 位；
- 官方 A733 pinctrl 对 PB8/DIN0 使用下拉，因为 INMP441 在未选声道会让 SD
  进入高阻态。

## v136 修复

1. 只在 `FIFOSTA.RXA` bit 8 置位后读取一次 `RXFIFO`，计数只取 bits 6:0；
2. 采集以 sample resolution 24、slot width 32、`RXOM=1` 运行，S24→S16 使用
   算术 `>> 8`；
3. 初始化全部四个 RX map，把单声道 SDI0/slot 0 明确写入 `RXCHMAP3`；
4. PB8/DIN0 对齐官方 Linux pinctrl，启用内部下拉；仍建议模块 SD 到 GND 外接
   约 100 kΩ，并在 VDD 近端放置 0.1 µF 去耦；
5. 录制时先把整段样本写入 RAM，停止 I2S 后再写 SD。避免每 256 样本写文件时
   阻塞超过 128-word FIFO 在 48 kHz 下约 2.67 ms 的容量；
6. 新增墙钟时间、目标/实际消费 RX 字数、硬件 `RXCNT`、FIFO 峰值和 INTSTA
   诊断；只要
   实际时长过短、硬件字数不足、RX underrun/overrun 或帧数不足，就返回失败，
   不再把伪 WAV 报成成功。
7. 独立记录最近一次采集的模式、返回值和文件提交状态；后续执行 `probe` 或
   `off` 不会再覆盖采集结论。

MAX98357A 已实机通过的 S16 `tone=` 路径未改变。

## 构建与镜像

构建目录：

```text
quickly-openvela/cmake_out/cubie-a7z_nsh_v136_inmp441_fifo_fix
```

```text
full build: passed (1783/1783 targets)
kernel: quickly-openvela/cmake_out/cubie-a7z_nsh_v136_inmp441_fifo_fix/nuttx.bin
kernel bytes: 2199576
kernel SHA-256: 68adbe5ac9a258c44fe3f3ea4628ffd5323738b0993bfea255c11e6725a3cf5f

image: A733-A7Z-ALL-Files/openvela-a733-cubie-a7z-sd-i2s-v136-inmp441-rxfifo-candidate.img
image bytes: 2147483648
image SHA-256: 3146cc601ee5a59f6815dbb08701d43c6d993bef6a4f852d91444f02253eb358
embedded kernel bytes: 2199576
embedded kernel SHA-256: 68adbe5ac9a258c44fe3f3ea4628ffd5323738b0993bfea255c11e6725a3cf5f
ext4: clean
GPT: no problems found
```

镜像从 v133 已知可启动副本封装，仅替换第 3 分区的 openvela 内核；基础镜像未被
覆盖。`tools/verify-a733-image.sh` 已完成 ext4 只读检查、GPT 检查和镜像内核反向
哈希，均通过。GPT 工具仍报告基础镜像既有的第 4 分区未按 2048 sector 对齐提示，
但最终结论为 `No problems found`，且保留 16 MiB 空闲空间。

当前状态为 **build-passed / image-verified / hardware-pending**。只有完成下方
三场景录音验收后，才可把 INMP441 标记为 hardware-passed。

## 严格上板验收

只连接 INMP441：VDD=3.3 V、GND 共地、L/R=GND、SCK=PB5/pin12、
WS=PB6/pin35、SD=PB8/pin38。杜邦线尽量短于 10 cm，时钟线旁安排地线；若模块
没有自带 SD 下拉，增加约 100 kΩ 到 GND。

```sh
echo init > /dev/a733-audio
time "echo capture=5000 > /dev/a733-audio"
cat /dev/a733-audio
cp /data/inmp441.wav /data/models/v136-silence.wav

time "echo capture=5000 > /dev/a733-audio"
cp /data/inmp441.wav /data/models/v136-voice.wav

time "echo capture=5000 > /dev/a733-audio"
cp /data/inmp441.wav /data/models/v136-clap.wav

time "echo capture32=1000 > /dev/a733-audio"
cat /dev/a733-audio
ls -l /data/inmp441.wav /data/inmp441-raw32.bin
```

通过门槛：

- 5 秒录音的命令不能瞬间返回，纯采集约 5.1 秒，随后才是短暂文件写入；
- `capture-proof` 应包含 `mode=pcm16 status=0 committed=1 requested=5000`，并接近
  `elapsed=5100 target-rx=244800 consumed-rx=244800`；
- `hw-rx >= consumed-rx` 且不超过 128 字尾差，INTSTA 的 RX underrun/overrun
  位均为 0；
- WAV 必须是 480044 字节，raw 1 秒文件必须是 192000 字节；
- 静音 RMS 应显著低于说话，拍手应有清晰瞬态，语音必须能辨认；
- 只有满足以上全部条件，INMP441 才能提升为 **hardware-passed**。
