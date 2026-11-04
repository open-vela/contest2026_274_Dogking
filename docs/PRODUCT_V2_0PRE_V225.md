# v2.0pre／v225：桌宠控制、语音设置与可选开机运行

2026-11-03。基于 v224；**候选预览，不替代 v1.1／v219 稳定版**。本轮不改模型、NPU ABI／驱动、CPU 全局调度或 UFS 分区，不提交 PR、不推送。

## 四项修改

1. 控制在 LLM 之前走确定性语法。补齐日志中“识别前面物体”“前面有个什么物体”“前进十五部”，支持 Vela／桌宠、请／帮我等前缀和常见口语。识图调用真实摄像头→YOLO→SPI，再按本轮路由播报；不会让 LLM 猜物体。否定、引用、复合运动和不明确数值不执行；不是任意模糊关键词触发。
2. `aipet run` 开启当前常驻服务的桌宠命令会话；链路健康、fault=0 时，收到明确的人类动作命令可直接执行，**无需每轮 arm**。说“前进三步”发送 30 个脉冲；“前进十五部”按同音单位发送 150 个脉冲。语音明确数字范围 1～1024，发送值 10～10240，禁止连续运动哨兵。默认速度档 2，按固定 STM32 固件是 300Hz（原档 1 为 200Hz）。×10 增加距离，并非把脉冲频率提高十倍。未给数字的“前进”仍为 256 脉冲，不再额外×10。原始控制台 `mcu move ...` 不做倍乘。
3. 新增默认关闭的 `pet.auto` 持久配置。用户显式启用后，下次开机启动独立任务，沿用 Wi-Fi、SSH、云配置、音量、增益和模式。模型准备完成后，播报“我是桌宠vela，有什么可以帮助您的吗”，完成后才 MIC ON。保存的静音优先，不强行出声。欢迎语失败不伪报监听就绪。
4. 音量绝对值、±10 调节、静音／恢复、音量查询、麦克风增益走直接设置路由，保存到原配置，下一轮采集使用新增益。不需调用 LLM，不因静音停止监听。静音状态下设置反馈可能只有日志。

保留串口 CRC、心跳、故障、ACK 超时和协作停止；它们用于防止未知执行结果被重发，并非汽车驾驶策略。NAK／失联／故障会锁住当前会话，健康心跳本身不重新授权；修正故障后显式 `aipet mcu desktop on` 或停下服务再重新 run。**不会自动清除故障、写 STM32 Flash、开启缺失传感器或重发运动。ACK 仅为接受，不能当作位移完成。**

## 接线（不变）

| A7Z 物理脚／信号 | 模块端 |
| --- | --- |
| 12／PB5 BCLK | 功放 BCLK，麦克风 SCK |
| 35／PB6 LRCLK | 功放 LRCLK，麦克风 WS |
| 40／PB7 DOUT | 功放 DIN |
| 38／PB8 DIN | 麦克风 SD |
| 16／PJ24 UART4 TX | STM32 PA10 RX |
| 18／PJ25 UART4 RX | STM32 PA9 TX |
| GND | 各模块共地 |

UART4=115200／8N1／3.3V TTL，旧 UART TTS 模块断开。电源按合并模块厂家要求接，不把裸 ICS43434 当 5V 器件。扬声器 OUTP／OUTN 是差分，两端均不能接 GND。首次动作测试留出空间，最好抬离桌面边缘。

## 本版先按顺序测试

不要把 Markdown 反引号或 `nsh>` 一起输入。

```text
aipet help
ufs start
ufs status
aipet audio output i2s
aipet audio volume 50
aipet audio gain 5
aipet mode local
aipet mcu start
aipet mcu ping
aipet mcu config-get
aipet mcu stepper-stat
```

确认帮助含 v2.0pre／v225；通信有 RX，故障为 0。先用文本排除 ASR 误识：

```text
aipet ask "识别前面物体"
aipet ask "前面有个什么物体"
aipet ask "音量调到五十"
aipet ask "大声一点"
aipet ask "当前音量"
```

这几条应显示 vision／setting 路由，不进入普通 LLM 聊天。文本运动测试需一次性开启当前桌宠会话：

```text
aipet mcu desktop on
aipet ask "前进三步"
aipet mcu stepper-stat
aipet ask "停止"
aipet ask "前进两步"
aipet ask "停止"
aipet mcu desktop off
```

前两次运动日志应分别是 pulses=30／20、speed=2；不以播报代表电机实测。语音常驻不用再单独 desktop on：

```text
aipet run
aipet audio service status
```

冷启动等模型准备。每次看到 **READY MIC ON** 后说一句，等待完整播报及再次 MIC ON：

- “你好”
- “识别前面物体”
- “前面有个什么物体”
- “帮我看看这是什么”
- “前进三步”
- “停止”
- “前进两步”
- “把音量调到百分之五十”
- “大声一点”
- “小声一点”
- “静音”，等 MIC ON 后再说“取消静音”
- “当前音量”
- “麦克风增益五”

当前仍半双工，不提供回声消除或扬声器说话时打断。识图会先释放 ASR NPU session、取得新帧并显示同帧框，再恢复监听；连续摄像头任务不能同时占用语音的 NPU。

```text
aipet stop
aipet audio service status
aipet mcu status
```

等 active=0 后再 run 测恢复。不要强杀持有 NPU 的线程。看见 ACK 超时不能用重复运动命令验证“是否真的走了”。

## 首次配置与第二次开机自动运行

先停止服务；首次需要人工输入网络和服务凭证，不能在后台输入密码。

```text
aipet stop
wifi connect 你的SSID
ifconfig
wifi time
sshd --setup openvela
sshd &
sshd --status
aipet init
aipet start
aipet status
aipet audio setup
aipet audio status
aipet status
```

`wifi connect`、SSH setup、Agent init、audio setup 按交互提示输入。`aipet start` 后先等 status 显示官方引擎就绪，再输入语音配置。LLM 与语音服务商凭证仍分别按真实接口配置；没有云语音凭证不能拿 LLM 密钥冒充。已有配置无需重输，先检查状态。云账号调用未在本次主机回归中实测。

旧 `audio setup` 对应火山。其他已有账号可用 `aipet audio setup xfyun` 或 `aipet audio setup tencent`；再以 `aipet audio providers asr 名称`、`aipet audio providers tts 名称` 保存选择。完整账号步骤和 Edge 网关限制见 [云服务商说明](CLOUD_VOICE_PROVIDERS_V218.md)。不要全部配置或把本地播报当成云 API 验收。

选路模式先保存：

```text
aipet mode auto
aipet audio output i2s
aipet audio volume 50
service wifi on
service ssh on
service agent on
service ufs on
aipet autostart on
service status
aipet autostart status
```

`auto` 在网络和云配置均就绪时走完整云链路；启动等待最多 60 秒，auto 不就绪可转本地。`cloud` 强制联网，不就绪会报错而不会冒充云调用；`local` 固定本地。本地路由自动准备本地 ASR、LLM、声学模型和 vocoder；云路由的模型在云端，无需下载到板上，之后切本地会自动准备，不把云就绪谎称为全部离线模型已常驻。离线模型集必须完整，可放在已挂载 UFS，原 /data 回退规则保留。

只在上述准备通过后关机再启动。预期：独立启动日志→存储／模型准备→`product-boot-v225: models READY`→欢迎语→MIC ON。首次冷加载仍有耗时，不能承诺上电即响应；状态不能代替真实可听声音。若保存静音，则明确输出 `greeting NOT played`，不擅自取消静音。

关闭下次开机监听，不改变当前任务：

```text
aipet autostart off
```

关闭当前监听需 `aipet stop`。`service pet on|off|status` 是同一启动策略的底层别名。Wi-Fi／SSH／UFS／agent 自启动互相独立；后台任务从 `/dev/null` 读取，日志走已验证的 `/dev/a7zconsole`，不会抢串口输入或继承临时 SSH 会话。

重刷整卡会替换 SD 上配置；先备份自己的配置及数据（包含秘密，不提交到 Git）。仅重启则沿用持久记录。FAT 的权限不是加密，必须保护卡和凭证。

## 验证、溯源与边界

构建入口 `tools/build-a733-product-v20pre-v225.sh`，独立 v225 缓存，不覆盖 v224。基础19项门禁后执行 `tools/test-product-gates-v225.py`，新增真实路由语法、PTY 串口命令／失败抑制、CRC 开机策略、实际服务加载／欢迎语／静音保存测试。外部 ASR／LLM／PCM 的测试替身明确标注，不冒充硬件声音或机械验收。

完整审计保留六路 YOLO 输出与冷热验证、NPU ABI／ISA／内存边界、摄像头所有权、SDK 原样还原和镜像文件回读。LLM 数学源保持与 v220 真 GGUF 对照相同；应用调度的新增行为单独回归，不再声称整个 product_runtime 只是换 banner。

## 已封装制品与恢复

已生成独立 2 GiB 镜像：`../openvela-a733-cubie-a7z-sd-v2.0pre-v225-candidate.img`（相对仓库根目录），2,147,483,648 字节，同目录有 `.sha256`。整卡烧写前备份 SD 上个人配置和数据；本次开发没有写物理 SD／UFS。

- 镜像 SHA256：`cd4cd64097720b7da15abef1ee7f1e8291c258888a8683590a8c537998ddc51c`。
- 内核：7,254,952 字节，SHA256 `c95c1b47a98feaafd45bcc40eca20645d2d2fc8e1bd3dd964e814145235d86aa`。
- 固件源码：`99f56842a67b68cf66b19233c4fc3a00c5eb7dec`；本地交付标签：`a733-v2.0pre-v225-image-20261103`。
- 源码恢复包：`out/checkpoints/a733-v20pre-v225-image-20261103.bundle`；完整固件封存：`out/v225-product-v20pre-release-r1`。
- 最终构建：`out/product-v20pre-v225-build-r2`；36 项软件门禁：`out/v225-product-gates-r2`；完整链接审计：`out/v225-full-link-r3`；镜像回读：`out/product-v20pre-v225-package-r1`。

完整编译、36 项门禁、最终 ELF／ABI／ISA／摄像头检查、模型及启动文件回读、Windows/Linux 独立哈希均通过，SDK 316 项字节还原。日志原话“前面有个什么物体”先以新增用例复现漏路由，再修正并重编译，最终二进制已检查包含该语句。Git 恢复点包含源码、工具、文档及脱敏证据，不含模型镜像和个人凭证。

[封存证据与限制](evidence/product-v20pre-v225-image/README.md)。这是软件验证后的**待板测预览版**，尚未新增真实云账号、动作距离、可听声音或冷启动自动监听验收；主机替身不代替板测。最新稳定仍 v1.1／v219，旧镜像／失败日志保留；没有 PR 或推送。
