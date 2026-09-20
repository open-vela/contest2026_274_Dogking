# A733 AI 桌宠音频阶段：UART4 TTS 与 I2S0

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

## 当前完成范围

- 新增 A733 UART4 字符设备 `/dev/ttyS4`，固定为 9600 8N1；使用官方
  sun60iw2/A733 BSP 的基址、CCU 门控和 PJ24/PJ25 复用定义。
- `aipet` 已接入 TW-TTS 串口合成模块，使用模块原生 UTF-8 编码选择值
  `0x04`，避免在系统内携带 GB2312 转换表。
- UART TTS 为可选持久功能；启用后，普通 Agent 回复会自动发送到模块，
  `route` 检查命令不会播报。
- 新增 `/dev/a733-audio` I2S0 非破坏诊断节点，读取控制器和时钟寄存器，
  但在实机测量音频 PLL 前不会改写 PB4..PB8，也不会输出错误采样率的时钟。
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

I2S0 只进行安全检查：

```text
echo probe > /dev/a733-audio
cat /dev/a733-audio
```

请回传完整输出。下一阶段将根据 `ccu` 和 `regs` 实测值确定 PLL_AUDIO
父时钟与精确分频，然后依次启用 MAX98357A 48/16 kHz PCM 播放、INMP441
32-bit slot/24-bit 单声道采集，并接入官方 Agent 语音通道。

不要在 v115 上把 MAX98357A 或 INMP441 的“没有声音/没有采样”判定为故障；
本版有意不驱动 BCLK/LRCK，目的是先取得安全的时钟树基线。

## 尚未宣称完成的部分

- MAX98357A 尚未输出音频；当前没有切换 I2S 引脚复用。
- INMP441 尚未采样；当前没有注册 PCM capture 设备。
- UART4/TTS 的 v115 `-22` 已在 v116 修复；v116 的 FIFO `-110` 已在 v117
  加入时钟/复位修复与寄存器诊断；v117 进一步暴露 MMIO 映射错误，已由
  v118 修复并完成实机验收。协议测试、路由回归、AArch64 完整链接、寄存器
  状态、首次初始化和重复播报均通过。
