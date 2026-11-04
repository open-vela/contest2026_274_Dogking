# 真实开发板测试证据

## 2026-10-30：v214 目前只有软件与镜像证据

[发布记录与板测项目](a733/VOICE_RESIDENT_V214.md)，证据目录 `docs/evidence/voice-v214`。
17 项语音回归、17 个实际 ARM 单元、22 项摄像头主机回归、完整固件及镜像校验通过。
持续空闲/多轮/前滚/端点/英文 OOV/停止和 stdio 的主机 mock 不是实机录放或长稳。
没有把主机 NPU stub 的零 PCM 保存/发布为声音，原中文 mel golden 继续严格比较。
真实麦克风、扬声器、SSH 断开后的常驻行为和端到端 3–5 秒须按新镜像另行板测。

## 2026-10-25：v178 仅主机/编译证据，未冒充板测

详见 [v178说明](a733/LLM_V178_MINICPM5_AND_VOICE.md)。真实MiniCPM5文件SHA核验、
分词/NoThink模板与参考一致，12个位置全部130560 logits的maxabs/RMSE为0；
主机缓存复用、API清理、流式线程隔离/失败路径、I2S应用回归和ARM64单元交叉编译通过。
参考关闭融合/重打包/AVX/FMA以固定逐算子oracle，探索失败日志保留。A7Z延迟、质量和
本版完整启动尚待用户实测。三个新ASR NBG只通过厂商导出/头CID/原生I/O静态检查，
没有执行板上NPU，不是通用ASR或完整离线语音验收。

## 2026-10-23：v177 UART4/STM32链路仅主机验证，双板待测

[接线与板测步骤](a733/AGENT_UART4_STM32_V177.md)。下位机源码提交锁定e83a9cac，
603帧与原C编码逐字节一致；真实产品采集/降采样/路由/串口/播报代码在PTY下通过，
PCM、云ASR/TTS及STM32硬件均为模拟。覆盖播报期间持续心跳、手动解锁、断链、
NAK/错SEQ/迟到ACK、故障抢先取消、禁止未授权/重复工具动作以及不谎报完成。
9个改动单元以实际ARM64参数编译通过，最大单函数栈704B；不能代替实机调用链水位。
UART驱动真实函数体离线验证独占、115200/8N1和非阻塞有界收发；GOT最小链接样本
确认孤立节被拒绝、显式归入数据段后通过。原音频/路由/Agent/UFS回归保持通过。
所有这些都**不是本版本双板、声学或实际云API验收**；候选镜像摘要见ARTIFACTS最新节。

另补存用户v175开机自动挂载日志：[原始文本](evidence/UFS_V175_AUTOMOUNT_USER_20261023.txt)。
这是一次正常重启的现有FAT自动挂载通过，不是断电压力测试；UFS启动链清理仍暂停。

## v172受控UFS写入/flush/还原已通过；v173文件系统待板测

完整原件和dd定位：[v172实机结果](evidence/UFS_V172_RUNTIME_20261022.md)。
v173主机ASan/UBSan验证数据通道、真实NSH大偏移与官方FAT32规划/格式化；
这些不是实际UFS文件系统或重启持久性证明。[本轮测试边界](a733/UFS_DATA_V173.md)。

## v171 UFS标准只读实机通过；v172写入验收待进行

[v171原始日志与指标](evidence/UFS_V171_RUNTIME_20261022.md)已补齐。
[v172固定窗口试验](a733/UFS_SCRATCH_V172.md)需要显式确认，不能由只读通过推导
写入、flush或断电一致性通过。旧证据和所有恢复点保留。

## v151 应用链路：主机通过，新的整链路实机待验收

新增 typed PCM ABI、应用持久配置/录音/播放/路由、FIR降采样与官方ASR/TTS适配。
ASan/UBSan回归覆盖真实采集/发送、应用错误/锁/路由、TTS完整性、密钥长token和
隐藏向导。1kHz测试输出RMS=7069.7，12kHz折叠抑制输出RMS=7.8；仅主机滤波证明。
官方ASR强制CA/最终响应并补接收NUL边界；不将源码安全检查称云端鉴权已通过。
见 [v151入口/验收流程](a733/AIPET_AUDIO_V151.md)。v150用户实机证据仍为恢复基线。
全量构建及最终复编、镜像/GPT/ext4/内嵌内核SHA、138路径SDK恢复已核对通过，
[v151交付记录](evidence/i2s-v151/build-summary.md)保留原完整构建与最终日志。

## v150 后续用户实机：48kHz mono WAV 五次回放通过

2026-10-16。两次默认 ×5 录音、五次50%音量播放，status=0、240000帧/5000ms，
用户确认拍手和说话清楚。第一次录音16个样本削顶、第二次0；本轮未独立试听新文件。
16kHz合成文件打开报-2原文保留，尚无该采样率的物理回放证明；不扩大为长稳/标准音频
或Agent云TTS完成。[完整证据、哈希和音量入口](evidence/I2S_SPEAKER_V150_RUNTIME_ACCEPTANCE.md)。
以下候选与失败均为历史记录。

## v150扬声器候选（2026-10-16，未实机发声验收）

有限PCM16 WAV播放主机回归、2955目标构建、启动布局、GPT/ext4/内嵌内核哈希通过，
130个SDK暂存路径与home身份恢复一致。新增合成测试音供上板独立验证。
底层tone发声为既有证据，不等于新文件播放/音质已通过。
见 [v150交付](evidence/i2s-v150/build-summary.md)。

## v148候选交付（2026-10-16，非新硬件验收）

icsvoice左槽mono/可调增益源码经主机ASan/UBSan与全量2955目标构建通过；
布局、GPT/ext4、内嵌内核SHA和129路径SDK恢复核对通过，已生成2GiB新镜像。
v147基础录音继续作为实机基线，v148新语音/冷启动/共存尚未实测。
哈希、源码版本和限制见 [v148交付](evidence/i2s-v148/build-summary.md)。

## v147 后续用户实机：首次基础录音通过（2026-10-15）

用户提供v147串口、5秒原始WAV及24MHz逻辑数据，并确认有声音但音量小。
WAV的非零/零数、最小/最大、平方和及符号变化与串口完全一致；左槽有效、右槽全零。
六个过程点计数均通过，单次录音提交成功。波形32 BCLK/半帧符合配置，但没有取得
短波形与WAV整段PCM精确匹配；不得扩展为长稳、标定音质或ASR完成。
原始语音/逻辑留本地不入Git，数值、哈希和复现方法见
[v147基础录音报告](evidence/ICS43434_V147_RUNTIME_ACCEPTANCE.md)。
以下是此前候选交付时的历史状态，全部保留。

## v146用户上板失败与v147计数候选（2026-10-15）

用户提供NSH启动和ICS日志：5.1067秒，480000输出样本、489600 FIFO字、RXCNT244801、
INTSTA=0x11。v146在文件操作前计数检查失败，-EIO/committed=0，无WAV。
这证明完成一次启动与诊断操作，不证明有效语音或冷启动长稳。PCM min/max不再是旧满幅
串扰模式，但无PCM文件可试听，不能判断语音通过。
用户`.sr`原文件SHA与归档副本一致，metadata=20kHz、302592样本、15.1296秒；
D0=LRCLK/D1=BCLK/D2=SD，不足以解码I2S。保留原文件，不伪造PCM或频率。
v147按帧候选核对HW与FIFO字，实际采集C函数的用户数值fixture、六个过程点、错误倍率、
计数停滞/大数、13种注入失败和文件保护通过ASan/UBSan；仍需新镜像实测。
见 [v147说明](a733/ICS43434_V147_COUNTER.md) 与 `docs/evidence/i2s-v147/`。
v147全量2955目标构建08:18，布局/GPT/ext4/内嵌内核SHA校验通过，129个暂存路径恢复一致。
这些不是新版本实机通过；[交付与哈希](evidence/i2s-v147/build-summary.md)记录明确待测边界。

## v146 ICS43434：源码/主机验证（2026-10-15）

更换麦克风后新增独立双槽接收候选，尚无新 ICS43434 实机录音证据。
实际 C 采集函数/命令入口在 MMIO/文件 stub 下通过 ASan/UBSan：双槽 byte map、
左右交错与单路选择、48kHz WAV头/文件长、5秒样本与5100ms模拟节拍、原始S24、
旧模式恢复、严格参数、短写、11种失败保留旧文件、内存释放及报告格式。
原 INMP441 八组矩阵回归继续通过；它们仅证明软件逻辑，不证明麦克风电气状态。
新增只读分析/左右分轨工具用合成信号验证恒值通道 AC RMS=0、时长拒绝、输入不变、
禁止覆盖旧输出；不能把合成测试标为实际语音通过。
全量构建2955/2955目标（08:12）、ELF布局检查、2GiB候选镜像的GPT/ext4/内嵌内核
校验通过，129个官方暂存路径与home目录身份恢复一致。
资料与上板步骤见 [ICS43434 v146](a733/ICS43434_V146.md)，
哈希/原始日志见 [本轮构建归档](evidence/i2s-v146/build-summary.md)。

开发板：瑞莎 Cubie A7Z，全志 A733，4 GiB 内存，microSD 启动，无 UFS。

## 2026-10-15 蓝牙证据边界

v144 已有五轮 enable/真实扫描/stop/disable/OFF/quit重进实测，见
[原始日志与判定](evidence/BLE_V144_FIVE_CYCLE_HARDWARE_PASS.md)。
v145新增 `home` 官方GATT客户端，交付后通过SSH实测了单板运行、编解码入口、非法参数、
适配器OFF拒绝/清理、SMP与计时、BLE五轮启停和扫描中退出重进。
扫描期间电脑→板端47/47 ping返回，属于短时轻负载共存，不是持续吞吐/长稳。
**真实Read/Write/Notify/业务ACK与ESP32-S3双板仍未测**；默认SSH会话损坏仍待查。
不能将mock ACK或ELF符号当作无线通信实测；见 [v145实机报告](evidence/BLE_V145_SSH_RUNTIME_ACCEPTANCE.md)。

## 启动与操作系统

观察到的成功启动边界：

```text
- Boot from EL1
- Boot to C runtime for OS Initialize
NuttShell (NSH)
nsh> uname -a
NuttX 0.0.0 ... arm64 cubie-a7z
```

`free` 报告约 4.27 GB 的托管内存；procfs 已挂载；`ps` 显示空闲线程、
高优先级工作队列和 NSH 任务。

## SD/GPT/FAT

修复 SDMMC 多块传输后，以下读取均无 I/O 错误：

```text
dd if=/dev/mmcsd0 of=/dev/null bs=512 count=1
dd if=/dev/mmcsd0 of=/dev/null bs=16384 count=64
dd if=/dev/mmcsd0 of=/dev/null bs=65536 count=16
```

开发板日志注册了 GPT 条目，并将 `/dev/a7z-data` 以读写方式挂载到 `/data`。

## 温度与开发板设备

```text
sensortest -n 3 temp0
temp0: ... value:34.47
temp0: ... value:34.47
temp0: ... value:34.47
```

已验证的设备节点包括 `/dev/userled`、`/dev/watchdog0`、
`/dev/uorb/sensor_temp0`、`/dev/i2c2`、`/dev/i2c7`、`/dev/spi1`、
`/dev/pwm0`、`/dev/mmcsd0`、`/dev/a733-wifi` 和 `/dev/npu0`。

## Wi-Fi 与 IPv4

FCU760K 已完成 VID:PID 从 `a69c:8d80` 到 `a69c:8d81` 的固件切换。扫描到
2.4 GHz BSS 和 5745 MHz 5 GHz BSS。开发板通过 WPA2 关联，并在测试网络中
由 DHCP 获取 `192.168.31.27/24`。网络名称和密码不在本仓库中。

网关 ping、互联网 IPv4 ping、DNS 查询、TCP、UDP、wget/curl、SSH 和 FTP
功能测试均通过。一次有代表性的本地 iperf 测试结果：

- TCP：30.25 秒传输 14.9 MiB，4.13 Mbit/s；
- UDP：30.15 秒传输 38.9 MiB，10.8 Mbit/s，丢包 5.7%。

FTP 测试覆盖小文件、大文件，以及本地路径中带空格和中文字符的 PDF。SSH
测试从 Windows 发起并连接到远端 NSH，覆盖主机密钥、ARP 和 socket 生命周期
修复后的行为。

## VIP2 模型执行

### LeNet

Linux VIPLite 参考结果：

```text
hardware CID=1000003b
profile inference=164..178 us
output[0] raw-crc32=d50f5d79
```

openvela `/dev/npu0` 产生相同 CRC 和 20 字节输出：

```text
003c000000000000000000000000000000000000
```

### YOLOv5s

使用确定性全零输入的官方 A733 模型，在 Linux 与 openvela 上均得到三个相同
的输出 CRC：

```text
53127e9c  ddb5675e  6e01633e
```

### YOLOv8n PCQ

Linux VIPLite 参考结果报告六路输出推理耗时 12,604 us。六个黄金 CRC32 为：

```text
55d0becd ead06611 ba209c57 2637657c 9e7f743a 2eb4d5ce
```

openvela 与内置 dog 输入的六路结果全部一致。动态提供一份 dog RGB 输入时
再次一致。使用黑色 640×640 RGB 输入时，输出 CRC 改变且解码得到零个检测框，
同时 MMU/IRQ/guard 检查保持干净。

## NPU ABI v4 构建检查点

2026-09-11 在当前 WSL openvela 环境中完成全量 AArch64 CMake 构建。驱动中的
`A733_NPUIOC_RUN_A7PM` 与原生 `npu` 应用均完成编译和最终链接，`System.map`
包含 `npu_main`。本次待刷写镜像为：

```text
cmake_out/cubie-a7z_nsh_npu/nuttx.bin
size:   1586024 bytes
sha256: bfc3d482d579f6c633af81ab5e002bdb9bcaa324b259719c155223ca396893be
```

首次配置时显式使用 `/usr/bin/python3`；WSL 的全局 `python` 仍保持原状。板端
回归命令和期望值见本次交付说明，未刷写前不把此 ABI 标记为硬件验证通过。

## 当前 UVC 失败边界

最新诊断可以读取有效的 xHCI 能力寄存器块，但在破坏厂商 Combo PHY 交接后，
xHCI HCRST 无法清零；随后 DWC3/app 寄存器读取为零。这里记录的是正在处理的
驱动问题，不应宣称摄像头已经成功。

后续 v113 实机仍未看到稳定端口连接；v114 加入 Combo PHY 表和最小 xHCI ring，
已随 v115 完整构建，但尚未上板。USB 麦克风和摄像头没有 VID/PID/描述符证据。

## 八核 SMP 与本地 LLM

板端 `cpucheck` 逐核确认 CPU0..5 为 Cortex-A55、CPU6..7 为 Cortex-A76，亲和
掩码覆盖 8 核。Wi-Fi、ping、NPU 和 NSH 在 SMP 镜像上继续工作。

Qwen2.5-1.5B Q4_K_M 模型完成 tokenizer、28 层前向、KV cache 和连续生成检查。
模型常驻后，默认 CPU2..7 六核工作池的单 token 前向检查点约 0.615 秒；板端
中文流式回答和有限多轮记忆已经观察到。该数值是特定检查点，不等同最终 tok/s，
也没有替代外部参考精度和长时间热稳定测试。

## 官方 Agent 与桌宠文字链路

实机执行 `aipet init` 后，DeepSeek 配置使用板级密钥加密保存，Agent 自启动。
连续两次中文请求成功，首次约 7490 ms，复用连接后的故事请求约 1829 ms。此前
第二次请求的非法 Unicode JSON 问题已修复，后续连续对话通过。

桌宠快速规则、云端 Agent、回复/表情/动作解析已进入源码。UART4 TW-TTS 和
I2S0 诊断进入 v115 候选镜像，但因没有开发板尚无实际发声/时钟日志，不能列为
硬件通过。

## UART4 TTS v115 失败与 v116 修复

v115 实机确认 `/dev/a733-audio` 可读取、官方 Agent 云端对话成功，但
`aipet tts test` 和 Agent 自动播报均返回 `-22`，且发送帧计数保持为零。
这证明错误发生在 UART 帧发送之前。源码复核确认 NuttX termios 的 `B9600`
是 `c_cflag` 中的编码选择值，而 `c_speed` 保存数值波特率；原驱动混用了
两者，使 `TCSETS` 稳定返回 `EINVAL`。

v116 已修正该 ABI 使用方式，并增加 `node=present|missing` 与
`stage=validate|open|termios|parameters|text|complete` 诊断。主机 TW-TTS 协议
测试、桌宠完整路由测试和 AArch64 全目标构建均通过。候选镜像校验如下：

```text
image:  openvela-a733-cubie-a7z-sd-uart-termios-v116-candidate.img
bytes:  2147483648
sha256: e03cb1bc3e9a9795eb2dd45530cba00c1357b4b1980f0097c567fd108bad35f9
kernel bytes:  1920592
kernel sha256: d55f12403d7c55a09b9bc395e2514e8dade0a8c2803e1c089322d435e50dd65e
```

`e2fsck -fn`、`sgdisk -v` 以及从镜像重新提取内核后的 SHA-256 比较均通过。
v116 实机得到 `node=present`，证明设备注册和 termios 修复有效；发送测试进入
`stage=parameters` 后返回 `-110`，`frames=0`，说明首个参数字节等待 UART
FIFO 可写超时。该结果取代“等待 v116 板测”的旧状态。

## UART4 TTS v117 时钟/复位修复候选

v117 保持官方 BSP 的 UART4 基址 `0x02504000`、BGR `CCU+0x0e10` 和
PJ24/PJ25 function 4，显式把 APB-UART `CCU+0x0538` 设为 24 MHz/1，随后
依次拉低复位、打开 UART4 门控、释放复位并执行 MMIO 读回/延时。新增只读
`/dev/a733-uart4`，可报告 APB 时钟、BGR、引脚复用、LCR、LSR 和 USR。

```text
image:  openvela-a733-cubie-a7z-sd-uart-hw-v117-candidate.img
bytes:  2147483648
sha256: 17658c8327cb6e015640212ba5bfdb3222779f8089a332a09bd82dff06fd54fe
kernel bytes:  1920592
kernel sha256: 418fe8b6a84479289f29029097414c45cb3b629db73c957cd51f9c27e7b1886d
```

AArch64 全目标编译/链接、`e2fsck -fn`、`sgdisk -v` 和镜像内核回读哈希均
通过。仍须实机取得 `thre=1` 或 `tfnf=1`、`stage=complete`、帧计数递增和
实际发声，才可标记 UART TTS 硬件通过。

## UART4 TTS v117 实机结果与 v118 MMIO 修复

v117 的 `/dev/a733-uart4` 同时读到 `apb-uart=0`、`bgr=0`、PJ24/PJ25
function 0、`lcr/lsr/usr=0`，TTS 仍为 `stage=parameters/-110`。三个独立硬件
域同时为零证明问题不是模块接线或协议，而是 MMIO 地址计算错误。

官方 `sun60iw2p1.dtsi` 指定主 CCU 为 `0x02002000`；官方 sun60iw2 pinctrl
HW type 4 使用首 bank `+0x80`、bank 步长 `0x80`、drive `+0x20`、pull
`+0x30`。v118 已逐项同步这些定义，UART4 基址仍为正确的 `0x02504000`。

```text
image:  openvela-a733-cubie-a7z-sd-uart-mmio-v118-candidate.img
bytes:  2147483648
sha256: 8330d6eeccc2f7736ccab2a567fdd109c87188656d4dd8809236877188dde685
kernel bytes:  1920592
kernel sha256: aa0ed5b717f136b3fadde2dbdb2abd26928acaec11a83261e944b558dd731356
```

AArch64 完整链接、ext4、GPT 和镜像内核回读哈希均通过。v118 实机结果：

```text
bgr=00010001 gate=1 reset=1
PJ24=TX/function4 PJ25=RX/function4 cfg3=0000ff44
lcr=00000003 lsr=00000060 thre=1 usr=00000006 tfnf=1
uart-tts test: frame sent (0)
initialized=1 frames=4 failures=0 last=0 stage=complete
```

`frames=4` 对应首次调用发送的音量、语速、语调三帧和 UTF-8 文本一帧；紧接着
再次执行相同播报命令也返回 `frame sent (0)`。UART4 时钟、复位、pinmux、
FIFO、termios 和 TW-TTS 帧发送已完成实机验收。

## v67 WEXT/WAPI 构建与镜像校验

2026-09-14 完成全量构建和两次增量构建，以下目标均编译并链接成功：

```text
vendor/allwinnertech/chips/a733/a733_wifi_usb.c
apps/wireless/wapi
apps/system/a733wifi
```

构建配置包含 `CONFIG_NETDEV_WIRELESS_IOCTL=y`、`CONFIG_WIRELESS_WAPI=y` 和
`CONFIG_WIRELESS_WAPI_CMDTOOL=y`。最终内核：

```text
size:   1615080 bytes
sha256: 3f602f2cb25d3a0bf188e93dd93dbae9cdc33e1e50ca3f66d7c9809d0504434f
```

候选镜像：

```text
openvela-a733-cubie-a7z-sd-openvela-wapi-v67-candidate.img
size:   2147483648 bytes
sha256: 51934ef1f1bfc309970d507b8f970d5d4f77628a4fb35426a603c7df867313fe
```

独立校验从镜像重新提取 `/boot/openvela/a733/Image`，其大小和 SHA-256 与构建
产物完全相同；`e2fsck -fn` 和 `sgdisk -v` 均通过。构建脚本的 trap 也已恢复
临时复制进官方环境的源码。WAPI 的真实扫描、连接和断开仍须刷写后验证。

校验过程中发现并修复一个打包器问题：向已有镜像回写完整 ext4 分区时不能使用
`dd conv=sparse`，否则新分区中的零块不会覆盖基底镜像的旧字节。两个相关打包
脚本现已使用完整 `conv=notrunc` 回写，并在写入前逐字节比较嵌入内核。

## v122 ST7735/LVGL 与 I2S0 构建证据（等待实机）

2026-09-25 完成 AArch64 全量构建与最终链接。`a733_header_peripherals.c`、
`a7z_st7735.c`、`a733_i2s0_audio.c`、官方 LVGL 和 `a733display` 均进入最终
ELF。构建脚本同时核对 ST7735/LVGL 与 `CONFIG_A733_I2S0_AUDIO=y`。

```text
build: quickly-openvela/cmake_out/cubie-a7z_nsh_v121_display_i2s
nuttx.bin bytes: 2187160
nuttx.bin SHA-256: bedf575b3905ae71c3ffb3ef53ad93becb3f3c9823d52677de4e9a8746508fbd
image: openvela-a733-cubie-a7z-sd-st7735-i2s-v122-candidate.img
image bytes: 2147483648
image SHA-256: 83e4f1fb3cf002e35366032e8ef5c53c76904e089f5d1b8db82a69cea282a3b2
```

独立校验确认 ext4 无错误、镜像内核大小与 SHA-256 完全一致、GPT 无错误；
分区 4 末尾非 2048 扇区对齐是基础镜像继承提示。此节仅是构建/封装证据，
不代表屏幕连续刷新、MAX98357A 发声或 INMP441 采样已在硬件上通过。

## v122 实机失败证据与 v123 修复构建

2026-09-26 的 v122 实机结果：

- `display raw` 初始化 `/dev/lcd0` 后不返回；最终是用户按 `Ctrl+C` 中止；
- `display test 30` 卡死，`Ctrl+C` 无效；屏幕没有显示内容；
- I2S0 `init/tone/capture` polling 均完成且无 FIFO timeout，生成 288044 字节
  WAV，但 MAX98357A 无声音，麦克风样本内容当时尚未判定；
- `/dev/a733-audio` 的命令状态被尾部零长度写覆盖为 `-22`。

v123 修正 SPI1 片选电平，加入整次显示操作的错误锁存和快速返回；同时修正
I2S 正弦相位索引越界、零长度写状态覆盖，并增加 TX/RX 与采集幅度统计。完整
AArch64 构建及镜像校验通过：

```text
build: quickly-openvela/cmake_out/cubie-a7z_nsh_v123_display_i2s_fix
nuttx.bin bytes: 2187176
nuttx.bin SHA-256: 2d4a3ee5cf088d5dae116d1d7d263e22d35a3b965a9f00de91890f7eca0a7b1a
image: openvela-a733-cubie-a7z-sd-st7735-i2s-v123-fix-candidate.img
image bytes: 2147483648
image SHA-256: 8725bf5063edc8cb97c003ce4a11e598ae1a29d52b57bb66b33f63da0b9721c0
embedded kernel SHA-256: 2d4a3ee5cf088d5dae116d1d7d263e22d35a3b965a9f00de91890f7eca0a7b1a
ext4: clean
GPT: no problems found
```

这里仍只声明“构建和镜像完整性通过”。ST7735 显示、MAX98357A 发声和
INMP441 有效录音必须刷写 v123 后重新取得实机证据。

## v123 实机失败证据与 v124 修复构建

v123 复测取得了可定位证据，而不是通过证据：

- ST7735 初始化成功，但首行报 `isr=00000473 fsr=00430000`、`-EIO`，屏幕黑；
- MAX98357A TX counter 前进至 240031，但没有声音；
- INMP441 RX counter 前进，WAV 已创建，但全部样本为零；
- 成功操作后的独立换行写仍会把状态覆盖为 `last=-22`。

`0x400` 是 SPI TX overflow。v124 对 SPI FIFO 改用 8 位 MMIO；I2S 依据厂商
驱动把 slot 0/1 从 MAP0 移至 MAP1，并补齐空换行过滤。构建和镜像完整性通过：

```text
build: quickly-openvela/cmake_out/cubie-a7z_nsh_v124_spi_fifo_i2s_map
nuttx.bin bytes: 2191296
nuttx.bin SHA-256: c464c3c43fbbbaad5aa6aa98d234bae217c577e39fab092b3470618ef7276758
image: openvela-a733-cubie-a7z-sd-st7735-i2s-v124-fifo-map-fix-candidate.img
image bytes: 2147483648
image SHA-256: 275fe9a413af02d275e4738e6d2bd0b1a1807b11c27853cded975f19408d6522
embedded kernel SHA-256: c464c3c43fbbbaad5aa6aa98d234bae217c577e39fab092b3470618ef7276758
ext4: clean
GPT: no problems found
```

显示色带、LVGL、扬声器发声和麦克风非零采样仍等待 v124 实机验证。

## v124 实机失败证据与 v125 PIO/LVGL 修复

v124 的 `display raw` 两次均返回成功，但实体屏幕完全无反应；这说明 SPI 控制器
内部完成不能作为引脚波形证据。`display test 30` 首帧异常：

```text
ESR_EL1=0000000096000044
ELR_EL1=000000004020a164
FAR_EL1=0000a00000000110
```

ELF 反汇编将 ELR/LR 定位到 `mm_forcefree`，是堆元数据损坏。与此同时 I2S 外设
也没有实际输出/输入。厂商 `pinctrl-sun60iw2.c` 明确 A733 主 PIO 为 HW type 4，
而三条相关路径仍使用旧式 `0x30` bank 步长，导致控制器寄存器正常但外部引脚未
被正确配置。v125 修正这一共享根因，并把 LVGL 改成静态 10 行 RGB565 draw
buffer + LCD `putrun` flush，绕开崩溃的通用初始化路径。

```text
build: quickly-openvela/cmake_out/cubie-a7z_nsh_v125_pio_lvgl_i2s
nuttx.bin bytes: 2187200
nuttx.bin SHA-256: ac91539019442f679ea40eb849cbf6c0f15684891d9b01d1f34e2c3f303ab7b4
image: openvela-a733-cubie-a7z-sd-st7735-i2s-v125-pio-lvgl-fix-candidate.img
image bytes: 2147483648
image SHA-256: 8f7b13f77521cc09b65e6cca9a52640fe5fc5b14845996071ddf64c8b4cfc8e1
embedded kernel SHA-256: ac91539019442f679ea40eb849cbf6c0f15684891d9b01d1f34e2c3f303ab7b4
ext4: clean
GPT: no problems found
```

以上只证明源码、链接、封装和文件系统完整性；屏幕显示、LVGL 连续刷新、扬声器
发声和麦克风非零采样仍需 v125 真机验收。

## v125 实体照片证据与 v126 专项显示修复

用户提供的 ST7735 实体照片中可以辨认桌宠的眼睛和嘴，但整屏被等间距细竖线覆盖，
颜色偏青且存在上一项目画面的阴影残留。它不是“显示完全失败”：该照片证明官方
仓库版本曾经打通 SPI 引脚、片选、D/C、RESET 和至少一部分初始化；同时竖线周期
高度符合面板处于 18-bit 像素格式却收到 2-byte RGB565 数据的错位表现。

代码复核还发现后续本地版本把低有效硬件 CS 改反。v126 恢复唯一有面板响应版本
的 CS 物理电平，并在每次初始化明确发送 `COLMOD=0x05`。raw 与 LVGL 均改用同一
条 8-bit SPI 路径，每个 RGB565 像素固定按高字节、低字节发送。

```text
build: quickly-openvela/cmake_out/cubie-a7z_nsh_v126_st7735_raw8
nuttx.bin bytes: 2183104
nuttx.bin SHA-256: 88f97d3cb8eabe85f6016a657686d316afa26611ff16e34db0141d4c22ae35c7
image: openvela-a733-cubie-a7z-sd-st7735-v126-raw8-cs-fix-candidate.img
image bytes: 2147483648
image SHA-256: f173ad215c58486e52fc315577666375013fbda6528ec6ecaa350ab63ef73042
embedded kernel SHA-256: 88f97d3cb8eabe85f6016a657686d316afa26611ff16e34db0141d4c22ae35c7
ext4: clean
GPT: no problems found
```

## v126 ST7735/LVGL 实机通过证据（2026-09-27）

实机使用上述哈希镜像冷启动，串口命令与结果如下：

```text
nsh> display status
display: initializing ST7735 direct 8-bit path
display: ST7735 direct path ready
display: direct=ready controller=ST7735 resolution=128x160 format=RGB565-MSB spi=1 mode=0 frequency=12000000
nsh> display raw8
display: raw8 path passed (128x160 RGB565, MSB first)
nsh> display test 30
ST7735/LVGL test active; press Ctrl+C to stop.
nsh> display status
display: direct=ready controller=ST7735 resolution=128x160 format=RGB565-MSB spi=1 mode=0 frequency=12000000
nsh> display raw8
display: raw8 path passed (128x160 RGB565, MSB first)
```

照片确认 raw8 显示完整白框和纯 RGB 三色块；LVGL 显示白底、两眼、嘴和顶部
三色区域，边界完整且不再出现旧版周期性像素错位。30 秒测试自动返回，测试后
`dmesg` 无 SPI、内存或异常日志。因此 ST7735 初始化、SPI1 写屏、RGB565 格式、
全屏窗口和 LVGL flush 均提升为 **hardware-passed**。

证据文件：

- `docs/evidence/st7735-v126/01-pre-first-full-refresh.jpg`：首次全帧覆盖前的随机 GRAM；
- `docs/evidence/st7735-v126/02-lvgl-face.jpg`：LVGL 桌宠界面；
- `docs/evidence/st7735-v126/03-raw8-rgb.jpg`：raw8 白框和 RGB 三色块；
- `docs/evidence/st7735-v126/04-raw8-rgb-repeat.jpg`：重复 raw8 验证。

第一张随机画面只出现在首次完整覆盖前，后续画面正确；将“初始化后立即清黑屏”列为
产品体验优化，而非当前链路阻断。30 秒测试不能替代数小时连续刷新压力测试，因此
stress-passed 仍为否。I2S 本轮明确暂停，未混入显示验收结论。

## v127 LVGL 桌宠产品 UI 构建与镜像证据

2026-09-27 完成 v127 全量构建，`a733display_main.c`、`pet_routes.cxx` 和官方 LVGL
均进入最终 ELF。`display pet` 成为唯一 LVGL 所有者，Agent 通过非阻塞 POSIX
消息队列投递“思考”和最终情绪；同时加入七类程序化表情、普通态眨眼、10 秒回闲
以及面板初始化清黑。

```text
build completed successfully
nuttx.bin bytes: 2195392
nuttx.bin SHA-256: 76cd826833d87870b78fd88c9f3bd40cfa4bc5a88bf12381d0318c8a25dc842e
image bytes: 2147483648
image SHA-256: 0a552c69c6c11b16727939cbf6d09df3e8e6f556ac427021d142885a3b563736
embedded kernel SHA-256: 76cd826833d87870b78fd88c9f3bd40cfa4bc5a88bf12381d0318c8a25dc842e
ext4: clean
GPT: no problems found
```

这证明源码、链接、封装和镜像文件系统完整；`display demo`、消息队列表情切换和
真实 `aipet ask` 联动仍需实体屏验证，不能提前声明 v127 产品 UI hardware-passed。

## v127 实机静态首帧与 v128 LVGL tick 修复

2026-09-27 实机确认 v127 的 `display status/raw8/test` 均通过，`display demo 30`
按两秒节拍输出七种情绪，后台 `display pet` 也能收到“开心、思考、难过”三条消息。
物理屏幕却始终保持普通表情。完整 `dmesg` 未出现 SPI timeout、FIFO overflow、
Data Abort、堆损坏或消息队列错误。这组证据说明显示硬件、首帧 flush、产品状态机
和跨命令事件路径均已打通，故障只在首帧后的 LVGL 刷新调度。

代码复核发现应用调用 `lv_timer_handler()` 后休眠，却从未推进 LVGL 毫秒 tick。
v128 在自管事件循环每次休眠后调用 `lv_tick_inc(delay)`，使刷新、眨眼与回闲定时器
能继续到期；底层 SPI、ST7735 初始化和 RGB565 路径保持不变。

```text
build completed successfully
nuttx.bin bytes: 2195392
nuttx.bin SHA-256: 9f176e6ffed714bd7b530de70046f35579f6a17807c12377c11a61bab63cef3b
image bytes: 2147483648
image SHA-256: 5e7c1f6044957cbc38881619bc082e738bfc180f1753b367e42c2cb09454a830
embedded kernel SHA-256: 9f176e6ffed714bd7b530de70046f35579f6a17807c12377c11a61bab63cef3b
ext4: clean
GPT: no problems found
```

实机照片保存在 `docs/evidence/st7735-v127/01-static-neutral-after-events.jpg`，SHA-256
为 `e563bc73efe27b951aafa9be1067f85f4e2591f7e93d4dd82cd26f692e5d2dcf`。
该照片和日志把 v127 产品 UI 定级为“首帧和事件通过、连续刷新失败”。

随后烧录 v128 完成实机回归：`display demo 30` 的七种表情均在实体屏连续切换；
后台 `display pet` 接收“开心、思考、难过”后画面分别更新；真实 `aipet ask` 在请求
开始发送“思考”，云端回复完成后发送“普通”，实体屏与日志同步。测试期间 Wi-Fi、
DHCP、TLS 和云端 Agent 同时正常工作。`dmesg` 无 SPI、异常、堆或消息队列错误，
测试后 `free` 显示 `4239232064` 字节可用内存。

完整串口记录为 `docs/evidence/st7735-v128/board-test.txt`，SHA-256：
`172536f2460a5388abab7e51abb8c6ec7af092aaf8a4d3dad5019bb90aa5cd67`。
v128 的 ST7735、LVGL 连续表情刷新、跨命令事件和 AIPET UI 联动现提升为
**hardware-passed**；数小时持续动画和高并发压力仍未执行，不能声明 stress-passed。

## v129 LVGL 全屏帧率基准构建与镜像证据

2026-09-27 在独立构建目录完成全量 AArch64 构建。`display fps [seconds]` 每帧改变
全屏背景、强制 `lv_refr_now()`，并在真实 flush 回调中统计完成帧、flush 数、像素、
耗时和吞吐；因此不会把空循环误报为显示帧率。

```text
build completed successfully (06:06)
build: quickly-openvela/cmake_out/cubie-a7z_nsh_v129_lvgl_fps
nuttx.bin bytes: 2195392
nuttx.bin SHA-256: 8fe4c531b12223e0dd785f8cd009426fcb51de2f90c51992c81a06c08043d2b1
image bytes: 2147483648
image SHA-256: 406697b0da50ba18809ed016fb215f7933e023a32a08fdac04ad05aca5719f5c
embedded kernel bytes: 2195392
embedded kernel SHA-256: 8fe4c531b12223e0dd785f8cd009426fcb51de2f90c51992c81a06c08043d2b1
ext4: clean
GPT: no problems found
```

v129 从 v128 副本封装，未覆盖 v128。镜像生成时以上只证明源码、链接、镜像和
内嵌内核一致性，不能提前填写性能数字。

随后完成实机验收：

```text
10 s: frames=296 elapsed=10011 ms fps=29.564 flushes=4736
      pixels=6062080 throughput=1182 KiB/s
30 s: frames=887 elapsed=30002 ms fps=29.563 flushes=14192
      pixels=18165760 throughput=1182 KiB/s
free after test: 4239806816 bytes
```

每帧正好包含 16 个 10 行 flush；10/30 秒帧率相差仅 0.001 fps。随后七种表情完整
循环，`dmesg` 未出现 SPI timeout、FIFO overflow、Data Abort、堆错误或 LVGL
错误。原始串口文本 SHA-256 为
`75a03bf69f2c77c223d78f9b42f3f966a7f70de2e0753a86b5d3ed349f63135f`。
v129 由此提升为 **hardware-passed**，数小时压力测试仍未执行。

## 2026-09-27：v130 文件 RGB → NPU → LVGL 候选

```text
full build: passed
final incremental build: passed
kernel bytes: 2199488
kernel SHA-256: df2ab035a71bc0703fd84370ad1c9e27596e8f662793776681914a18936ac8b9
image bytes: 2147483648
image SHA-256: a8f84509632f73805c95a43d8afaf7aff735038a62741b3d2e4d18e55e39d7da
embedded kernel bytes: 2199488
embedded kernel SHA-256: df2ab035a71bc0703fd84370ad1c9e27596e8f662793776681914a18936ac8b9
ext4: clean
GPT: no problems found
```

源码仓库包含 `a733npu_main.c`、`a733display_main.c` 和共享协议头，但后续实机证明
构建包装器只暂存了显示应用和协议头，漏掉整个 `apps/system/a733npu`。因此最终镜像
沿用旧 NPU CLI：`display vision` 存在，`npu yolo-ui` 不存在；普通 `npu yolo` 的
dog 三框与 black 零框推理通过。v130 状态改为 **hardware-failed (packaging
omission)**，不得作为联动通过版本，也不能声称 UVC 实时链路已完成。

## 2026-10-11：v131 修复 NPU 应用暂存并重新封装

```text
full build: passed (2730 targets)
linked NPU app: apps/system/a733npu/libapps_npu.a
kernel bytes: 2199488
kernel SHA-256: eff2afa0a20c7e956b47dc0bdfb1b493a4bed636cdd46211021d066fa35cd816
BIN marker: npu yolo-ui <640x640-rgb.bin>
BIN marker: YOLO LVGL overlay active
ELF markers: both present
image bytes: 2147483648
image SHA-256: a8dba939ae52f94890abc0f5665ef294386b42af87aae89f6f4558b7ee5e7389
embedded kernel bytes: 2199488
embedded kernel SHA-256: eff2afa0a20c7e956b47dc0bdfb1b493a4bed636cdd46211021d066fa35cd816
ext4: clean
GPT: no problems found
```

`tools/build-a733-wapi.sh` 现显式暂存 NPU 应用的 CMake、Kconfig 和主源文件。v131 从
已实机通过的 v129 副本封装，未覆盖 v129/v130。该证据把状态提升为 build-passed
与 image-verified。

随后完成实机联动验收。dog 输入用时 `1686627 us`，得到 31 个候选和 3 个最终框；
显示端报告 `frame=1 boxes=3`，实体屏出现三个彩色框。black 输入用时 `1680635 us`，
得到 0 候选/0 框；显示端报告 `frame=2 boxes=0`，实体屏清除了上一帧的三个框。
测试后空闲内存为 `4239645200` bytes，未见 NPU、消息队列、SPI、LVGL、异常或堆
错误。由此 v131 的“文件 RGB → NPU → 队列 → LVGL 框层”提升为
**hardware-passed**。详细记录见 `docs/evidence/yolo-lvgl-v131/board-test-summary.md`。
该结论不包含 UVC 实时摄像头、实时预处理或数小时压力测试。

## 2026-10-11：v132 I2S0 FIFO 语义与 INMP441 启动诊断候选

```text
full build: passed (2730 targets)
build: quickly-openvela/cmake_out/cubie-a7z_nsh_v132_i2s_fifo_diag
kernel bytes: 2199512
kernel SHA-256: 3875d699853878c4a99e4b7e4e532fabe23de0405247332ed462fd69ad30599f
image bytes: 2147483648
image SHA-256: e3212a5f107cd842b7090b7f8f2afdcc53da5023a08721cc18861e67471b958a
embedded kernel bytes: 2199512
embedded kernel SHA-256: 3875d699853878c4a99e4b7e4e532fabe23de0405247332ed462fd69ad30599f
ext4: clean
GPT: no problems found
```

v132 从 v131 通过版副本封装，未覆盖 v131。默认测试音改为 16-bit FIFO 写，旧
32-bit 对齐写保留为对照；麦克风采集加入 24-bit 模式、100 ms warmup、原始 RX
FIFO 文件和 PB5..PB8 mux 回读。该证据仅把版本标记为 **build-passed / image-
verified / hardware-pending**；MAX98357A 实际发声和 INMP441 非零 PCM 必须由实机
日志与文件确认。详细记录见 `docs/evidence/i2s-v132/build-summary.md`。

## 2026-10-11：v134 INMP441 S24→S16 修复候选

v133 的安静、说话、拍手三段 5 秒 WAV 文件尺寸均正确且内容哈希不同，但 RMS
全部约为 82，时间包络几乎恒定，试听均为电流噪声。结合原始 FIFO 字与厂商 Linux
`S24_3LE` 配置，确认有效样本位于 bits 23:0；v133 错误右移 16 位，仅保留约 8 位
信号。v134 将转换修正为算术右移 8 位。全量构建 2730/2730、ext4、GPT、镜像
结构和内嵌内核哈希均通过，镜像 SHA-256 为
`e644ce550879a7025a58f531e30568bb37a8fe27126ad0994cdd7da0f2cc2750`。

实机复测否决了该假设：安静、说话、拍手三段录音的 RMS 都约为 2.11 万，听感
均为电流声。`>> 8` 只是把错误数字流放大，v134 不构成 INMP441 音频通过证据。

## 2026-10-11：v135 INMP441 官方单声道 S32 路径

对照 A733 官方 Linux BSP 后，采集改为 `S24_LE/S32_LE` 对应的 32-bit sample
resolution、RXOM=1，并按 `L/R=GND` 只启用 RX slot 0/channel 0。CPU 每帧只读取
一个 FIFO 字，不再采集双通道后丢弃第二字；S32 容器算术右移 16 位写入 S16 WAV。
详细依据和测试方法见 `docs/evidence/i2s-v135/build-summary.md`。

实机复测证明 v135 仍失败：三段合法 5 秒 WAV 中约 72% 样本是连续重复旧值，
三场景 RMS 最大仅差 0.048 dB；软件完成 52800 次读取时硬件 `RXCNT` 只有
13234。A733 手册确认 `FIFOSTA` bit7 是固定 `PLACE_HOLDER`，真正的 RX 可读位
是 bit8，接收计数仅为 bits6:0。旧 `&0xff` 把空 FIFO 的 `0x80` 误判为 128 字，
解释了命令提前返回和电流声。v135 状态改为 **hardware-failed**。

## 2026-10-11：v136 INMP441 RX FIFO 节拍修复候选

v136 改为仅在 `FIFOSTA.RXA` bit8 置位后读取一个 FIFO 字，计数掩码限制为
`0x7f`；同时采用 A733 通用四 RX map、SR24/SW32/RXOM1、S24 算术右移 8 位，
并对 PB8 启用与官方 Linux DTS 一致的下拉。录制期间完整样本先进入 RAM，停止
I2S 后才写临时文件，并在 close 成功后替换最终 WAV，避免 SD 写阻塞 128 字
FIFO。新增墙钟时长、目标/消费/硬件 RX 数、FIFO 峰值、RXU/RXO，以及最近一次
采集模式/状态/提交结果；任何一项失败都不能生成新录音。详细依据与测试门槛见
`docs/evidence/i2s-v136/build-summary.md`。

```text
full build: passed (1783/1783 targets)
kernel bytes: 2199576
kernel SHA-256: 68adbe5ac9a258c44fe3f3ea4628ffd5323738b0993bfea255c11e6725a3cf5f
image bytes: 2147483648
image SHA-256: 3146cc601ee5a59f6815dbb08701d43c6d993bef6a4f852d91444f02253eb358
embedded kernel bytes: 2199576
embedded kernel SHA-256: 68adbe5ac9a258c44fe3f3ea4628ffd5323738b0993bfea255c11e6725a3cf5f
ext4: clean
GPT: no problems found
```

因此 v136 当前状态为 **build-passed / image-verified / hardware-pending**；不能在
收到三场景实机试听、时长和 `capture-proof` 前声称麦克风已经通过。

## 2026-10-11：v136 实机内容失败与 v137 半帧选择修复

v136 的三次 `capture=5000` 均耗时约 `5.40 s`，WAV 数据区均为 480000 字节，
`RXCNT=244801` 与 warmup+目标样本数吻合，说明时钟、FIFO 节拍和文件提交已正确。
但静音、语音与拍手 RMS 均约 21150，且原始样本的低字节集中在
`55/aa/66/99` 等时钟型图案；三种声学场景没有有效差异，v136 判定为
**transport-passed / audio-content-failed**。

INMP441 每帧只驱动 L/R 选中的 32-clock 半帧，另一半帧立即三态。v137 不再把
FIFO channel 0 固定映射到 wire slot 0：`capture=` 与 `capture32=` 默认映射 slot 1，
并提供 `captureleft=`、`captureright=` 对照入口。候选版本只有在语音/静音 RMS、
波形和可听内容随声学场景变化后才能标为录音通过。

## 2026-10-11：v137 实机内容失败与 v138 采样矩阵

用户提供的最新 v137 实机测试：`captureright=5000` 完全无声，
`captureleft=5000` 仍为电流声。left elapsed=5100 ms、target/consumed RX=244800、
hw RX=244801、nonzero=240000、min/max=-30391/30394，原始首尾为
`ffd35536`/`ff96699a`，最终 rxmap 为 slot 0。两侧不同证明半帧映射切换有效，
但没有有效声学 PCM，状态为 **transport-passed / audio-content-failed**。

v138 固定其他格式、warmup 和 FIFO 节拍，只对照 slot、BCLK 极性和 PB8
pull-disable/pull-down；八个独立 WAV 和采集寄存器/内容统计用于下一轮实机。
完整命令、检查字段和物理波形检查步骤见 `docs/evidence/i2s-v138/build-summary.md`。
当前没有 v138 上板语音证据，不能标为 hardware-passed。

v138 全量构建 2730/2730 targets 通过，主机辅助函数检查通过，官方暂存前后
96 个路径条目完全一致。候选镜像 ext4、GPT 和内核回读哈希校验通过：
镜像 `3d19c04540fc433567694b3030dba4b5c74a3f9ed041aa135945d5746723bff1`，
内核 `bbe97a27643bc5c8f7441290b58bd049b66c47a822dfb7c54a0f2d42a8ab9cc2`。
这些证据只证明软件检查和制品完整性，真实语音仍待实机。

## 2026-10-11：v138 八组实机录音内容失败

用户提供的八组日志均 status=0、committed=1、elapsed=3100 ms、target/consumed
RX=148800、hw RX=148801、FIFO peak=1。八份 WAV 的样本数、min/max、平方和、
非零/零值、符号跳变与日志精确一致；FMT0、RXMAP3 和 PB8 pull 回读符合命令。

left 四组 RMS=21152.32/21156.30/21152.56/21159.45，符号跳变约 42.7..44.4%，
一秒窗口能量变化均小于 0.4%。right 四份 WAV 完全相同，每个样本均为 -11437，
AC RMS=0、unique=1、相邻重复=100%，不能视为正常零值静音。
本轮未获得有效语音证据，状态为 **transport-passed / audio-content-failed**。
分析未做主观试听或 ASR。用户随后明确确认八段均按每段“1 秒静音→1 秒说话→
1 秒拍手”操作，三种已施加声学场景的分秒 RMS 差异均小于 0.4%，右侧则完全恒值。
本轮没有体现预期声学响应；分秒数值见 `docs/evidence/i2s-v138/audio-analysis.md`。

需要修正此前把“右侧无声”当作半帧正确的推断。只确认映射寄存器切换和输出差异，
未证明 slot 1 输入真实更新；CHCFG RX_SLOT_NUM=0 与 RXCHMAP3=1 是待核验的
配套疑点。先检查模块 SD/PB8 实际 I2S 波形、接线/供电/CHIPEN，再定向检查 RX 配置。

原始日志、WAV 哈希、可复现分析工具及数据见 `docs/evidence/i2s-v138/audio-analysis.md`。
