# v135 INMP441 单声道 S32 接收候选

## 触发证据

v134 实机生成的 `v134-silence.wav`、`v134-voice.wav` 和
`v134-clap.wav` 均为 240000 个 48 kHz S16 样本，但三者 RMS 分别约为
21097、21111、21176，听感均为电流声。文件长度正确只证明定长文件写入成功，
不证明麦克风数据有效。

v134 的 `S24_3LE` 路径把错误数字流放大了 256 倍。安静、语音、拍手之间没有
合理的能量差异，因此不能归咎于普通底噪，也不能标记为硬件通过。

## 修复依据

A733 官方 Linux BSP `snd_sunxi_i2s.c` 对 `S24_LE/S32_LE` 使用：

- sample resolution = 32；
- RXOM = 1；
- 单声道 capture 时 `RX_SLOT_NUM=0`、`RX_CHSEL=0`；
- RX FIFO channel 0 映射到 slot 0。

v135 对齐此路径：

1. INMP441 `L/R=GND` 时只启用一个 RX 通道，不再启用两路后丢弃第二个 FIFO 字；
2. RX channel 0 只映射到左声道 slot 0；
3. 采集使用 32-bit sample resolution 和 RXOM=1；
4. 从 S32 容器算术右移 16 位生成 S16 WAV；
5. 保留 100 ms 麦克风启动 warmup、超时保护和原始 FIFO 抓取。

MAX98357A TX 仍保持双声道，已经通过的 `tone=` 路径不变。

## 上板验收

仅连接 INMP441，确认 VDD=3.3 V、GND 共地、L/R=GND、SCK=PB5、WS=PB6、
SD=PB8，然后分别执行：

```sh
echo init > /dev/a733-audio
echo capture=5000 > /dev/a733-audio
cp /data/inmp441.wav /data/models/v135-silence.wav
echo capture=5000 > /dev/a733-audio
cp /data/inmp441.wav /data/models/v135-voice.wav
echo capture=5000 > /dev/a733-audio
cp /data/inmp441.wav /data/models/v135-clap.wav
echo capture32=1000 > /dev/a733-audio
cat /dev/a733-audio
```

录制语音和拍手时保持线材位置不变。通过标准不是“文件非零”，而是：语音可辨、
拍手有明显瞬态、安静段 RMS 显著低于语音/拍手，并且无周期性满量程数字噪声。

## 构建与镜像校验

- 全量构建：2730/2730 成功；
- 内核：`quickly-openvela/cmake_out/cubie-a7z_nsh_v135_inmp441_s32mono/nuttx.bin`；
- 内核 SHA-256：`aab132f34b836c48ebe1821d878ec7f7a92ac474aa56c71255d376a4e9a8884a`；
- 镜像：`A733-A7Z-ALL-Files/openvela-a733-cubie-a7z-sd-i2s-v135-inmp441-s32mono-candidate.img`；
- 镜像 SHA-256：`c38e1ed47ff486658434894223a8272f7845c0578acff4d5ba5c8997d7b2a110`；
- ext4：只读检查通过；
- GPT：`sgdisk -v` 无错误；
- 镜像内核：2199512 字节，哈希与构建产物一致。

## 实机复测结论

v135 已被实机复测否决。三段文件虽然都是合法的 5.000 秒 WAV，但约
72.4%～72.9% 的样本是连续重复旧值，静音/说话/拍手 RMS 只相差 0.048 dB；
`capture32=1000` 由软件读出 52800 个 warmup+保存字时，硬件 `RXCNT` 仅为
13234。根因不是普通底噪，而是驱动把 `FIFOSTA` 固定为 1 的 bit 7
`PLACE_HOLDER` 当成 RX 计数最高位，空 FIFO `0x80` 被误报成 128 字并被高速
重复读取。

当前状态：**build-passed / image-verified / hardware-failed**。修复和严格验收见
`docs/evidence/i2s-v136/build-summary.md`。
