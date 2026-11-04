# v138 INMP441 采样矩阵候选

## 已有实机边界

v136 已证明 RXA 节拍、5100 ms 采集持续时间、48 kHz RXCNT 和 RAM/文件提交。
用户提供的 v137 实机结果为 right 静音、left 电流声；left 的目标/消费 RX 均为
244800、硬件 RX 为 244801。半帧映射有效，但两侧均没有真实语音。
这些结果与 SD 未形成有效声学 PCM 一致；具体接线、供电、模块或边沿原因尚未由
物理波形确认，不能仅凭数字图案断定某个硬件故障。

## v138 实现

`capturecfg=<slot>,<bclk>,<pull>,<ms>`：slot=0/1，BCLK 极性=0/1，PB8 pull=0/2
（禁用/下拉），持续时间 100..10000 ms。固定 LRCK polarity=0、SR24/SW32、
RXOM1、RX offset=1、100 ms warmup。保留原有 FIFO、时长检查和 RAM/原子提交。
旧 capture/captureleft/captureright/capture32 命令保持原路径与默认选择，且显式恢复
normal BCLK、PB8 下拉；tone/tone32 恢复 normal BCLK，避免上一组反相配置泄漏。

每组使用独立 WAV 和临时文件，重复同一配置只替换该组。`cat /dev/a733-audio`
记录本次 slot/BCLK/PB8 pull、采集配置寄存器快照、输出路径、min/max、zero、
sign-transitions、sum-square、raw-low55/raw-lowaa。平方和及其他 PCM 统计在停止 RX
后计算，单位为 S16；RMS = sqrt(sum-square / samples)。符号跳变比较相邻样本的
符号，零按非负处理；55/aa 计数针对 warmup 后原始 FIFO 低字节。

厂商 BSP `snd_sunxi_i2s.h/.c` 确認 FMT0 bit7 为 BCLK_POLARITY，bit19 为
LRCK_POLARITY，NB_NF=(0,0)、IB_NF=(1,0)。
同一厂商 Cubie A7Z `linux-5.15/board.dts:427..431` 的 PB8 `i2s0_din0`
使用 `bias-pull-down`，`linux-6.6/board.dts:319..323` 则使用 `bias-disable`。
因此保留两组显式对照，不把某一配置提前判为修复。
[INMP441 官方数据手册](https://invensense.tdk.com/wp-content/uploads/2015/02/INMP441.pdf)
规定 24-bit I2S、每帧 64 SCK、非驱动阶段 SD 高阻，以及约 100 kΩ 外部下拉。

## 实机矩阵命令

刷写 v138 后，先执行 `echo init > /dev/a733-audio`。每组均在前 1 秒保持安静、
第 2 秒连续说话、第 3 秒拍手；每条命令后立即记录完整 `cat` 报告。

```sh
echo capturecfg=0,0,2,3000 > /dev/a733-audio
cat /dev/a733-audio
echo capturecfg=0,1,2,3000 > /dev/a733-audio
cat /dev/a733-audio
echo capturecfg=0,0,0,3000 > /dev/a733-audio
cat /dev/a733-audio
echo capturecfg=0,1,0,3000 > /dev/a733-audio
cat /dev/a733-audio
echo capturecfg=1,0,2,3000 > /dev/a733-audio
cat /dev/a733-audio
echo capturecfg=1,1,2,3000 > /dev/a733-audio
cat /dev/a733-audio
echo capturecfg=1,0,0,3000 > /dev/a733-audio
cat /dev/a733-audio
echo capturecfg=1,1,0,3000 > /dev/a733-audio
cat /dev/a733-audio
```

| slot,bclk,pull | 输出（/data/） | 预期 fmt0 | rxmap3 | PB8 pull bits17:16 |
| --- | --- | --- | --- | --- |
| 0,0,2 | inmp441-left-normal-pd.wav | 00001f57 | 0 | 2 |
| 0,1,2 | inmp441-left-invert-pd.wav | 00001fd7 | 0 | 2 |
| 0,0,0 | inmp441-left-normal-none.wav | 00001f57 | 0 | 0 |
| 0,1,0 | inmp441-left-invert-none.wav | 00001fd7 | 0 | 0 |
| 1,0,2 | inmp441-right-normal-pd.wav | 00001f57 | 1 | 2 |
| 1,1,2 | inmp441-right-invert-pd.wav | 00001fd7 | 1 | 2 |
| 1,0,0 | inmp441-right-normal-none.wav | 00001f57 | 1 | 0 |
| 1,1,0 | inmp441-right-invert-none.wav | 00001fd7 | 1 | 0 |

每组预期 capture status=0、committed=1、samples=144000、target/consumed RX=148800、
elapsed 约 3100 ms、WAV=288044 字节。这些只证明传输。
**录音通过必须可辨认真实语音，且安静/语音/拍手的内容与波形/RMS 有声学变化。**
重复测试前先另存文件；不要只比较整段 RMS 或凭非零计数宣称通过。

若八组均无声学变化，停止仅改位移或录制时长的软件试错。使用逻辑分析仪/示波器
检查 PB5≈3.072 MHz、PB6≈48 kHz，以及 PB8 在选中半帧的 24-bit 数据是否随声音
变化、未选半帧是否高阻。检查 SD 是否实际接 PB8/物理 pin38、3.3 V/共地、L/R
电平、CHIPEN 高电平（若引出）、近端 0.1 µF 去耦及杜邦线；必要时更换麦克风。

## 恢复与构建

开发前：`87b626897564412661a6926d23d6f7e62243c734`。
恢复 bundle：`A733-A7Z-ALL-Files/a733-i2s-v138-before-matrix.bundle`，已通过 bundle verify。
bundle SHA-256：`bd479b05388e3a583e6098dc3b89ffa8f9220b334dfe4c417d2c7f98d6da75f5`。
驱动构建源提交：`34d087f`。
v137 镜像保留，作为只读封装底图；v138 使用新文件名。

工具链：`aarch64-none-elf-gcc (GCC) 13.4.0`，bundled CMake `3.23.0`、Ninja `1.12.1`，WSL
Python `3.14.4`。官方 nuttx/apps/vendor/allwinnertech/packages/ai_agent 的现有
`.git` 文件无法由 Git 解析，因此本轮不宣称已读取官方提交号，也不修改其元数据。
对构建包装器暂存文件及 aipet/aipetllm 目录进行 SHA-256 清单比对来确认恢复。

```sh
OPENVELA_WORKSPACE=/mnt/d/My-Program/AI-Agent-Program/GPTsprogram/Openvela/quickly-openvela \
A733_BUILD_DIR=/mnt/d/My-Program/AI-Agent-Program/GPTsprogram/Openvela/quickly-openvela/cmake_out/cubie-a7z_nsh_v138_inmp441_matrix \
bash tools/build-a733-wapi.sh
```

## 构建、测试与镜像结果

```text
full build: passed (2730/2730 targets, 06:25)
source commit: 34d087f
kernel: quickly-openvela/cmake_out/cubie-a7z_nsh_v138_inmp441_matrix/nuttx.bin
kernel bytes: 2203816
kernel SHA-256: bbe97a27643bc5c8f7441290b58bd049b66c47a822dfb7c54a0f2d42a8ab9cc2

image: A733-A7Z-ALL-Files/openvela-a733-cubie-a7z-sd-i2s-v138-inmp441-matrix-candidate.img
image bytes: 2147483648
image SHA-256: 3d19c04540fc433567694b3030dba4b5c74a3f9ed041aa135945d5746723bff1
embedded kernel bytes: 2203816
embedded kernel SHA-256: bbe97a27643bc5c8f7441290b58bd049b66c47a822dfb7c54a0f2d42a8ab9cc2
ext4: clean (before/after packaging and final image extraction)
GPT: No problems found
official restoration: 96 path entries identical
build.config SHA-256: 8f9226ef0f35feb55e57d975fab98691f65a21244502536dc2558ffd30c2951f
```

`python3 tools/test-a733-i2s-matrix.py` 编译实际驱动辅助函数并通过：8 组配置、
持续时间边界、16 个无效/溢出参数、寄存器位隔离与反相→正常/下拉→禁用恢复、
已知 PCM/raw32 统计、480000 个满幅样本的 64 位平方和及诊断报告格式/分块读取。
主机检查使用 `-Wall -Wextra -Werror -fsanitize=undefined`；不代表物理 I2S 通过。
最终 BIN 已静态确认含 `capturecfg=`、独立矩阵路径格式和 `capture-stats` 报告。

本目录保存完整 `build.log`、实际 `build.config`、`host-test.log`、`image.log`、
官方暂存前后哈希清单和 `official-restore.log`。镜像从 v137 副本封装，仅替换
第 3 分区内核；v137 未覆盖。GPT 第 4 分区沿用底图的 2048-sector 对齐提示，
`sgdisk -v` 的最终结果仍为 `No problems found`。

构建时状态：**source-ready / build-passed / image-verified / hardware-pending**。

## 本轮实机更新

用户随后提供启动/八组命令日志和八份 WAV，已确认进入 NSH 且矩阵全部完成。
WAV 的样本数、min/max、平方和和符号跳变与日志精确匹配；配置寄存器回读也符合命令。
左侧四组 RMS=21152..21159，仍为强干扰特征；右侧四份文件完全相同，所有样本
固定为 -11437，去直流 RMS=0。真实语音验收失败，不能将右侧恒值当成正常静音。
此前“已排除半帧配置”的推断需收窄为“寄存器切换和不同输出已确认”；RX_SLOT_NUM=0
却映射 slot 1 的配套行为仍待核验。完整数据及物理检查步骤见 `audio-analysis.md`。
当前状态为 **build-passed / image-verified / boot-observed / transport-passed /
audio-content-failed**；多次冷启动和长稳仍未执行。
