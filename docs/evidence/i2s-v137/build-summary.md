# v137 INMP441 左右半帧选择修复候选

## 已确认的根因范围

v136 已在实机证明 48 kHz RX FIFO 节拍、5 秒持续时间、WAV 大小和硬件计数正确，
因此本轮不再修改计数或文件写入。实际原始流在静音、语音和拍手时都接近满幅，
低字节反复出现 `55/aa/66/99`，属于 PB8 在未驱动时隙采入 BCLK/WS 串扰，不能
解释为有效 INMP441 PCM。

INMP441 是单声道三态器件：L/R 只决定它驱动 64-clock I2S 帧中的哪个 32-clock
半帧。厂商资料所附示例还明确记录了部分模块/控制器中“SEL 接地但软件需要选
RIGHT”的现象。因此 v137 将 FIFO channel 0 显式映射到 wire slot，并保留两侧
强制入口。

## 代码变化

- `capture=<ms>`、`capture32=<ms>` 默认使用 slot 1（right）。
- `captureleft=<ms>` 强制 slot 0，`captureright=<ms>` 强制 slot 1。
- `/dev/a733-audio` 报告本次采集的 slot、方向和最终 RX map。
- 保留 v136 的 SR24/SW32、RXOM1、100 ms warmup、RXA 节拍、RAM 缓冲和原子提交。

## 实机验收

```sh
echo init > /dev/a733-audio
time "echo captureleft=3000 > /dev/a733-audio"
cp /data/inmp441.wav /data/models/v137-left.wav
time "echo captureright=3000 > /dev/a733-audio"
cp /data/inmp441.wav /data/models/v137-right.wav
cat /dev/a733-audio
```

对着麦克风持续说话并在中间拍手。有效一侧应能听到语音，静音与拍手波形/RMS
必须明显不同；仅“文件非零、大小正确”不算通过。如果两侧仍都是 `55/aa` 电流
声，则软件已覆盖左右半帧，剩余检查必须转向实物：PB8 是否确实接模块 SD、L/R
和 CHIPEN 电平、麦克风 3.3 V/GND、近端 0.1 uF 去耦，以及 SD 到 GND 的约 100 kΩ
下拉。

## 构建与镜像

```text
full build: passed (2730/2730 targets)
kernel: quickly-openvela/cmake_out/cubie-a7z_nsh_v137_inmp441_slot_fix/nuttx.bin
kernel bytes: 2199576
kernel SHA-256: 07a3a5432ad713c1a995fb2396586a4834c09894c59e5542846474310b147c47

image: A733-A7Z-ALL-Files/openvela-a733-cubie-a7z-sd-i2s-v137-inmp441-slotfix-candidate.img
image bytes: 2147483648
image SHA-256: 580178196fe68ac7db0a0591360bdca13f668c5c8417971cc2ce7b18aa618191
embedded kernel bytes: 2199576
embedded kernel SHA-256: 07a3a5432ad713c1a995fb2396586a4834c09894c59e5542846474310b147c47
ext4: clean
GPT: no problems found
```

镜像从已实机启动的 v136 复制，仅替换第 3 分区内核；v136 未被覆盖。第 4 分区
沿用基础镜像既有的 2048-sector 对齐提示，`sgdisk -v` 最终结论仍为
`No problems found`。本版本状态为 **build-passed / image-verified /
hardware-pending**。

## 续接实机结果（用户提供，2026-10-11）

right/slot 1 完全无声，left/slot 0 仍为电流声。left elapsed=5100 ms、
target/consumed RX=244800、hw RX=244801、capture-nonzero=240000、
min/max=-30391/30394、first/last raw=`ffd35536`/`ff96699a`。
映射切换有效，但没有真实语音；本版本录音状态更新为
**transport-passed / audio-content-failed**。下一轮 v138 对照 BCLK 极性和 PB8 下拉。
