# A733 AI 桌宠音频阶段：UART4 TTS 与 I2S0

## v151：应用层与官方语音候选已交付

新增 `aipet audio` 本地录放、持久音量/增益/输出、隐藏凭据配置和官方ASR/Agent/TTS接入。
构建/镜像验证通过，新应用与云语音链路尚待实机；[分步测试与限制](AIPET_AUDIO_V151.md)。
v150以下通过记录及所有历史阶段保留。

## 最新：v150 录音文件 → I2S 扬声器回放通过

两次默认 ×5 录音、五次48kHz mono WAV回放，50%音量，用户确认拍手/说话清楚。
数字播放音量已有0..100接口，默认25%、重启不保留；不影响麦克风增益或UART TTS。
[实测与边界](../evidence/I2S_SPEAKER_V150_RUNTIME_ACCEPTANCE.md)；
标准audio/Media、云TTS→I2S和长稳尚未完成，以下阶段历史全部保留。

## v148 开发：左声道语音与可调增益

新icsvoice入口、测试流程和未完成边界见 [v148](ICS43434_V148_VOICE_GAIN.md)。
保留v147原始路径/幅度和通过镜像，新处理不等于标准audio框架或ASR已经完成。

## 最新：v147首次基础录音通过

用户确认ICS43434有声音；5秒WAV与日志统计一致，左槽有效、右槽全零，
24MHz波形支持48kHz/64时钟帧。音量偏小且未加增益。
[后续实测报告](../evidence/ICS43434_V147_RUNTIME_ACCEPTANCE.md)说明测量及限制。
本次没有改固件/增益；仍未完成标准audio框架、ASR或长稳。以下全部旧阶段记录保留。

## 当前入口：v147计数候选

v146首次上板因RXCNT/FIFO字数单位检查失败，未生成WAV。原始20kHz逻辑捕获不足以
解码总线；后续修正、证据和重新测试见 [v147](ICS43434_V147_COUNTER.md)。
麦克风声学通过仍待实测，保留下面全部阶段历史，不删除失败过程。

## 当前入口：v146 ICS43434（2026-10-15）

用户改用 MAX98357＋ICS43434 合并模块，新双槽录音、资料审核、供电/接线边界、
测试命令见 [ICS43434 v146](ICS43434_V146.md)。旧 INMP441 八组录音均未通过声学
验收，历史调试记录全部保留，后文按版本顺序阅读，不把早期传输结论扩大为有效收音。
新模块实机收音待测，当前未接 Agent/ASR 或标准 audio upper-half。

## v118 UART4 MMIO 映射修复——实机通过

- 镜像：`openvela-a733-cubie-a7z-sd-uart-mmio-v118-candidate.img`
- 镜像 SHA-256：`8330d6eeccc2f7736ccab2a567fdd109c87188656d4dd8809236877188dde685`
- 内嵌内核 SHA-256：`aa0ed5b717f136b3fadde2dbdb2abd26928acaec11a83261e944b558dd731356`
- v117 实机证据：CCU、PIO 和 UART 诊断值全部为零，发送仍在参数首字节
  `-110`；软件节点注册成功不代表硬件地址正确。
- v118 修复：主 CCU 从错误的 `0x02001000` 改为官方 DTS 的
  `0x02002000`；PIO 改用 sun60iw2 pinctrl HW type 4 的 `0x80` 首 bank、
  `0x80` 步长、`0x20/0x30` drive/pull 布局。
- 构建状态：完整 AArch64 编译/链接、ext4、GPT、内核回读校验通过。
- 实机状态：通过。诊断值为 `bgr=00010001`、PJ24/PJ25 function 4、
  `lcr=3`、`lsr=0x60`、`usr=0x06`；首次测试完成三帧参数初始化和一帧文本，
  状态为 `initialized=1 frames=4 failures=0 last=0 stage=complete`，重复播报
  继续返回 `frame sent (0)`。

## v117 UART4 硬件初始化修复候选

- 镜像：`openvela-a733-cubie-a7z-sd-uart-hw-v117-candidate.img`
- 镜像 SHA-256：`17658c8327cb6e015640212ba5bfdb3222779f8089a332a09bd82dff06fd54fe`
- 内嵌内核 SHA-256：`418fe8b6a84479289f29029097414c45cb3b629db73c957cd51f9c27e7b1886d`
- 内核大小：`1920592` 字节
- 构建状态：AArch64 全目标编译/链接、ext4、GPT 与内嵌内核回读校验通过。
- 实机状态：等待 FIFO 状态和实际发声复测，仍是候选镜像。

v116 已确认 `/dev/ttyS4` 存在，termios 配置不再返回 `-22`；但参数帧首字节
因 FIFO 始终不可写而超时，状态为 `stage=parameters`、`last=110`、
`frames=0`。v117 显式配置 APB-UART 24 MHz/1，并采用复位断言 → 门控开启 →
复位释放的初始化顺序。新增 `/dev/a733-uart4`，用于直接读取 CCU、pinmux、
LCR、LSR 和 USR，避免继续依赖推测。

## v116 UART termios 修复记录

- 镜像：`openvela-a733-cubie-a7z-sd-uart-termios-v116-candidate.img`
- 镜像 SHA-256：`e03cb1bc3e9a9795eb2dd45530cba00c1357b4b1980f0097c567fd108bad35f9`
- 内嵌内核 SHA-256：`d55f12403d7c55a09b9bc395e2514e8dade0a8c2803e1c089322d435e50dd65e`
- 内核大小：`1920592` 字节
- 构建状态：AArch64 全目标编译、链接通过；ext4、GPT 与内嵌内核一致性
  校验通过。
- 实机状态：设备节点与 termios 已通过，FIFO 首字节发送超时；由 v117 继续修复。

v115 实机执行 `aipet tts test` 返回 `-22`。根因是 UART4 的 `TCGETS`
把 termios 编码常量 `B9600` 错写进了保存数值波特率的 `c_speed`；
`cfsetospeed()` 将其规范化为数值 `9600` 后，`TCSETS` 又拿它和编码常量
比较，必然返回 `EINVAL`。v116 改为 `c_cflag` 保存 `B9600` 编码、
`c_speed` 保存数值 `9600`，并在 `aipet tts status` 增加 `node` 与 `stage`
诊断字段。

## v115 构建产物与失败记录

- 镜像：`openvela-a733-cubie-a7z-sd-audio-uart-v115-candidate.img`
- 镜像 SHA-256：`cf25ac887087e5bbad1863258225d2060c3c875706b84686d1ef8cdc6b01c834`
- 内嵌内核 SHA-256：`2d3af39ada75f72e42f127867637c2643142ec2d949c0117b1c62d8d5df80d08`
- 构建状态：完整交叉构建通过；ext4、GPT 和内嵌内核一致性检查通过。
- 实机状态：系统、Agent 和 I2S0 只读诊断正常；UART TTS 在 termios 配置
  阶段返回 `EINVAL(-22)`，已由 v116 修复，v115 不再用于 UART 验收。

## v122 I2S0 主动验收候选

I2S0 已从只读寄存器检查推进到可控的主动收发候选实现。它在启动时保持被动，
只有写入 `/dev/a733-audio` 的 `init`、`tone` 或 `capture` 命令才会设置音频 PLL、
PB5..PB8 与 I2S0，因而未接模块或接线错误不会阻塞整机启动。

当前固定格式为 48 kHz、标准 I2S、双 32-bit slot、BCLK 3.072 MHz、无 MCLK：

```text
echo probe > /dev/a733-audio
cat /dev/a733-audio
echo init > /dev/a733-audio
echo tone=1000,2000 > /dev/a733-audio
echo capture=3000 > /dev/a733-audio
ls -l /data/inmp441.wav
echo off > /dev/a733-audio
cat /dev/a733-audio
```

`tone=频率,毫秒` 同时向左右声道送正弦波；`capture=毫秒` 从左声道采集并写成
48 kHz/16-bit/mono WAV。命令范围受限，PLL、TX/RX FIFO 等待都有超时保护。
这是寄存器和接线验收层；真机确认时钟、发声和录音后，再把同一底层封装为
标准 NuttX/openvela I2S 与 audio lower-half，并接入 Agent PCM 接口和 DMA。

## 当前完成范围

- 新增 A733 UART4 字符设备 `/dev/ttyS4`，固定为 9600 8N1；使用官方
  sun60iw2/A733 BSP 的基址、CCU 门控和 PJ24/PJ25 复用定义。
- `aipet` 已接入 TW-TTS 串口合成模块，使用模块原生 UTF-8 编码选择值
  `0x04`，避免在系统内携带 GB2312 转换表。
- UART TTS 为可选持久功能；启用后，普通 Agent 回复会自动发送到模块，
  `route` 检查命令不会播报。
- `/dev/a733-audio` 提供被动探测、显式初始化、MAX98357A 正弦测试和 INMP441
  WAV 采集。PB4/MCLK 始终不切换，因为两块器件都不需要 MCLK。
- `PcmSpeechOutput` 保留官方云端 TTS PCM 到 I2S 播放层的接口；主动播放和
  INMP441 采集下半层仍需上板测钟后完成。

## 接线

UART TTS（优先验证）：

- A7Z 物理 Pin 16 / PJ24 / UART4-TX -> TTS 模块 RX
- A7Z 物理 Pin 18 / PJ25 / UART4-RX <- TTS 模块 TX（模块不返回数据时可不接）
- A7Z GND <-> TTS 模块 GND
- 供电按模块铭牌要求；不得仅凭信号电平猜测电源电压

MAX98357A：

- PB5 / Pin 12 -> BCLK
- PB6 / Pin 35 -> LRC/WS
- PB7 / Pin 40 -> DIN
- GND 共地；VIN、GAIN、SD 按具体模块手册连接
- 一般不需要 MCLK

INMP441：

- PB5 / Pin 12 -> SCK/BCLK
- PB6 / Pin 35 -> WS/LRCK
- PB8 / Pin 38 <- SD
- L/R -> GND（先使用左声道）
- VDD -> 3.3V，严禁接 5V；GND 共地

MAX98357A 与 INMP441 可以共用 PB5/PB6 时钟线。

## 上板测试

先断电接线，只接 UART TTS，启动后执行：

```text
ls /dev
cat /dev/a733-uart4
aipet tts status
aipet tts test "串口语音测试成功"
aipet tts on
aipet ask "请只回答：语音链路成功"
```

预期诊断应包含 `gate=1 reset=1 functions=4/4`，并且 `thre=1` 或 `tfnf=1`；
状态应包含 `node=present`。`aipet tts test` 应显示 `frame sent (0)` 且
模块发声；启用后，
`aipet ask` 的文字回复仍出现在终端，同时从 UART4 自动播报。如果终端显示
发送成功但没有声音，先检查模块供电、共地、PJ24 是否接模块 RX，以及模块
是否兼容 `FD + 长度 + 01 + 04(UTF-8) + 文本` 协议。

`aipet tts on` 会创建
`/data/ai_agent/config/uart_tts.enabled`，以后启动 Agent 后自动播报；执行
`aipet tts off` 可以关闭。即使 UART 模块故障，文本对话仍继续，终端会输出
`UART TTS failed` 诊断。

I2S0 先进行安全检查，再分别测试播放与采集：

```text
echo probe > /dev/a733-audio
cat /dev/a733-audio
echo init > /dev/a733-audio
echo tone=1000,2000 > /dev/a733-audio
echo capture=3000 > /dev/a733-audio
ls -l /data/inmp441.wav
cat /dev/a733-audio
```

请回传完整输出、扬声器是否发声，以及 `/data/inmp441.wav`。可用 FTP/SCP
取回 WAV 后检查是否为 48 kHz 单声道并实际试听。若 `tone` 没声音，不要把
MAX98357A 的 `OUTP` 或 `OUTN` 任一端接地；确认 `SD_MODE` 已拉高、功放与板子
共地。若 WAV 静音，确认 INMP441 使用 3.3 V、`L/R` 接地，并在 SD 到 GND
之间使用模块自带或外加约 100 kΩ 下拉。

## 尚未宣称完成的部分

- MAX98357A 已在 v133 实机发声通过；INMP441 已连续两轮完成原始 FIFO 与 WAV
  采集，证明时钟/FIFO/文件链路工作，但 WAV 回放暴露 S24→S16 位移错误；修复
  镜像回归前不能把麦克风 PCM 内容标记为通过。
- 当前为有界 polling 验收实现，还不是 DMA/标准 audio upper-half，不能把
  编译通过写成硬件已通过。
- UART4/TTS 的 v115 `-22` 已在 v116 修复；v116 的 FIFO `-110` 已在 v117
  加入时钟/复位修复与寄存器诊断；v117 进一步暴露 MMIO 映射错误，已由
  v118 修复并完成实机验收。协议测试、路由回归、AArch64 完整链接、寄存器
  状态、首次初始化和重复播报均通过。

## v122 实机结果与 v123 修复

v122 实机能够完成 `init`、96000 样本测试音和 144000 样本/288044 字节 WAV
采集，PLL lock、FIFO 超时计数也正常，但 MAX98357A 没有声音。检查代码发现测试
音相位索引在 64 位右移后才转换为 32 位，约一个波形周期后就会越过 32 项正弦
表。v123 改为先截取相位低 32 位再取最高 5 位，消除越界读取。

v122 每次成功命令后状态仍显示 `last=-22`，原因是设备写结束时的零长度写被
错误地当作空命令。v123 对零长度写直接返回，不再覆盖上一条命令状态，并新增：

```text
stream-proof: tx-count=... rx-count=... capture-nonzero=... min=... max=...
```

这些字段用于区分“控制器完成了循环”和“线上真的有样本”。若修复后仍无声，
下一步必须核对 MAX98357A `SD_MODE` 为高电平、供电和共地；扬声器只能跨接
`OUTP/OUTN`，任一输出端都不能接地。INMP441 采集以 `capture-nonzero > 0`
且 `min/max` 随声音变化作为初步通过条件。

## v123 实机定位与 v124 声道映射修复

v123 实测 TX 计数达到 240031，但 MAX98357A 无声；RX 计数达到 21730，生成
288044 字节 WAV，但 `capture-nonzero=0 min=0 max=0`。这证明控制器和时钟循环
在运行，却没有把低声道样本路由到 DOUT0/DIN0。厂商 A733 Linux I2S 驱动确认
该控制器的 `CHMAP0` 对应高 8 个 slot，`CHMAP1` 才对应 slot 0..7。旧实现把
0、1 号声道写到了 `TX0CHMAP0/RXCHMAP0`。

v124 将 TX/RX 低声道映射 `0x10` 写入 `TX0CHMAP1/RXCHMAP1`，清零 MAP0，且
状态报告新增 `channel-map` 寄存器回读。另增加“去除 CR/LF 后长度为零”的写入
过滤，避免 NSH 将单独换行误报为 `last=-22`。软件修复后若 TX 仍无声，才继续
检查 MAX98357A 的 `SD_MODE`、供电、共地与 OUTP/OUTN 差分接法；若 RX 仍全零，
检查 INMP441 的 3.3 V、L/R=GND 和 SD→PB8。

## v132 I2S0 官方 FIFO 语义对齐候选

v131 作为已经通过的 NPU/LVGL 基线保留且不覆盖。v132 音频候选针对 v123/v124
“控制器计数前进、外部仍无声音/采集全零”的现象，重新逐项对照 A733 厂商 Linux
I2S 驱动和两颗器件数据手册：

- 48 kHz、2×32-bit slot、3.072 MHz BCLK、标准 I2S 一位延迟保持不变；
- PB5/PB6/PB7/PB8 的 sun60iw2 mux=3 已由官方 pinctrl 表确认；
- PLL_AUDIO1_DIV5 → 614.4 MHz、模块 /25 → 24.576 MHz、BCLK /8 已由官方 CCU
  驱动确认；
- 默认 `tone=` 改为与 Linux `S16_LE` 相同的 sample-resolution=16、TXIM=1 和
  16-bit FIFO 写；旧的 32-bit 高位对齐写法保留为 `tone32=` 对照；
- INMP441 采集切换为 24-bit sample resolution，并先丢弃 100 ms（器件规定上电后
  前 2^18 个 SCK、约 85 ms 输出无效）；
- 新增 `capture32=` 原始 FIFO 字抓取和 `raw-nonzero/first-raw/last-raw` 诊断；
- 状态新增 PB5..PB8 mux 寄存器回读，避免只看软件配置而未验证真实 pinmux。

建议先只接 MAX98357A：

```text
echo init > /dev/a733-audio
cat /dev/a733-audio
echo tone=1000,3000 > /dev/a733-audio
echo tone32=1000,3000 > /dev/a733-audio
cat /dev/a733-audio
```

`tone=` 有声而 `tone32=` 无声即可直接证明旧问题是 FIFO 样本对齐。两者都无声时，
必须实测 PB5≈3.072 MHz、PB6≈48 kHz、PB7 数据活动，并确认 MAX98357A `SD_MODE`
拉高、扬声器跨接 OUTP/OUTN。再单独接 INMP441：

```text
echo capture32=1000 > /dev/a733-audio
cat /dev/a733-audio
ls -l /data/inmp441-raw32.bin
echo capture=3000 > /dev/a733-audio
cat /dev/a733-audio
ls -l /data/inmp441.wav
```

`warmup` 应为 4800；对着麦克风说话时 `raw-nonzero`、`capture-nonzero` 应大于零，
`min/max` 应变化。若原始字仍全零，优先检查 INMP441 CHIPEN/模块焊接、3.3 V、
L/R=GND 和 SD→PB8，而不是继续修改 WAV 层。

## v133 MAX98357A 实机通过

稳定供电下 v133 正常经过 `N8`、`X1` 并进入 NSH。只连接 MAX98357A 时：

- `echo tone=1000,3000 > /dev/a733-audio` 声音清晰且较大；
- `echo tone32=1000,3000 > /dev/a733-audio` 同样有声，但音量明显较弱；
- 两条路径均完成 144000 样本，`tx-count=144031`、`timeouts=0`；
- 16 位路径 `fmt0=00001f37`，32 位对照路径 `fmt0=00001f77`。

这直接验证了 v132/v133 对 FIFO 样本宽度的判断：正式播放应采用与 Linux
`S16_LE` 一致的 `tone=` 路径，`tone32=` 只作为诊断对照。MAX98357A 播放链路
现标记为 **hardware-passed**。本次未连接 INMP441，不能把麦克风标记为通过。

## v133 INMP441 传输通过、PCM 转换失败

INMP441 单独接入后，`capture32=1000` 连续两轮都得到 48000/48000 个非零
原始 FIFO 字，`warmup=4800`、`timeouts=0`，文件大小均为 192000 字节。
`capture=3000` 连续两轮都得到 144000/144000 个非零 PCM 样本，WAV 大小均为
288044 字节；两轮范围分别为 `-118..115` 和 `-109..118`。

后续对安静、说话、拍手三段 5 秒 WAV 的离线分析表明，三者 RMS 都约为 82，
十个时间片的能量几乎不变，试听均为电流噪声。文件大小一致是固定采样时长的
正常结果，但内容不随声学输入变化，不能标记为录音通过。

原始字 `ffa66b34`、`00355595` 等同时揭示了根因：A733 在 24-bit sample
resolution、`RXOM=1` 下采用厂商 `S24_3LE` FIFO 布局，有效有符号样本位于
bits 23:0；旧代码右移 16 位，实际只保留了最高约 8 位信号。修复应右移 8 位
完成 S24→S16。PB8、时钟、FIFO 和文件传输标记为 **hardware-passed**，PCM
内容在修复镜像重新试听前仍为 **hardware-pending**。

## v135 失败取证与 v136 RX FIFO 节拍修复

v134/v135 的“5 秒文件”和“48000 个非零 raw 字”不是有效录音证据。对 v135
三段文件的离线取证表明，72% 以上样本连续重复，静音/语音/拍手 RMS 最大只差
0.048 dB；板端软件读满 52800 个 warmup+raw 字时，硬件 `RXCNT` 只有 13234。

A733 手册给出了决定性解释：`FIFOSTA` bit7 是固定为 1 的 `PLACE_HOLDER`，
真正的“至少一个字可读”是 bit8 `RXA`，计数只有 bits6:0。旧代码用 `&0xff`
读取空 FIFO 状态 `0x80`，因而以 CPU 速度重复取得 stale RXFIFO。v136 已改为：

- 必须等待 bit8 后才读一个字，计数严格掩码 `0x7f`；
- 使用官方 Linux `S24_3LE` 的 SR24/SW32/RXOM1，并以算术 `>>8` 转 S16；
- 对 A733 通用 `RXCHMAP0..3` 全部显式初始化，channel0 位于 MAP3；
- PB8/DIN0 启用与官方 DTS 一致的下拉；
- 先把整段数据采入 RAM，停止 I2S 后写临时文件，close 成功后再替换正式文件；
- 增加真实耗时、期望/硬件 RX 字数、FIFO 峰值、RXU/RXO 等硬门槛。

5 秒采集应耗时约 5.1 秒，而不是瞬间结束；状态必须显示约
`target-rx=244800 consumed-rx=244800` 且 `hw-rx>=consumed-rx`、无 RXU/RXO。
完整命令和证据见
`docs/evidence/i2s-v136/build-summary.md`。
