# v177：I2S 语音、官方 Agent 与 STM32 UART4 控制候选

2026-10-23。用户要求暂停旧 UFS 启动链清理，优先让 A7Z 通过 UART 控制 STM32。
本版不擦 UFS、不启用此前 Linux 清理工具、不推进摄像头；保留 v175 镜像及所有历史文档。

## 链路与边界

```text
ICS43434 → I2S0 48kHz S16/增益x5 → FIR降采样16kHz
    → 官方云端ASR → 原文安全校验 → Agent产品控制路由
      ├─ 明确短指令：直接进入受限UART适配器，不必等LLM
      ├─ aipet cloud：官方Agent的stm32_control工具（须匹配本轮原文）
      └─ 普通聊天：既有官方云Agent / CPU本地LLM回退
    → UART4 115200 8N1 → STM32 USART1 → 按SEQ匹配ACK/NAK
    → 真实接收结果 → 官方云TTS → MAX98357/I2S扬声器，兼容LVGL表情通知
```

这是**云端ASR/云端TTS**整合候选，不是已经完成本地 NPU 流式 ASR/TTS。
已有 NPU KWS、npuref、fbank 诊断保留，不据此自动驱动电机。I2S、UART 双板以及实际
语音识别仍需本版本实机验证。文字控制不依赖云端ASR；无网络也能直接验证UART。

## 下位机来源与实际协议

源码：[STM32C8T6Mini-Play](https://github.com/hxy299/STM32C8T6Mini-Play/tree/e83a9cac8a0e4a775a38910bdaf08c68bfdffc8c)。
锁定提交 `e83a9cac8a0e4a775a38910bdaf08c68bfdffc8c`。下载到忽略目录
`out/stm32c8t6mini-reference-20261023-proxy/`，不修改/推送下位机仓库。
生产编解码独立实现；测试直接编译其原始 C encoder 对照，不复制 ST 驱动。

| 字段 | 约定 |
| --- | --- |
| 帧 | A5 / TYPE:u8 / SEQ:u16LE / OP:u8 / LEN:u8 / PAYLOAD / CRC:u16LE |
| CRC | CCITT-FALSE，1021，初始FFFF；123456789→29B1 |
| 最大载荷 | 实际 app_config.h 为200，README旧描述64不能覆盖源码 |
| MOVE 01 | dir0..3，speed0..3，steps:u16LE；上位机限制1..1024，禁止FFFF连续运动 |
| SERVO 02 | angle:i8，duration:u16LE；限制-45..45度、100..2000ms |
| PING 06 | 空载荷；PONG为TYPE2/OP86，11字节含uptime/fault/config/flags/impl |
| MOTOR_STOP 0C | 空载荷；不携带清除热保护的bit0 |
| ACK / NAK | TYPE2/OP81或82；payload[0]为状态，MOVE附加距离不是状态 |
| 事件 | FAULT93、MOTION_DONE94、CONFIG_CHANGED96；没有SEQ关联的完成事件只用于诊断 |

MCU TTS/TOF 操作码目前未实现，不能当作本版功能；不发送它们。
UART4不再运行TW-TTS，旧 `aipet tts on/test` 明确拒绝，旧启用标记不能抢占串口。
旧 audio.conf 的 output=0 只在内存映射到 I2S，不改原配置文件；显式off仍保留。

## 接线

断电后移除UART4上的原TTS模块；不要让两个发送端并接。

| A7Z 40针排针 | STM32F103C8T6 |
| --- | --- |
| Pin16 / PJ24 / UART4-TX | PA10 / USART1-RX |
| Pin18 / PJ25 / UART4-RX | PA9 / USART1-TX |
| 任意GND，例如Pin20 | GND |

3.3V TTL、交叉TX/RX、共同地线；不能接RS-232电平。步进/舵机使用各自合适的5V电源，
不从A7Z GPIO取电，不把两块板的电源输出直接并接。先架空履带/固定舵机，远离桌沿。

## 安全与生命周期

- 初次和每次重启都不自动解锁、不发运动、不录音。`aipet mcu start`只建立通信和心跳。
- 板上用独立 `posix_spawn` 内建任务，而非挂在短命NSH命令的pthread；命令退出后继续维护心跳。
- 每250ms发送PING；PONG超过750ms未更新、故障事件、MCU重启/配置变化、请求超时或
  解锁5分钟到期，关闭运动权限并尽力发送STOP。重连不自动解锁。
- UART4是单一打开者，应用也只有一个串口任务读写，避免TTS或另一个终端吃掉ACK。
- 每次控制等待ACK有期限；超时不重发运动、不把发送成功或迟到/错SEQ应答算作完成。
- 明确的语音STOP绕过LLM；串口仍忙时先排队优先STOP，但没有ACK时不得宣称停止已确认。
- 手动 `clear-fault blind-ack` 是显式台架风险确认；模型工具不暴露arm、清故障、Flash配置、
  单轮标定或热保护绕过。TOF未实现时绝不能宣称有防跌落保护。
- Agent工具只接受本轮用户原文能够明确授权的同一动作和参数，一轮最多消费一次；
  工具失败也消费额度。不是执行模型自由文本或把任意action标签转成串口命令。
- ACK只表示下位机接收命令，**不等于电机实际完成、达到某个物理距离或障碍检测通过**。
- 软件急停、心跳和CRC不是硬件安全系统；没有力矩、碰撞、可靠认证或急停按钮保证。

## 最短板测顺序

先备份当前SD内配置/凭据/文件，再将独立候选镜像写到SD；不要写到UFS。
新固件也需要冷启动、UART示波器/逻辑分析仪和双板验收，不以主机测试代替。

### 1. 只查询，不运动

```sh
cat /dev/a733-uart4
aipet tts status
aipet mcu start
sleep 1
aipet mcu status
aipet mcu ping
```

应显示v177、115200，随后 `ready=1 online=1 armed=0`；心跳持续增加。
保持STM32上电，退出/重进NSH或SSH后再次status，确认后台任务没有随启动命令退出。
`create/open`失败、旧9600或没有PONG时不得继续运动；检查交叉接线/共同地和固件提交。

### 2. 固定/架空后的显式解锁

```sh
aipet mcu clear-fault
aipet mcu ping
```

如果fault=10（0A，未启用底部TOF），普通clear会被MCU拒绝。只有确认是固定台架测试、
接受无防跌落保护时，才单独执行：

```sh
aipet mcu clear-fault blind-ack
aipet mcu ping
```

确认fault=0且能力/配置匹配后：

```sh
aipet mcu arm
aipet mcu servo -20 500
aipet mcu servo 0 500
aipet mcu move forward 1 256
aipet mcu stop
```

STOP同时关闭上位机运动权限。下次运动须重新arm。未知/禁用能力不得自动更改MCU配置。

### 3. 文字 → 应用控制 → I2S

```sh
aipet audio output i2s
aipet audio gain 5
aipet audio volume 50
aipet route "前进"
aipet mcu arm
aipet ask "向左看"
aipet ask "前进256步"
aipet ask "停止"
```

`route`仅检查，不发串口字节。以上短命令走同一Agent产品路由的直接控制分支，
不必忍受本地LLM长预填充；问答仍沿用原有路由。
额外验证 `aipet ask "不要前进"` / `"前进然后右转"` / `"前进65535步"` 应拒绝且无运动。

### 4. 语音全链（会上传本次PCM）

先用现有 `wifi connect <SSID>` 联网并确认时间，再 `aipet init`（未配置时）和
`aipet start` / `aipet status`。Agent ready后，未配置官方语音凭据时运行 `aipet audio setup`，
密钥隐藏并沿用板上加密保存；无需在日志/文档/命令行中贴密钥。

```sh
aipet audio status
aipet audio asr 5000
aipet audio speak "语音控制测试开始"
aipet mcu arm
aipet audio listen 5000
```

录音时清楚说“向左看”“前进”“前进256步”“停止”。每条listen只处理一轮。
多轮需显式开启有界录音：

```sh
aipet audio loop 5000 10
```

共最多10轮，每轮录5秒，再识别/控制/播报；不是VAD/唤醒词/免打断全双工。
最多20轮、每轮1..10秒；错误或Ctrl+C结束，无开机常驻录音。ASR失败不能发动作。
播放时停止采集，避免扬声器内容被当成下一条输入；尚无回声消除，远场误识别仍需实测。

### 5. 官方Agent工具而非直接短指令

```sh
aipet mcu arm
aipet cloud "前进"
aipet ask "停止"
```

`cloud`强制经过官方Agent的工具调用。工具须按默认256步/speed1与原文一致；
未调用、改参数、重复调用、未解锁或超时都不能发送第二次运动。
最终动作结果以工具适配器的真实反馈覆盖模型自述，避免模型没执行却说“完成了”。
云端模型是否实际调用此工具仍须实测，不同模型/工具模式可能不调用。

### 6. 断链和恢复

在台架上模拟断开串口RX或关闭STM32，确认A7Z 750ms级失联后armed归零，MCU约1秒
无有效帧停止步进（以真实定时为准）。重连后PING恢复但不自动再动。
结束执行 `aipet mcu stop`、`aipet mcu disconnect`；必要时物理切断电机电源。
UFS `ufs status` / 模型大小及原有SPI显示回归另测，不执行UFS清理。

## 源码、验证与恢复

新增 `mcu_protocol.*`、`mcu_link.*`、`mcu_agent_tool.cxx`、`mcu_tool_bridge.*`；修改UART4驱动、音频输出选择、
路由和Agent attach工具注册。芯片适配技能要求将硬件所有权留在BSP，应用只用公开
POSIX串口和官方Agent/语音接口，保留通过镜像，不把编译成功标作双板成功。
官方工具注册头以小型C桥接，不改官方头文件。ARM64检查将两个4097字节回复缓冲
移出调用栈；9个改动单元的最大单函数栈占用704字节，不代替整条调用链的实机栈水位测试。
整机链接发现GOT孤立节落在`_image_file_end`之后，已显式收入可写`.data`，
新增孤立GOT检查；没有跳过原有镜像长度、BSS、栈、MMU页对齐与BL31边界门禁。

```sh
bash tools/test-aipet-mcu.sh
python3 tools/test-a733-uart4-mcu.py
bash tools/test-aipet-audio.sh
bash tools/test-aipet-routes.sh
bash tools/test-aipet-agent.sh
bash tools/build-a733-mcu-v177.sh
```

协议测试直接对照锁定STM32提交的603组真实C编码结果；另有10万噪声字节、CRC、
半帧超时、否定/复合指令、参数范围测试。PTY集成用真实应用采集/降采样/路由/
串口/播报代码，PCM和云端是mock；覆盖播报期间心跳、解锁/断链/NAK/错SEQ/迟到ACK/
故障事件抢先取消（随后收到成功ACK也不能复活请求）/
原文授权/重复工具调用/不谎报完成。不能据此声称已经通过实际麦克风、模型API或STM32。

改动前恢复点 `out/checkpoints/a733-pre-uart4-mcu-20261023/`，包含986份当前源码和Git历史，
引用v175镜像及UFS原始前缀摘要。恢复到新目录，不覆盖当前工作树。
本次镜像和源码摘要、实际构建/SDK恢复记录见 `docs/ARTIFACTS.md` 最新节。
