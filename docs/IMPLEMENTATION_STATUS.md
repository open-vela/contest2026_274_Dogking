# A733 / Cubie A7Z 适配状态

## v215 加载与常驻启动修复候选（2026-10-30，待板测）

v214 板测暴露两项独立问题：UFS 未挂载时 LLM 误选缺失 SD 文件；worker 日志误接
`/dev/console` 而非已验证的 `/dev/a7zconsole`。已修正输出节点，增加缺失模型的
spawn 前门禁、加载进度/阶段耗时、四条 tensor UFS 路径及完整验证。18 语音回归、
17 ARM 单元、22 摄像头回归、完整链接/镜像回读通过；真实 READY/stop/多轮仍待新镜像验收。
UFS 1000 MB/s 和整机首音频 3–5 秒没有达标证据。详见
[v215](a733/VOICE_LOAD_UFS_V215.md)，旧版/恢复点和所有开发文档保留。

## v214 常驻语音候选（2026-10-30，待板测）

独立任务持有 ASR/vocoder，一次准备、空闲持续等待、自动多轮；新增前滚和端点滞回，
英文拼读/OOV 编码读法、保持 KV 和首句流式输出。完整实现、限制和测试见
[说明](a733/VOICE_RESIDENT_V214.md)。半双工，不支持 AEC/插话，不默认开机录音；
未宣称整机 3–5 秒、自然英文、长期稳定或硬件验收通过。v213 恢复基线保留。

## v204 RAM循环协调器候选已封装（2026-10-29；待板测）

[命令、RAM/VFS约束和测试分级](a733/IMX214_V204_RAM_LIVE_TEST.md)。独立NuttX任务串行完成
受控RAW采集、同v203像素的增强、RAM输入、真实VIP2推理、128×128底图与同帧框刷新。
CPU处理去掉丢弃的线性重算；一会话缓存模型一次、固定tmpfs输入原位复用，不写逐帧介质文件。
原NPU v4 ABI及执行器不改，实验门禁仅加入两个根tmpfs的精确读路径；不嵌套挂载到FAT的/data。
所有帧保留停流/DMA/保护区/恢复/读者门禁；实验清洁成功预算32768、单会话上限3600秒，
这些是软件边界，不是实测连续帧数或时长。显示独占，旧框不套新图，零检测清空，清理失败隔离。
协调器故障注入、128次控制器模拟、100000次显示元数据检查、400组实际C像素兼容及ARM对象预检已通过；
它们不证明真实YOLO速度/准确率、热稳定、实际常开视频或MP4录制，原v203-r2通过版和语音/UFS状态不变。
完整构建及v186→v199旧回归、新协调器/显示/VFS/日志/像素/重采测试、原生ARM对象、24构造器、
4 MiB NOBITS帧池和runtime_end边界均通过；291项官方暂存路径逐字节恢复。
新2 GiB镜像已回读并核对全部未改分区，Windows完整SHA一致；[封存证据](evidence/camera-v204-final/README.md)。
固件源码点`fe641a4`，封装工具点`318784e`；镜像SHA为`f21a9d0bd8aab20264db3e44273ebf372c629f33ee7bba6c49659f0e2e82326d`。

## v203-r2 两次真实单帧与SPI显示通过（2026-10-29）

[实机证据和范围](evidence/camera-v203-board-20261029-r2/README.md)。用户确认颜色正常；
generation=1→2、两帧FNV不同，published=1且validation/error/cleanup=0、owned=0。
两份实际RAW绑定串口FNV，八个payload/份与生产C重放逐字节一致；源文件未变，不存入Git。
固件、镜像SHA沿用原v203-r2；保存新本地恢复标签`a733-v203-camera-board-pass-20261029`。
下面“仍待板测”为构建时记录，本次补足双次实拍/显示，未补足冷启动循环、32次实拍、长稳、
ISP/AE/AWB、连续视频或摄像头YOLO准确率。语音/UFS等状态不改变。

## v203 新固件：偏黑/方向和受控重采候选（2026-10-29）

[测试入口](a733/IMX214_V203_TONE_REPEAT_TEST.md)。v200一次真实RAW/自动SWOUT1/SPI通过的用户
日志已追加在[evidence](evidence/camera-v200-board-20261029/README.md)，补充下面历史待测状态。
v203保留旧五文件，新增RAW10域经验黑电平/软件增益/手动RGB/Gamma2.2及180°预览和可选模型输入。
两份私有实际RAW经过生产C处理，源文件摘要不变；不归档人像，不等同新固件上板验收。
仅在上一帧有效且停流/接收/DMA/控制恢复成功、无读者时复用本启动已核验配置，最多32个成功单帧。
失败不强制重启DMA；新传感器曝光/增益只读，不盲写。不是ISP、AE/AWB或连续取流。
ASan/UBSan、32个变化模拟帧、读者/故障门禁、旧版本回归、ARM栈、完整链接及289项SDK恢复均通过。
**v203-r2是独立候选镜像，重复实拍、画质、冷启动、NPU输入准确率及热稳定性尚待板测。**
原语音/I2S/UFS/STM32待测范围与恢复版本不改变，本轮不擦UFS、不改存储或SPI驱动。

## v202 RAW一致性与离线图像报告（2026-10-29）

[操作/边界](a733/IMX214_V202_OFFLINE_RAW_REPORT.md)。独立重算RAW→模型RGB/显示预览，
逐字节对比；新增RAW10异常/亮度统计、v201日志重绑定、原图坐标映射及无网络HTML/PNG/JSON报告。
CRC自洽但RGB不属于该RAW时拒绝；无推理日志时不编造框，合成外观示例明确不算硬件证据。
不改固件/内存/启动/模型，不新增镜像，不操作开发板或介质。**v200/v201联合板测仍待安排。**

## v201 主机单帧联调工具（2026-10-29；不改 v200 固件）

[操作与边界](a733/IMX214_V201_HOST_COORDINATOR.md)。开发默认不接板；新增两路SSH协调、
COMPLETE/文件尺寸、底图和NPU输入CRC、序号和整批框确认，以及下载目录离线校验。
等待真实UI就绪，不用固定sleep猜测；自然结束，不自动复位/重试/取消远端DMA任务。
只增加主机工具、测试和文档，保留全部旧镜像与恢复点，未重新编译内核或放开连续取流。
**v200自动电源门禁、真实RAW、摄像头YOLO视觉准确率及新工具板上SSH仍未测试。**

## SWOUT1 手动显示复测通过，v200 启动固化候选已生成（2026-10-28）

[手动板测摘录](evidence/swout1-v128-board-20261028/README.md)：0x11=17、0x12=17，
仅把11置1f后 v128 三色块/LVGL/七表情恢复；不能归因于屏幕损坏或已证明UFS抢引脚。
[v200命令/限制](a733/SWOUT1_V200_RAW_TEST.md)、[最终证据](evidence/swout1-v200-final/summary.json)。
启动包源码d8eefd500ac325ccaebdb2bc99ca56578c418cf8；严格复用v199内核，不改固件或驱动，
不重编内核，只重新审计原完整构建、289项SDK恢复、内存布局及当前主机/ARM回归。
电源只允许已确认的17->1f或1f不写路径，未知/异常停止本脚本，成功读回后才提交hand-off。
新镜像的活动extlinux入口被移至内容完整的备份名，让SD distro扫描执行boot.scr；
所有非根分区、内核、模式/GPL来源、旧脚本、UFS内容保持不变。外层固件仍可能选择其他设备，
因此不宣称解决了UFS残留引导或自动启动已经实测。
镜像SHA256=a729b7442ffc764063b191d8ac3ebb38c4a2812c18f21cc9dacf2cd02fbaaa33。
**v200自动电源门禁与真实RAW采集尚待上板；语音既有待测状态不变。**

## v197/v198 板测失败及 v199 修复镜像（2026-10-28 追加）

[原始失败日志](evidence/camera-v197-v198-board-20261028/README.md)和历史待测段落保留。
v197 脚本 CRC 与候选一致，ID/模式/RX 通过，DMA 配置首错/恢复错均位于自动 BUF_LEN，
未开流且恢复不完整；v198 此次未脚本启动，交接缺失，读写为零，未运行真实帧和推理。
[v199 修复和命令](a733/IMX214_V199_SIZING_REPAIR.md)沿用厂商自动尺寸模式，
不写自动派生字段、不改 IOMMU 全局映射、不降低帧完成/停机/内容和保护区检查。
两个归档源码在新语义模型中都复现原错，修复后 276 场景/259 回调故障注入及 ARM 回归通过；
完整构建和 289 项 SDK 逐字节恢复成功，镜像回读/未修改分区摘要验证及 Windows 独立 SHA 一致。
固件源码 6d1ece1c1a385db8f1f6163b172fbbf51a99a002，独立镜像 SHA256 为
78d74c627ba002d5a94bcd8a877bef58b17ba848155858e3188f0d118bbad724。
**仍是待板测候选，不宣称已经出图**；语音、UFS 引导问题和物理存储未改动。

## v198 同输入单帧识别展示镜像已生成，待板测（2026-10-28）

[命令/数据契约/边界](a733/IMX214_V198_SAME_INPUT_VISION.md)，
[最终证据](evidence/camera-v198-final/summary.json)。
在 v197 候选上添加同一 RAW 的 640×640 RGB24 文件、LVGL 底图、协议 v2 的输入 CRC/
序号/整批绑定；复查 DMA 迟到错误、映射变化、输出完成标记。没有改 NPU 底层或模型。
源码提交 735c1dd，主机/ARM/完整链接/SDK 逐字节恢复/镜像一致性检查通过。
镜像 SHA256=d9236d51985f26a713aca55d5fd19cb94aa0f676bc059db541b575371c44ed07。
不把旧文件输入 NPU 板测、v196 包级通过或主机模拟扩大为本版摄像头全链路通过。
语音、UFS 引导清理等已有待办状态不变，旧文档和恢复点保留。

## v197 RAW 单帧与预览镜像已生成，待板测（2026-10-28）

[功能、命令和限制](a733/IMX214_V197_RAW_FRAME.md)，
[完整链接/镜像/内存布局证据](evidence/camera-v197-final/summary.json)。
新增独立 4 MiB 常驻帧池、parser1/RAW bridge/BK0 单次 DMA、停机与保护区校验、
RAW/RGB/PPM 导出和 ST7735 预览。固件源码提交 947cb9c，完整主机/ARM 回归通过；
镜像 SHA256 为 580e5dbfebb986ffdad885ea22a4a4ef56c6d882b0cdb1fe4569038e87b18acc。
这不是硬件出图验收；IOMMU/RAW 格式/实际画面仍须用户上板验证，不扩大 v196 包级结论。
不运行 ISP、实时 YOLO，不变更语音待测结论或处理 UFS 残留引导。

## v196 追加一次实机包接收通过（2026-10-28）

用户原始日志和结果见 [板测证据](evidence/camera-v196-board-20261028/README.md)。
ID=0214、模式 153 次写/14 项回读一致；源 ON/STOP 均成功。
RAW10 包头观察 119 次、FS/FE 各 14 次、errors=0，控制与接收端结束均 owned=0、
error=cleanup=0，后续 status 一致。这是包级组合通过，不是 RAW 帧、FPS 或长稳验收。
本次不改固件、不生成替换镜像；保留 v196 镜像及新的包级通过恢复点。
日志未含加载 CRC/SHA，不能把版本字符串当成精确二进制独立身份核验。
后续先实现 CSI1 parser、独立 DMA/IOMMU/缓存/保护区与有界停机，拿到真实 RAW 后再接
RGB/YOLO/SPI 展示。UFS 引导暂停、语音待测状态不变，下面历史待测文字保留。

## v195 板测失败及 v196 修正候选（2026-10-28 追加）

v195 未开流：ID/模式通过，但接收端准备及恢复 -5、owned=1。
原始日志完整保留，不覆盖之前“待测”的历史时间点。
v196 修正 DESKEW1 14/18 偏移、校准结果/计数器的动态字段处理，并分离首次/清理错误。
独立厂商宏核对、旧代码 93R/29W/05810004 模型重现、2249 接收核心用例及 ARM 对象
检查通过；完整链接、24 个构造函数、287 项 SDK 恢复、镜像回读和未修改分区
摘要核验通过。独立 2 GiB 镜像 SHA256 为
`b29c27c98b5494a079b84892e3d8bc34b1e7e99298b011e355fe004d526f8ba1`。
**尚待 v196 真实板测，不是 RAW/YOLO 完成。**
见 [v196 说明](a733/IMX214_V196_REGISTER_REPAIR.md)。

## v195 MCSIB 包接收候选（2026-10-28）

v194 一次真实模式写入/回读/恢复通过，不改旧镜像。新增 J5 MIPI1 Combo PHY/port，
只在显式新命令下配置 RX、开流、收集 500ms 新包状态、停流与逆序恢复。
没有 DDR 目标，不访问 parser capture、DMA、IOMMU 或 ISP，不发布伪 RAW 文件。
接收核心 2247 主机用例通过，含所有 2237 个 callback-after-effect 失败及 PK 引脚冲突/恢复；
旧模式/ID/脚本/像素回归及 ARM 对象检查通过，最终 RX 最大单函数栈 144B。
**尚待真实 MIPI 包实测；不把此结果扩展成 RAW 帧/YOLO 成功。**
最终构建 r3/host-r5/24 构造函数/287 项 SDK 恢复及完整镜像只读复核通过。
镜像 SHA256=`3bf513770bb4a42d7aa12bdc4f97a31f7a43426fd31d19df9b96ebce02571ff3`。
来源、限制、命令与制品状态见 [v195](a733/IMX214_V195_MIPI_PACKETS.md)。

## v194 追加实机验收（2026-10-28，单次模式实验通过）

用户完整日志见 [证据与边界](evidence/camera-v194-board-20261028/README.md)。
真实 ID=`0214`、模式 writes=153、verified=14/14、blob-error=0、mode error=0，
elapsed=102ms；结束 state=2、owned=0、error=cleanup=command-error=0、无总线隔离。
这是与最终候选 CRC=`4de89c0c` 一致的一次真实控制/模式通过，历史“待测”段落保留。
未开流，CSI/DPHY/DMA/ISP/RAW 帧/YOLO 仍未验证，不能以 102ms 推断 FPS。

## v194 摄像头待机模式实验（2026-10-28）

新增 v193 一次真实板测证据：同次 U-Boot 交接、固定 ID=`0214`、match=1、
无错误且控制恢复；见 `evidence/camera-v193-board-20261028/serial.txt`。
这不是实测电压/频率，也不是模式/CSI/帧验证。

v194 在该受控实验基础上增加外置 SHA256 锁定配置、153 次模式写入与 14 项回读，
始终保持 STREAM_ON 禁止；一次操作完成 ID→待机模式→回读→恢复。
5971 项组合测试覆盖真实 SDK SHA、所有数据字节损坏和逐回调故障；
最终构建/镜像关联见 [v194 范围与测试](a733/IMX214_V194_STANDBY_MODE.md)。
完整链接、24 构造函数/内存检查、285 项 SDK 逐字节恢复、镜像文件回读/分区摘要已通过。
独立 2 GiB 镜像 SHA256=`76c73bd6c79c2b5d9c6f63bb675f27501e935087523be26382913f0edc6f05a9`。
**v194 硬件仍待测；尚无 CSI/DPHY/DMA、真实 RAW、摄像头 YOLO 或 ISP 验收。**
UFS 引导清理不执行，语音待测不冒充已完成，所有历史证据与恢复点保留。

## v193 摄像头 ID 探索实验（2026-10-28，开发中）

追加：已完整链接并生成独立 2 GiB 候选镜像，最终关联/布局/镜像回读和分区摘要通过，
尚待上板读取真实 ID。外层脚本一次额外退出 1、成功 SDK 子构建与恢复的区别已保留记录。
镜像 SHA256：`c590fe77b30f12e47ac90b900aa8c055349a88577d3c68e8f75a5d9a9f21aac3`。

v192 一次真实脚本启动、控制准备/时钟切换/恢复已经通过，原始日志另存且不改历史构建证据。
用户 FPC 不便实测，本轮增加显式假设 firmware rails/名义 sys24M 的一次性固定 ID 实验，
**不伪造电气测量、不解锁严格资格、不启动视频流**。有界释放/读 ID/STOP/恢复及隔离路径
进入真实节点和 CLI；软件/目标对象结果与最终镜像、真实 ID 验收分开记录。
见 [v193 范围与测试](a733/IMX214_V193_ID_EXPERIMENT.md)。

## v192 摄像头复位保持控制候选（2026-10-28 恢复完成构建）

用户已确认 v191 一次启动到 NSH、CPU 合成图像自测和 12 次只读寄存器采集通过；
这不等于传感器 ID/取帧通过。现获得 U-Boot AXP8191 六项真实配置读数及加载地址。
v192 新增同次启动、精确 Image 绑定的 RAM 交接，避免 NuttX 抢 SCP 的 PMIC 总线。
控制面仅保持 RESET/STBY 低、固定 TWI3 空闲配置与 24MHz/1MHz 名义 MCLK 测试；
供电保留是明确的固件假设，不伪造实测或解锁旧的严格传感器门禁。
昨日完整构建因关机中断，今日增量恢复成功；285 路径 SDK 暂存逐字节恢复。
已完成主机/SDK 对象、完整链接、交接脚本生成、独立镜像回读/分区摘要检查；
2 GiB 候选镜像 SHA256 为 `c359a2d23e3a86f6fdd7d3e5d702bd740a6d8375aadb8329a346daf704234d79`。
**尚未实机验证 v192；没有释放 RESET、读 ID 或摄像头帧。**
见 [v192 控制范围与测试步骤](a733/IMX214_V192_CONTROL_TEST.md)。

## v191 摄像头资格检查候选（2026-10-27）

用户暂缓 v190 板测，已校验其 1349 文件检查点及五项外部引用，保留待测。
本轮首次为 V1.11 单摄 J5 单独完整链接摄像头诊断/预处理应用的测试镜像；
新增板上合成像素自测和六项 handoff 配置解释，所有电源/实测 MCLK/所有权门槛不变。
**这不是取帧镜像：真实板级提供层、CSI DMA、ISP 和实时摄像头 YOLO 未完成。**
构建产物、测试含义与下一道实机门槛见 [v191](a733/IMX214_V191_TEST_IMAGE.md)。
原始构建记录为未板测，保持不改写。之后用户确认一次 NSH 启动、CPU 自测与
只读快照通过；不能将这些或 6/6 配置匹配记为摄像头硬件通过。

## v190 离线会话/等待优化候选（2026-10-27）

新增显式 `aipet audio offline`：先检查/预热 LLM，再 MIC ON，逐轮完成真实
ASR→本地 LLM/上下文→CPU 声学/NPU vocoder→I2S，动作与云回退关闭。
采集侧能量端点不再跟随 NPU 积压，同命令复用三路 ASR/vocoder 并清空全部状态；
CPU 声学和 ASR staging 临时选择已验证 A76，NPU 模型和硬件时钟不变。
同组/同线程 lease、跨 NSH fd 拒绝、并发 mutex、错误/取消/隔离回归已覆盖。
真实 CPU 模型和捕获重放/设备 mock 软件检查通过，最终目标构建/镜像独立记录；
**新版实时麦克风、完整听音、多轮稳定性和 3–4 秒首音频仍未板测达标**。
当前状态与完整测试顺序见 [v190](a733/OFFLINE_SESSION_V190.md)。
v188 已有一次启动板测通过；v189 流式断句修复仍待上板，所有历史原始记录保留。

## v180 本地LLM有界上下文（2026-10-25）

加入默认3轮RAM历史、可清空/关闭、不污染历史的预加载与精确已计算token KV复用。
MiniCPM5/Qwen真实主机参考数值回归与目标编译通过，不改NPU/UFS/STM32/I2S。
新增功能和提速幅度尚未板测；已有用户日志热首段2.47–3.75秒不是新版成绩。
完整离线语音的缺口仍与v179相同，见[v180说明](a733/LLM_CONTEXT_V180.md)。

## v179 离线语音核心（2026-10-25，继续开发，未安排整机测试）

用户指定先完成LLM/ASR/TTS，再统一测试；STM32继续跳过。本地Git基线已保存为
`f638874` / `a733-v178-llm-candidate-20261025`。新增通用ASR贪心解码、真实CPU
ONNX三音频对照、Matcha文字音素12句对照、mel尾部/PCM转换及显式无云回退应用路径。
**target ASR provider与Matcha RTOS声学执行器仍缺失，不是全链路完成**。
细节见 [v179](a733/OFFLINE_VOICE_V179.md)；版本保存见 [本地Git](LOCAL_GIT_RELEASES.md)。

## v178 本地LLM候选与ASR模型转换（2026-10-25）

[阶段说明与测试](a733/LLM_V178_MINICPM5_AND_VOICE.md)：MiniCPM5-1B支持、完整UTF-8
流式输出及权重/目录/分词/前缀KV缓存。主机真实模型12个位置全部logits精确一致；
ARM64及既有I2S应用回归通过，首段1–2秒/板上质量待验收。LLM仍为CPU/NEON。
三个通用ASR fp16 NBG导出和CID检查通过，但Linux真实golden、openvela ASR和本地任意
文字TTS仍未完成。STM32本轮跳过，不改变安全门禁；不动UFS引导残留，历史记录保留。

## v177 UART4下位机与语音控制整合候选（2026-10-23）

[测试与接线说明](a733/AGENT_UART4_STM32_V177.md)：UART4改为独占115200，旧UART TTS
停用但源码/镜像可恢复，输出改为I2S。对照STM32提交e83a9cac实际协议实现CRC、SEQ、
ACK/NAK、独立心跳、有限MOVE/SERVO、手动解锁、故障/断链去使能及官方Agent工具。
语音目前复用官方**云ASR/云TTS**，明确短命令走快速受限控制；不宣称本地NPU流式ASR已完成。
603组帧已与STM32原始C encoder逐字节一致，PTY模拟语音链/断链/NAK/错SEQ等通过；
实际双板、录音、云API、持续心跳和动作仍待本版实机验收。
UFS旧启动链清理按用户要求暂停，原始备份/工具保留，本版不执行清理。
另据用户新日志，v175设置自启动后一次正常重启 `mounted-existing-FAT/ready=1` 已通过，
不再把这一具体行为笼统标为“完全未测”；冷启动概率故障和长稳仍未解决。

## v175镜像封存 / v176摄像头探测准备（2026-10-23）

v175新SD镜像、478源归档、974文件/5引用恢复点、新目录Git恢复、613补充材料清单和
SDK173路径恢复均完成，见 `out/v175-final-integrity.json`；自动挂载新行为仍待实机。
摄像头 [v176](a733/IMX214_V176_QUALIFICATION.md) 只完成严格身份/只读状态核心、
故障回归和ARM64编译，最大栈80B；Linux对照脚本已用模拟命令检查默认不启动视频流。
实际单口/CAM2的板级电源/时钟provider未限定，CSI/VIN/ISP/DMA、标准视频节点和实时
YOLO未实现/未通过；未把探测代码混入v175，不增加“摄像头已完成”的声明。

## v174 用户实机通过 / v175 自动卷与模型路径候选（2026-10-22）

v174 1.1 GB 模型完整 SHA256、重启 compare、UFS 加载 15.9906 s / 计算 0.6212 s、
128 MiB 读取约 67.58 MiB/s 已有用户日志；全部层 CRC 保持通过结果。
新增 [v175](a733/UFS_AUTO_V175.md)：独立开机任务限定旧卷身份/几何/FAT 主备份/FSINFO，
一次初始化后挂载 `/ufs`；Agent 自动优先已通过的 `.v174.gguf`，拒绝旧 0 字节模型，
不可用时回到 SD。不开机格式化、迁移、scratch WRITE 或隐藏重试；冷 PMIC/PHY 初始化
仍未实现。v175 的新启动路径尚待实机，不借用 v174 手动结果宣称成功。
当前摄像头为 Radxa 13M214 / IMX214，不是误写的 12M314；摄像头全链路仍未通过。

## v173部分持久性实机通过 / v174批量存取候选（2026-10-22追加）

[用户完整日志与界限](evidence/UFS_V173_RUNTIME_20261022.md)：probe/64KiB备份重启后可读，
大模型cp长时间无反馈且断电，目录有模型名字但未给大小/摘要，完整性未知，不确诊FAT损坏。
[v174流程](a733/UFS_BULK_V174.md)增加原始只读块视图、256KiB后台copy/compare/readbench、
SHA256全目的文件验证、串口status/stop。底层逐段读回和CACHE10保留，默认不开机挂载/
格式化，不宣称HS Gear或满速；官方FAT只读挂载仍未适配，不把只读块节点冒称为只读文件系统。
旧版本/所有开发文档原样保留；v174实机仍待测试。

## v172受控写入实机通过 / v173数据卷候选（2026-10-22最新补充）

用户日志确认scratch两轮pattern、3WRITE/4CACHE10、原件还原、保护区/停止全部通过。
末尾dd错误已定位为NSH四处64位seek返回值截断，不是UFS写入失败。
原件、数值和限制见 [v172实机证据](evidence/UFS_V172_RUNTIME_20261022.md)。
v173修复dd并新增固定第三分区 `/dev/ufsdata`、逐段写回读+flush，排除尾部512KiB；
整盘 `/dev/ufs0` 仍只读，官方FAT32由用户手动首次创建/挂载，不自动格式化/迁移SD。
数据文件系统和模型盘仍需实机验收，见 [v173一轮测试](a733/UFS_DATA_V173.md)。
下方历史快照不追改，旧manifest/镜像/文档/恢复点全部保留。

## v171只读实机通过 / v172固定窗口写入候选（2026-10-22最新补充）

v171用户日志确认标准块读、两遍8MiB CRC、4GiB偏移读取、保护区和内存回落通过，
见 [v171原始证据](evidence/UFS_V171_RUNTIME_20261022.md)。旧封存manifest不改。
v172增加固定Kingston测试Linux分区内64KiB准备/显式确认/写入/flush/回读/原件还原。
仍保留只读块API，尚待实机写入验收，不是可写文件系统/模型盘；备份仅RAM，失败或
断电可能破坏测试Linux，无错误后自动还原或重试。
见 [v172风险、命令和构建](a733/UFS_SCRATCH_V172.md)。下方历史记录原样保留。
软件侧642个scratch故障用例、驱动36场景、11个ARM64对象、49关键源、最终完整复编、
BSS布局、454文件源归档、SD镜像提取/非内核分区不变和167路径SDK恢复均已核对通过。
新镜像/源码/恢复点见 [ARTIFACTS](ARTIFACTS.md)，仍不增加v172实机通过声明。

## v170 实机原始读取通过 / v171 标准只读块设备候选（2026-10-22）

用户 v170 日志确认 Kingston CY14-128、4096B 逻辑块、128,026,935,296B；
三对头中尾样本、GPT CRC/基本范围、保护区和停止均通过，无 UA/隔离。
这不是独立冷初始化、文件系统/写入、持续吞吐或长稳通过。
v171 合并标准 NuttX 只读块设备、64KiB 传输/1MiB 有界回调、
跨尺寸头样本与两遍较大连续读取 CRC；不自动 mount/格式化/迁移模型。
生产主机回归、35 场景、10 个 ARM64 对象、完整构建、三块缓冲区布局、
164 项 SDK 恢复及独立 2GiB 候选提取校验已通过；v171 尚待实机。
见 [v170 实机结果](evidence/UFS_V170_RUNTIME_20261022.md)、
[v171 一轮测试](a733/UFS_BLOCK_V171.md)、[软件证据](evidence/UFS_BLOCK_V171_20261022.md)。

## v169 实机 NOP 通过 / v170 合并只读验收候选（2026-10-22）

用户 NSH 日志确认本次 quiesce→私有 DMA NOP 完成，保护区和停止检查通过、无隔离；
原文追加保存，不追改旧封存。这不是介质读取/文件系统或独立冷启动通过。
为减少烧录轮次，v170 用第二块独立 arena 合并身份、容量、头中尾 16KiB 精确重复读及
GPT CRC/基本范围检查；不增加介质写/格式化/挂载，不开放旧 attach，不复用 NOP arena。
一次初始合法 UA 可消费重发，传输失败/超时不重试；每次启动一个手动批次。
主机故障注入、26 个驱动整合场景、10 个 ARM64 对象与 46 项 staging 已通过；
完整构建、两块独立 arena 布局、164 项 SDK 恢复及新独立 2GiB SD 候选镜像提取校验通过；
v170 尚无实机介质结果。镜像/源码哈希及最终独立恢复验证见软件证据与封存清单。
见 [v169 实机证据](evidence/UFS_V169_RUNTIME_20261022.md)、
[v170 批次与测试](a733/UFS_READCHECK_V170.md)、[软件证据](evidence/UFS_READCHECK_V170_20261022.md)。

## v168实机停止通过 / v169私有缓冲区NOP候选（2026-10-22）

用户本次boot到NSH、quiesce三次零写成功，刷新快照确认IE/runstop均0、原base不变；
这是一次空闲队列停止实机通过，不是DMA/文件系统通过。
v169新增默认关闭、每次启动最多一条NOP的独立试验；静态guarded BSS、逐页AT R/W翻译、
cache/屏障、响应/非预期修改检查与写后失败隔离，仍不开放attach或媒体命令。
主机故障注入、23个驱动场景、9个ARM64对象、完整构建、静态arena布局及162项SDK恢复通过；
独立v169 SD候选镜像、445项源码归档及镜像提取/非p3数据保留校验通过，尚无v169实机结果。
SDK中断恢复问题已按原始哈希修复，构建器改为持久备份及失败保留；所有旧版本继续保留。
见[v168真实证据](evidence/UFS_V168_RUNTIME_20261022.md)及[v169步骤与边界](a733/UFS_DMA_NOP_V169.md)。

## v167实机HCI通过 / v168空闲队列停止候选（2026-10-21）

用户本次完整启动到NSH，32项HCI读取稳定、link-ready=1、doorbell=0；
固件IE=30e15、UTRL/UTMR runstop仍1，因此尚不可替换DMA地址。
新实验quiesce入口仅允许三项零写、逐步复核、超时/写后失败隔离，一次启动只尝试一次。
不清IS/门铃、不改base、不复位/重启、不分配DMA、不发介质命令；原attach仍关闭。
软件故障注入、ARM64对象、完整构建、38项暂存、159项SDK恢复及2GiB镜像校验通过；
独立v168候选已生成，v168尚无实机结果，UFS文件访问/模型盘仍未完成。
见[v167真实日志](evidence/UFS_V167_RUNTIME_20261021.md)、[v168边界/测试](a733/UFS_QUIESCE_V168.md)。

## v166 实机采样 / v167 固件交接观察候选（2026-10-21）

用户v166日志确认到NSH、两次CCU/RTC稳定，U-Boot报告128.0269GB UFS/G4双lane；
echo -n复测确认-22是换行解析问题。新证据另存，不回写旧封存。
用户确认UFS已有Linux，随后明确只是测试、无需保留数据，允许之后清空作为模型盘；
当前不立即格式化，先确认LUN/容量/介质身份与可靠读写，不动SD及其他盘。
v167增加独立默认关闭的manual handoff HCI只读观察，窄profile门禁、32次读取上限，
不清队列/中断、不改DMA地址，不发介质命令；transport/attach权限仍关闭。
这是实际只读接管之前的交接状态关口，尚未完成UFS文件访问。
修订版完整构建、36项critical暂存、157项SDK恢复、2GiB镜像提取/分区保留校验通过；
独立v167镜像已生成，仍待实机handoff结果，不能据此清空或迁移模型。
见[v167测试与边界](a733/UFS_HANDOFF_V167.md)和[v166真实结果](evidence/UFS_V166_RUNTIME_20261021.md)。

## UFS v166 板级安全取证候选（2026-10-21，待实机）

新增固定13项CCU/RTC双采样与报告，无SMC/PMIC/HCI/DMA访问；
收紧现有probe/attach门禁，供电/domain/owner/DMA未确认时返回-EACCES，不能force绕过。
同时删除旧hwdiag直接读取UFS HCI的旁路。v165核心与全部旧代码/文档/镜像保留。
已到需要真实板级供电及固件交接证据的关口；不是UFS高速读写完成。
独立完整构建、34项源码暂存、155项SDK输入恢复及2GiB镜像提取/校验通过；
原生Windows复核431源码归档和非p3区域2025848832B一致，不等于实机启动。
见[v166测试与恢复](a733/UFS_BOARD_V166.md)及[实际软件/镜像证据](evidence/UFS_BOARD_V166_20261021.md)。

## UFS v165 M-PHY/UIC核心（2026-10-21，源码/对象验证）

新增生产link core，完成RMMI、SRAM、校准、UIC link startup及CPort回读，
独立板级门禁、双重超时和失败隔离；3934故障注入点及ARM64对象编译通过。
尚无真实供电/clock/reset adapter或caller，不能上板执行cold-start；无新镜像/完整链接验收。
旧v164 transport/镜像和全部历史保留，不改PMIC/文件系统/介质。
参见[本阶段实现与明确边界](a733/UFS_LINK_V165.md)。

## UFS 器件信息补齐（2026-10-21，未上板）

用户提供 Kingston UFS128-CY14 丝印；官方产品表确认系列。
完成 A7Z v1.11 原理图与 CY14 子型号资料的供电/NC 球交叉检查，
新版 Linux 取证工具增加真实供电 phandle→regulator of_node→电压读回关联，未知不推定通过。
没有编程 PMIC/复位/时钟，也没有改变 v164 固件或旧镜像；冷启动和高速读写仍未完成。
见[芯片资料、供电边界与恢复点](a733/UFS_KINGSTON_CY14.md)。

## 新增 UFS 数据存储任务（2026-10-20，第一阶段未验收）

用户已焊接128GB UFS3.1，新增模型/高频数据高速存取目标，SD启动和原始数据保留。
新增默认关闭、手动执行的 UFSHCI 只读传输及标准块设备接口；只注册节点不阻塞开机，
支持有严格前置条件的固件已初始化链路接管，不含冷启动PMIC/M-PHY流程。
目前不能宣称UFS已能使用或高速读写通过，不格式化/搬迁模型，不抹去历史“无UFS”记录。
主机模拟及ARM64对象检查通过，提供Linux无损取证工具，真实硬件仍按用户约定以后统一测。
最终v164独立候选完整构建、SDK151项恢复及镜像提取内核校验通过；不是可用UFS高速版本。
参见[实现边界、测试和恢复](a733/UFS_STORAGE.md)。语音和IMX214待办不取消。

## 后续方向与统一板测约定（2026-10-20）

按用户新要求，语音链路推进至可衔接阶段后，继续完整适配 Radxa Camera 13M 214，
目标为 IMX214 → CSI/VIN/ISP → 标准采集 → NPU YOLO → SPI/LVGL 预览与结果展示。
用户暂缓新旧板测，之后统一执行；未测不标通过，当前完整流式 ASR/摄像头都尚未完成。
新增不可覆盖本地 checkpoint/新目录恢复工具，保留历史代码、文档、旧镜像及 v163 封存，
不在本轮烧录、启动摄像头/录音或更改既有驱动。
见[长期实施与恢复约定](a733/VOICE_CAMERA_RECOVERY_ROADMAP.md)。

## v162 音频板测通过 / v163 前端数值诊断候选（2026-10-20）

用户确认 v162 录音电流声消失；8 次录音、3 次播放、零超时，末次 heap/NPU 池恢复。
这不代表 KWS 准确率或整条语音助手通过，3 条录音仍有增益饱和，
见[v162 本次板测证据](evidence/local-voice-v153/openvela-v162-runtime-audio-passed.md)。
v163 保留 v162 驱动及 x5 默认，增加纯本地 WAV 电平、实际 fbank 导出/逐元素对照，
共享前端并在 NPU prepare 前释放临时状态；不修改数学选项/模型/阈值或严格 oracle。
主机回归、ARM64 对象/完整构建、SDK 恢复、镜像校验已通过，独立 v163 候选已生成，
实际 WAV/fbank 数值差异、削顶根因和声学准确率仍待本轮板测。
见[v163 测试与构建](a733/LOCAL_VOICE_FRONTEND_V163.md)。旧版本和全部开发文档保留。

## v161 部分板测 / v162 录音功放静音修复候选（2026-10-20）

v161预生成features0两个入口匹配；六WAV仅静音组精确匹配，前五组特征CRC不同，
四次麦克风运行均完成但只有一次候选，用户报告后两次录音期间扬声器电流声。
不标记整体通过，见[v161实际记录](evidence/local-voice-v153/openvela-v161-runtime-partial.md)。
v162固定录音/空闲PB7功放DIN为GPIO低电平，显式播放时恢复I2S；不改麦克风时钟/模型，
不虚构独立SD_MODE静音。主机已验证pinmux顺序/其他位保留/采集错误退出/播放及tone恢复。
WAV跨平台数值问题仍单独未解决，未放宽CRC。新候选待上板听音，
见[v162测试流程](a733/I2S_CAPTURE_QUIET_V162.md)，旧镜像和全部历史保留。

## v160 实机通过 / v161 真实音频输入候选（2026-10-20）

v160用户六场景及四组CRC A/B共3187次真实调用通过，专用池恢复；同镜像CPU6
chunk总耗时降低约35%，仍有6/86块超160ms。见[v160板测记录](evidence/local-voice-v153/openvela-v160-runtime-passed.md)。
v161新增WAV/ICS43434分段录音→真实fbank→本地NPU KWS诊断，保留严格旧oracle；
任意声音只发布明确标注的候选，不冒充ASR/准确性验收，不触发Agent/云端/播放/设备动作。
现有公共录音是同步整段，故本版先录后推理，不是连续实时流。新候选待上板，
见[v161测试步骤](a733/LOCAL_NPU_KWS_V161_INPUT.md)。所有通过镜像及历史文档保留。

## v159 实机通过 / v160 CRC全扫描加速候选（2026-10-20）

v159六组主场景及五组A/B共2576次调用全部正确；相同CPU6的bounded分块总耗时
比fullclean降低15.44%，主测试资源恢复。只有噪声场景全部满足160ms，其余尚超预算。
见[v159板测归档](evidence/local-voice-v153/openvela-v159-runtime-passed.md)。
v160以软件slicing-by-8加速张量专用CRC，仍完整扫描复制后的权重、输入/状态/输出，
保留scratch/padding清零、guard及隔离；新增crclegacy同镜像对照。v159通过镜像保留。
新候选不是实机速度/实时ASR验收，见[v160测试步骤](a733/LOCAL_NPU_KWS_V160_CRC.md)。
以下历史记录原样保留。

## v158 实机通过 / v159 全校验性能优化候选（2026-10-20）

新增用户板测归档：v158 六组固定特征 KWS 共1771次真实NPU调用，关键词、
调用数和全部输出链CRC均匹配；返回后NPU专用池恢复，未见堆持续增长。
见 [v158实际验收记录](evidence/local-voice-v153/openvela-v158-runtime-passed.md)。
原通过镜像、源码包和所有开发历史保留，不覆盖。

v159 优化每次RUN的cache clean范围、resident重建、官方CRC复用和参考索引，
增加临时CPU6/CPU7亲和性、fullclean对照、驱动九阶段及chunk P50/P95/max统计。
LOAD/输入/输出CRC、guard、显式scratch/padding清零、超时隔离和退出恢复均保留。
主机模拟不算实机速度验证，v159仍是待上板的性能候选；不宣称实时麦克风、ASR
或完整NPU语音助手完成。见 [v159说明及全部测试命令](a733/LOCAL_NPU_KWS_V159_OPTIMIZATION.md)
与 [v159交付证据](evidence/local-voice-v153/kws-optimized-v159.md)。下方历史原样保留。

## v157 实机通过 / v158 连续 KWS 候选交付（2026-10-20）

用户提供的 openvela 日志已逐路核验：四模型固定 + 14 动态共 18 次调用、170 路
输出长度/CRC 全匹配，最终 NPU 空闲和堆用量回到测试前值。通过镜像及全部历史保留；
见 [v157 实机归档](evidence/local-voice-v153/openvela-v157-runtime-passed.md)。
v158 已增加显式模型 RAM 快照、实际 encoder 状态回灌及三模型联合 KWS；
六场景真实应用代码的主机文件后端、错误清理、真实目标栈帧检查、2972目标完整构建、
独立镜像及分区/哈希校验通过，148个SDK路径恢复一致。新候选未烧录/未硬件验收。
先使用固定 fbank，不自动录音/上传/联网，不把 KWS 冒充 ASR 或完整语音助手。
见 [v158上板步骤](a733/LOCAL_NPU_KWS_V158.md) 与
[交付证据](evidence/local-voice-v153/resident-kws-v158.md)；以下历史原样保留。

## v156后续板测 / v157 encoder分配修复（2026-10-19）

用户已烧录v156：NSH/selftest通过，joiner、decoder、vocoder在openvela上固定输出CRC
均匹配；encoder为-ENOMEM。确认是旧LeNet保留页后，1MiB专用池只有148页可用，
两路聚合缓冲需149页，并非4GB系统RAM不足。仅实验模式扩为2MiB，保留旧常量、
ABI/句柄和32MiB DMA arena；补实际分配器接真实客户端回归。修复版硬件待测试，见
[v157说明](a733/NPU_TENSOR_POOL_V157.md)。用户已覆盖Linux卡，先前报告本地保留。

## v156 prepared Linux实测与openvela候选（2026-10-19）

四模型 A7TV 导出和 Linux 固定42输出、动态128输出/74处状态回灌通过；
进一步用实际 prepared encoder/decoder/joiner 和同一应用 C 搜索核心完成六场景KWS，
1728次真实NPU调用，三正例/三负例通过，不使用冻结模型输出。
新增公共客户端的 `aipet audio npuref` 入口及独立实验构建；默认产品开关仍关闭。
当前Linux卡未改动/刷写，openvela模型执行、实时麦克风、ASR与完整语音闭环仍待验收。
详见 [v156说明](a733/LOCAL_NPU_PREPARED_V156.md) 和
[实测/构建证据](evidence/local-voice-v153/prepared-v156.md)。下列历史记录全部保留。

## v154 实测与 v155 多张量接口（2026-10-19）

Linux唤醒搜索3正例/3负例回归通过，1303次原生调用及全量数据连接、主机C重放复核通过；
两段NPU声码器语音也经用户听音确认。均不冒充openvela语音硬件验收。
新增默认关闭的64入64出A7TV传输扩展、公共客户端和错误隔离；24组主机/模拟测试及
真实NuttX目标对象编译通过，尚未链接/刷入新镜像。四个固定模型的真实prepare布局
Linux r2回传encoder 39路输出全部匹配，但VM_IO快照读取失败；已修复并生成
`a733-linux-voice-layout-v155-r3.tar.gz`。后续已通过SSH完成四模型Linux实机布局采集，
42个输出哈希及85处native I/O绑定本地复核通过；不是openvela执行通过，见
[r3实机证据](evidence/local-voice-v153/layout-v155-r3-ssh-passed.md)。
[r2核验及r3修复](evidence/local-voice-v153/layout-v155-r2-failure-r3-fix.md) 保留。
ASR NBG与openvela端到端仍未完成，详见
[v155说明](a733/LOCAL_NPU_TENSOR_V155.md)。以下历史过程保留。

## v153 新版Zoo/本地fbank（2026-10-19）

用户提供v1.1.0，Zipformer源码和校准集已取得；ASR三NBG与TTS目录未附带。
后续已下载固定提交的上游三份ONNX、核验哈希并审查36路状态/INT64/校准集；
源图不是已转换NBG，也没有运行NPU语音。
新增真实Kaldi 80维CPU前端、10秒有界流式接口与 `aipet audio features`，
不联网、不保存录音、不冒充ASR。细节见[v153](a733/LOCAL_NPU_VOICE_V153.md)，
构建/实机状态以对应证据为准；以下历史阶段原样保留。
修正iostream兼容后目标构建、布局和163路径SDK恢复通过，静态运行占用较v152
增加32KiB；未生成整卡/未声学板测，见[v153构建证据](evidence/local-voice-v153/build-summary.md)。

## v152 本地NPU语音前置阶段（2026-10-19）

锁定Radxa参考语音NBG/WAV，确认NBG v2.0、KWS 39入39出与当前A7PM限制，
公开Zoo链接仍为缺Zipformer/TTS的v0.9.0。新增真实厂商API的Linux多张量对照工具
及 `aipet audio play22k` 的有界采样率转换；不是全链路NPU推理完成。
恢复点、所需资料、源码/回归/构建状态见[v152说明](a733/LOCAL_NPU_VOICE_V152.md)。
全量2958目标、布局与140路径SDK恢复已通过；没有整卡镜像或语音实机通过声明，
见[构建证据](evidence/local-voice-v152/build-summary.md)。

## v151 I2S应用层/官方语音候选（2026-10-16）

新增 `aipet audio`：本地录音/WAV回放、持久音量/增益/输出选择、隐藏语音凭据向导、
48k→16k FIR后官方云ASR、原Agent路由及官方16k TTS→I2S。默认保留UART，
不后台录音/上传；标准audio/Media、双工/唤醒词和实机云鉴权仍未验收。
主机回归已通过，整机镜像/板测状态以[v151说明](a733/AIPET_AUDIO_V151.md)和后续证据为准。
后续全量2957目标及最终修正1991目标复编、布局/2GiB镜像/138路径恢复已通过，
见 [v151交付与哈希](evidence/i2s-v151/build-summary.md)；新应用/云语音仍待上板。
v150通过镜像及历史开发记录全部保留。

## v150 后续实机：录音文件回放通过（2026-10-16）

用户完成两次默认 ×5 录音与五次 48kHz mono PCM16 回放，50% 音量，
每次约5.07秒、status=0，确认拍手/说话清楚。现有volume=0..100为本次开机内的
数字幅度，默认25%，对后续WAV生效，不改变麦克风增益；不支持播放中实时调节。
16kHz测试文件最初路径报-2，不能计入通过；标准audio/Media/云TTS和长稳仍待完成。
见 [用户实机归档](evidence/I2S_SPEAKER_V150_RUNTIME_ACCEPTANCE.md)。以下候选历史保留。

## v150 I2S扬声器WAV播放候选（2026-10-16）

用户澄清转向扬声器。新增有限PCM16 WAV→RAM→I2S播放、25%默认数字音量、
格式/时长/超时/供数错误检查，保留麦克风默认×5和UART TTS。
主机证明不等于实机发声；源码、边界、候选构建与上板步骤见
[v150扬声器](a733/I2S_SPEAKER_V150.md)。标准audio/Media/云TTS仍未接入。
后续全量2955目标、布局/镜像校验及130路径恢复已通过，
见 [v150交付](evidence/i2s-v150/build-summary.md)；新播放声音仍待上板。

## v149 默认语音增益×5（2026-10-16）

用户实机确认×5合适，省略参数的icsvoice默认值由4改5；显式参数和原始录音接口不变。
本次仅源码/主机默认值回归，未重新生成镜像；现有v148板上仍需显式,5。
见 [默认值变更](a733/ICS43434_V149_DEFAULT_GAIN.md)，不覆盖v148镜像或构建证据。

## v148 左声道语音与增益候选

10月16日恢复后，源码/主机回归、2955目标完整构建和2GiB镜像验证通过；
129个SDK暂存路径及home身份恢复核对通过。新模式尚未上板，不称硬件已通过。
见 [交付/哈希](evidence/i2s-v148/build-summary.md)；此前[暂停记录](evidence/i2s-v148/PAUSED.md)保留。

保留v147通过标签/镜像。新增icsvoice=<ms>[,<gain>]：固定左槽mono S16、默认×4、
整数1..16可调、S16饱和与削顶计数，原始命令不放大、不改路径。
处理位于停RX且门禁通过之后，不改已通过硬件时序。构建/候选交付及后续实机状态
见 [v148](a733/ICS43434_V148_VOICE_GAIN.md)，不把主机测试当成新模式硬件通过。

## v147 首次基础录音通过（2026-10-15，后续实机更新）

用户确认新ICS43434录到声音、音量偏小。单次命令5.6712秒，内部5100ms含warmup，
5秒/48kHz双槽S16 WAV为960044字节，六个帧/字计数核对通过，文件统计逐项匹配串口。
左声道峰值6.68%满幅、无削顶，右声道全零；24MHz原始波形支持约48kHz/3.072MHz、
每帧64个BCLK。短波形未与WAV逐字节匹配，不作bit-exact结论。
本轮仅保存证据/分析工具，没有改固件或增益，没有新镜像；长稳、音质标定、标准音频
框架与ASR仍待开发。[实测报告](evidence/ICS43434_V147_RUNTIME_ACCEPTANCE.md)。
以下候选/失败记录保留为历史交付状态，不代表当前仍未首次收音。

## v147 ICS43434 计数单位候选（2026-10-15）

v146用户上板进入NSH、单次ICS采集5.1067秒，内部FIFO字数489600、RXCNT244801，
旧按字比较返回-EIO且committed=0，没有创建WAV。v147按实机支持的帧单位与FIFO字数
分开核对，增加warmup/每秒中间快照；不是将计数错误忽略。主机模型同步修正并增加
错误倍率/停滞/溢出等负例。手册sample措辞仍有歧义，帧策略是候选，不称最终硅片定论。
逻辑分析仪原文件元数据20kHz，D0=LRCLK/D1=BCLK/D2=SD，无法据此解码3.072MHz总线。
原始文件和日志已归档，新语音/高采样率时序/实机回归待测；见
[v147说明](a733/ICS43434_V147_COUNTER.md)。
v147全量2955目标构建、布局与2GiB候选镜像验证通过，129个官方暂存路径恢复一致；
新镜像仍待上板，哈希与日志见 [构建记录](evidence/i2s-v147/build-summary.md)。

## v146 ICS43434 合并模块开发（2026-10-15）

用户更换曾反接电源的旧麦克风，确认改用 MAX98357＋ICS43434 合并模块。
蓝牙 v145 暂存，待 ESP32 联调后恢复，恢复分支及完整 bundle 已保存。
ICS 新增双槽同时接收、标准24-bit I2S、stereo/left/right/raw 独立文件；严格保留
旧 INMP441 命令和录音失败证据，不把更换硬件推断为根因已解决。
新主机回归、2955目标全量构建和2GiB候选镜像校验通过；129个官方暂存路径恢复一致。
镜像 SHA-256=`fece72b9666b3d3b99c6ec3831e26ae6962fa33404aa13e6db55217903b2eb1f`。
实机照片/新麦克风收音仍待验证；[构建归档](evidence/i2s-v146/build-summary.md)。
接线、电压边界和命令见 [ICS43434 v146](a733/ICS43434_V146.md)。

## v145 SSH实机回归：单板运行/扫描/轻负载共存通过（2026-10-15）

助手经用户授权通过SSH实测：v145 info/frame、非法参数与适配器OFF清理、八核cpucheck、
5秒计时、HCI只读probe、官方BLE五轮扫描/启停及扫描中quit后重进/再扫描通过。
五轮扫描期间电脑→板端47/47 ping返回；Host worker=270不变，error/failures/needs-reboot=0。
无GATT外设，正向Read/Write/Notify/业务ACK和双板仍未测；不称长稳或全链路完成。
默认SSH协商会话曾报Bad packet length/Connection corrupted；显式AES128-GCM会话完成
回归并正常退出，仅为临时客户端选择，默认会话损坏及异常退出子进程回收仍待定位。
报告含成功与失败原始日志：[v145实机回归](evidence/BLE_V145_SSH_RUNTIME_ACCEPTANCE.md)。
没有改固件、重构底层、修改防火墙或覆写恢复镜像。以下为交付时历史记录。

## v145 官方 GATT 客户端开发（2026-10-15）

用户要求直接推进，在 v144 五轮通过基线上新增 `home info/frame/probe/monitor`。
复用官方 openvela GATT Client/外部 UV 回调，接入既有共享协议，验证 Read/Notify、
PING/GET_STATE 的 ATT 响应与业务 ACK；同地址最多三次重连，不开放物理输出或 Agent 控制。
主机模拟/ASan/UBSan、完整 ARM64 构建、布局与 2 GiB 镜像回读校验通过；
129个暂存路径和 home目录身份已恢复核对。镜像 `openvela-a733-cubie-a7z-sd-ble-v145-gatt-client-candidate.img`，
SHA-256 `d66bc9afdf8ecc690fca77b8d42b2f1b177a40cfbdeab9c2c1be893a57f40d32`。
交付时尚待本版启动、真实GATT外设和双板验收；后续单板运行/轻负载共存实测见上节，
真实GATT和双板仍未通过，不称硬件全链路完成。
详见 [v145 说明](BLE_GATT_CLIENT_V145.md)。保留 v144 实机恢复点，不覆盖旧镜像。

## v144 BLE 五轮生命周期实机通过（2026-10-15）

启动进入 NSH；手动启动 kvdbd/bluetoothd 后，连续五轮 enable/真实扫描/stop/disable
回到 OFF(0)/quit/重进全部通过，共 664 条 ScanResult，未复现此前后续轮次卡状态 6。
恢复标签 `a733-ble-v144-lifecycle-5cycle-pass`；保留原镜像和全部过程文档，不重新构建。
此次未测 Wi-Fi/ping 共存、扫描中直接退出、长时间压力、ACL/GATT 与 ESP32-S3 双板。
详见 [五轮实机证据](evidence/BLE_V144_FIVE_CYCLE_HARDWARE_PASS.md)。以下为历史记录。

## v143 重复启停失败；v144 Host 资源回收候选（2026-10-14）

v143 前两轮真实扫描/关闭/OFF 通过，后续重开不再收到广播，关闭长期状态 6。
本轮保持下位机/GATT 暂停，修复扫描锁重置和工作对象回收，加入有限等待、SAL/HCI
阶段诊断；不把队列接受当硬件完成。原始失败日志、恢复点、构建和上板步骤见
[v144 记录](evidence/BLE_V144_REOPEN_CANDIDATE.md)。保留以下历史状态记录。
v144 回归/最终源码匹配、构建、布局与 2 GiB 镜像校验通过，119 个暂存路径已恢复。
镜像 `openvela-a733-cubie-a7z-sd-ble-v144-reopen-candidate.img`，待实机五轮启停/重进和
Wi-Fi 共存测试；尚未 boot-passed，不称蓝牙稳定通过。

## v142 扫描实机通过；v143 生命周期候选已交付（2026-10-14）

v142 已有官方 BLE_ON、status=0、真实 ScanResult 和 KVDB 写入/读回证据；
随后关闭/退出重进失败：BleOn 未处理 safe-off、重复 enable 误入 Classic，
工具遗漏 BLE_ON 初始化导致清理缺失。不能把扫描通过扩展为 GATT/长稳通过。
源码审计还修正 SAL 提前报告 OFF 的时序，v143 源码/ASan/UBSan 回归通过，
完整构建、2 GiB 新镜像、布局/GPT/ext4/内嵌 hash 检查通过，115 个本轮官方
路径恢复核对通过，待上板。镜像为
`openvela-a733-cubie-a7z-sd-ble-v143-lifecycle-candidate.img`。详细记录与恢复点见
[v143 生命周期修复](evidence/BLE_V143_LIFECYCLE_FIX.md)。I2S/ESP 下位机仍暂停。

## v142 官方 BLE Host 修复候选（2026-10-14）

v141 HCI query 已通过，但 Host enable 返回 -5，停在 BleTurningOn，
官方 KVDB 的 /data/persist.db 打开失败，扫描没有通过。
v142 修复 bttool 无回显/输入边界/结果提示、LE 失败状态及 FAT filepath ioctl，
增加不含密钥的 HCI opcode/status 诊断，关闭可选 Controller→Host ACL 流控作对照。
见 `docs/evidence/BLE_V142_HOST_FIX_CANDIDATE.md` 与 `docs/BLE_OFFICIAL_HOST_TEST.md`。
保留恢复标签 `a733-v141-boot-smp-pass`，不覆盖旧镜像。
源码/主机测试不等于蓝牙扫描/ACL/GATT 实机通过；下位机与 I2S 仍暂停。
v142 最终 kernel、2 GiB 镜像及内嵌 hash/GPT/ext4 校验通过，本轮官方路径恢复
核对通过。镜像名 `openvela-a733-cubie-a7z-sd-ble-v142-cli-hostfix-candidate.img`；
hash、构建日志与上板门见上述证据文件。后续扫描通过、生命周期失败的实测更新
见本页顶部及 v143 记录；保留本节原始候选阶段说明。

## v141 实机基础门通过

用户日志已确认 v141 进入 NSH、8 核逐核执行检查通过（6×A55 + 2×A76）、sleep 5 为 5.0009 sec、Wi-Fi 双频扫描正常、官方 H4 注册返回 0。本轮 ifconfig 仍为 0.0.0.0，未测试 WPA2/DHCP/ping；BLE Host enable、扫描、ACL/GATT 与长稳仍待验收。v141 作为启动/SMP 恢复版本保留，可恢复官方 BLE 分层测试；详见 [实机证据](evidence/BLE_V141_BOOT_HARDWARE_PASS.md)。

## v140 实机启动回归失败

用户反馈 CPU0 同 EL Data Abort、异常向量保存 FPU 时再故障，未到 NSH。v140 的编译/镜像校验仍是事实，但 **boot-failed，不再推荐烧录**。v141 修复 RO 区页对齐、Zblue orphan 与 Image runtime footprint 后，clean build、布局/镜像校验及本轮启动/SMP/扫描回归通过；原因边界及新镜像信息见 [启动故障分析](evidence/BLE_V140_BOOT_FAILURE_V141_LAYOUT.md)。不据此宣称排他定位了第一次异常根因。

## 2026-10-13 BLE 进展

v139 三次 USB HCI 查询通过，身份/地址/LE features 可读；Wi-Fi WPA2/DHCP 通过，网关 ping 3/4 收到，不能据此宣称共存长稳。证据见 [BLE_V139_HARDWARE_PASS.md](evidence/BLE_V139_HARDWARE_PASS.md)。
v140 已接入官方 BTH4、Zblue、bluetoothd、bttool 和基本 GATT Client；编译/链接、2 GiB 候选镜像、GPT/ext4/嵌入内核校验通过，尚无 v140 实机启动/扫描/ACL/GATT 通过证据。构建记录见 [BLE_V140_BUILD_AND_TEST.md](evidence/BLE_V140_BUILD_AND_TEST.md)，分层验收见 [BLE_OFFICIAL_HOST_TEST.md](BLE_OFFICIAL_HOST_TEST.md)。v139 恢复镜像和 backup-before-ble-v140 分支保留；I2S 仍暂停。

## 2026-10-11 开发方向更新

I2S 麦克风暂停：v137 右声道无声、左声道电流声，未通过真实录音验收；历史代码和镜像保留。
转向 A7Z BLE 主控与 openvela ESP32-S3 N16R8 下位机通信。新增手动 `/dev/a733-bt` HCI 查询诊断；应用报文 codec 主机内存安全测试通过。BLE Host/GATT/双板通信仍待实现和实机验证。完整方案及测试步骤见 [BLE_SMART_HOME_PLAN.md](BLE_SMART_HOME_PLAN.md)。

状态日期：2026-09-20。“已通过”表示已在真实的 4 GiB Cubie A7Z 上观察到，
而不只是完成编译。

## 已验证的系统基线

- Boot0/SCP/BL31/U-Boot 通过 `booti` 将控制权以 EL1 交给 openvela。
- AArch64 运行时、MMU、异常、GICv3 和虚拟定时器正常。
- UART0 提供早期输出，并在 115200 8N1 下提供可交互的 NSH 控制台。
- 4 GiB DRAM 通过分区堆区域暴露，同时保留固件预留区域。
- procfs、`free`、`ps`、`uptime`、`dmesg`、信号和 Ctrl+C 正常。
- `poweroff` 和 `reboot` 使用开发板/PSCI 路径。
- 8 核 SMP 已识别 6× Cortex-A55 与 2× Cortex-A76；NSH、Wi-Fi、NPU 与本地
  LLM 回归通过。启动期多核串口字符交错属于当前诊断输出限制。

## 已验证的存储与外设

- SDMMC0：PIO，400 kHz 初始化，12 MHz 传输，4-bit 总线。
- GPT 分区：firmware、EFI、openvela 和 data。
- FAT 数据分区以读写方式挂载到 `/data`，支持 UTF-8 长文件名。
- 用户 LED、watchdog、THS 温度传感器、I2C2/I2C7、SPI1 和风扇 PWM。
- 测试板没有 UFS，并且 UFS 缺失被设计为非阻塞状态。

## 已验证的 FCU760K Wi-Fi 与服务

- 模组：FCU760K / AIC8800D80-U02，通过专用 USB1 高速链路连接。
- BootROM 标识 `a69c:8d80`，运行时标识 `a69c:8d81`。
- 固件上传、补丁配置、重新枚举、LMAC/RF/ME/法规域设置和 station VIF。
- 2.4 GHz 与 5 GHz 主动扫描、WPA2-PSK/CCMP、DHCP、ARP、DNS、TCP/UDP。
- 原生 `wifi` 命令、记忆网络重连和可选的开机自启动。
- SSH/SCP、FTP、curl/wget/HTTPS、NTP 和 iperf。
- 板载 TRNG 为 `/dev/random` 和 `/dev/urandom` 提供 TLS/SSH 随机数。
- 观测到的吞吐基线：TCP 4.13 Mbit/s；UDP 10.8 Mbit/s，单次 30 秒测试丢包
  5.7%。这只是功能基线，不是峰值性能声明。

## openvela 公共 Wi-Fi 控制面

- `wlan0` 已实现 WEXT 扫描、扫描结果、模式、认证、密码、频率、BSSID、ESSID、
  国家码和基本状态 IOCTL；
- 已启用并成功编译 openvela `apps/wireless/wapi` 库及 `wapi` 命令；
- `wifi scan/list/connect/disconnect` 已从私有字符设备写命令迁移到 WAPI；
- 密码仍由 `wifi connect` 隐藏式读取，WEXT 查询不会回显密码；
- 扫描缓存从 16 扩展到 64，避免密集 2.4 GHz 环境挤掉 5 GHz 结果；
- 私有 `/dev/a733-wifi` 当前仅作为详细诊断和旧接口兼容，真机验证后再默认关闭
  写控制接口。

v69 真机回归确认 WAPI 在 2.4/5 GHz 均能扫描并完成关联，但在 ZBCK-E 多 BSSID
AP 上，AP 持续重传 EAPOL Message 3，最终以 reason 15 断开。日志同时证明 M3
MIC、PTK/GTK 派生和 USB TX 提交均成功，因此问题定位为 Message 4 发送时序：
旧实现先向 FCU760K 安装 PTK，再发送 M4，使 M4 被数据路径提前加密；严格 AP
不会接受该帧。

v70 候选实现已经按标准 supplicant 顺序修正为：校验/解密 M3 → 发送 M4 → 安装
PTK → 安装 GTK → 开放 controlled port → DHCP；并在异步 disconnect indication
到达时同步清除 carrier、密钥安装标志和 WPA 状态，避免 reason 15 后仍显示已连接。
该版本已完成 AArch64 编译、链接和 SD 镜像文件系统校验，等待真机验证后再列入
“已验证”基线。

详细边界与验收命令见 `docs/OPENVELA_COMPONENT_BOUNDARIES.md`。

## 已验证的官方 Agent 与文字桌宠

- 使用 openvela 官方 `packages_ai_agent` 的消息总线、上下文、LLM proxy、TLS、
  Skills 和工具注册机制；产品层 `aipet` 负责桌宠路由和表现接口。
- `aipet init` 支持 DeepSeek/MiMo、默认或自定义模型、隐藏输入 API Key，并使用
  板级密钥加密持久化；配置成功后 Agent 开机自启动。
- 实机连续中文对话通过：首次 TLS 请求约 7.5 秒，连接复用后约 1.8 秒。
- 快速规则、云端优先、失败转本地接口、UTF-8 校验和回复动作/表情白名单已实现。
- 本地 LLM 已通过线程安全库接口注册到产品路由，支持强制本地请求，以及断网或
  云端失败后的真实 Qwen 回退；屏幕、舵机、电机和 LED 仍只保留安全接口，
  不宣称硬件动作已经执行。

## 已验证的本地 Qwen2.5-1.5B 基线

- 1.1 GB Q4_K_M GGUF 的元数据、tokenizer、embedding、Q/K/V、RoPE、GQA、
  28 层 Transformer、LM head、KV cache 和连续生成均已在板端逐阶段验证。
- 模型可常驻 4 GiB 内存；6 核工作池默认使用 CPU2..7（4×A55 + 2×A76），
  保留 CPU0..1 给系统。缓存命中后单 token 前向检查点约 0.6 秒。
- 中文流式输出和有限多轮历史已通过现场测试；桌宠 `LocalRouteBackend` 已接入。
  独立参考精度、长稳、跨请求持久 KV、采样策略和强制中断清理仍待完成。

## ST7735 与 LVGL 显示（v126 实机通过）

- 复用 openvela 官方 LVGL；A733 板级代码负责 SPI1、PL5 D/C、PB0 RESET、
  ST7735 初始化和直接 8-bit 写区路径，规避通用 16-bit SPI word 的端序歧义。
- `display status/raw8/test/run` 已进入 v126，RGB565 高字节优先、SPI Mode 0、
  12 MHz，目标面板为 128×160 ST7735。
- v122 修复 CCU 基址、写操作 RX 丢弃、transfer-complete/error/timeout 判定，
  加入 Linux 已验证的面板初始化序列及绕过 LVGL 的 raw 色条分层测试。
- v122 实机 raw/LVGL 均卡死且黑屏；v123 修正片选并快速定位为 TX FIFO
  overflow；v124 改用 8 位 FIFO MMIO，但实体屏仍黑且 LVGL 在 `mm_forcefree`
  崩溃；v125 修正 A733 type-4 PIO 地址和 PB0 RESET；v126 恢复低有效 CS，强制
  `COLMOD=0x05`，并显式发送高字节优先 RGB565。
- 2026-09-27 实机确认：`display status` ready；`display raw8` 两次自动返回且白框、
  RGB 三色块正确；`display test 30` 显示完整 LVGL 桌宠界面并自动返回 NSH。
- 功能等级为 hardware-passed；长时间动画刷新和性能统计尚未达到 stress-passed。
- 详细接线、哈希、日志和照片见 `docs/ST7735_LVGL_PORT.md` 与
  `docs/evidence/st7735-v126/`。
- v127 保持 v126 底层不变，新增正式 `display pet/demo/emotion` 产品入口、七类
  程序化表情、普通态眨眼、10 秒回闲和 Agent 非阻塞 POSIX 消息队列。完整构建、
  最终链接、镜像封装及反向校验已通过。实机确认首帧和事件接收，但画面保持普通
  表情；根因为自管 LVGL 循环没有推进毫秒 tick。v128 仅补齐 `lv_tick_inc(delay)`。
  实机已确认七种表情连续切换、手动情绪事件和 AIPET 请求/回复联动全部生效，显示
  产品链路提升为 hardware-passed；数小时压力测试仍待完成，详见
  `docs/LVGL_PET_UI.md`。
- v129 在不改动上述已通过路径的前提下新增 `display fps [seconds]`，按照实际完成的
  LVGL 全屏 flush 统计帧率、像素和有效 RGB565 吞吐。全量构建、独立镜像和板端
  10/30 秒测试均通过：`29.564/29.563 fps`、`1182 KiB/s`，约为 12 MHz 纯像素理论
  上限的 80.7%；表情回归和内存检查正常。v128 继续作为不可覆盖的恢复基线。
- v130 源码实现了文件输入闭环，但上板发现构建包装器没有暂存 `apps/system/a733npu`
  三个文件，最终镜像只有新 `display vision`、没有新 `npu yolo-ui`。普通 NPU dog
  三框和 black 零框推理均通过，证明故障局限在打包边界。v130 保留为失败证据。
- v131 修复暂存清单并从 v129 重新封装。最终 BIN/ELF 已确认同时包含
  `npu yolo-ui` 与 `display vision` 标志；全量构建和镜像结构验证通过。32 字节消息、
  队列深度 8、最多七框及非阻塞语义保持不变。实机 dog 输入得到三框并显示，black
  输入得到零框并清除上一帧，文件输入联动已为 hardware-passed；UVC 实时链路仍未
  完成。

## 音频与语音状态

- UART4 `/dev/ttyS4`、PJ24/PJ25、9600 8N1 和 TW-TTS UTF-8 帧已完成。
  v115 实机暴露的 termios `B9600`/数值波特率混用导致 `-22`，已在 v116
  修复。v116 实机已进入参数帧发送阶段，但 UART FIFO 状态始终为零并在首字节
  超时（`stage=parameters/-110`）。v117 按官方 sun60iw2 CCU 定义显式选择
  24 MHz APB-UART 父时钟，并以“拉复位、开门控、释放复位”顺序初始化，新增
  `/dev/a733-uart4` 寄存器诊断。v117 实机诊断发现 CCU、PIO、UART 全部为零，
  由此确认旧实现仍使用了错误的 CCU 基址和传统 pinctrl 布局。v118 已改为
  官方 DTS 的 CCU `0x02002000`，并采用 sun60iw2 HW type 4 的 `0x80` 首 bank、
  `0x80` bank 步长、`0x20/0x30` 驱动/上下拉偏移。v118 实机确认
  `bgr=00010001`、PJ24/PJ25 function 4、`LSR=0x60`、`USR=0x06`；TW-TTS
  初始化三帧和文本帧全部发送成功，`frames=4 failures=0 stage=complete`，随后
  重复播报继续成功。UART4 TTS 基础链路已通过。
- I2S0 的 MAX98357A/INMP441 引脚与时钟资源已确认。v123 实机证明 TX/RX
  counter 会前进，但扬声器无声且采样全零；v124 修正低声道 MAP1 后仍未取得
  外部效果。v125 进一步确认并修正主 PIO 旧布局，使 PB5..PB8 实际配置为
  function 3。v122 起提供的
  `/dev/a733-audio` 在启动时保持被动，可显式输出测试音并采集
  `/data/inmp441.wav`；所有轮询有超时保护。v125 主动路径尚待实机验收，
  v132 已对齐厂商 Linux 的 16-bit TX FIFO 语义，保留 `tone32=` 对照，并为
  INMP441 加入 24-bit 采样、100 ms warmup、`capture32=` 原始 FIFO 抓取与
  PB5..PB8 mux 回读。v133 已实机确认 MAX98357A 的正式 S16 `tone=` 与 32 位对照
  均能发声，播放硬件链路为 hardware-passed。
- INMP441 的 v133～v135 文件大小与非零计数曾被误当成有效采集，实际三场景均为
  电流声。离线取证确认约 72% 样本为重复旧值，5 秒文件实际只有约 1.36 秒硬件
  更新；根因是把 `FIFOSTA` 固定 bit7 当成 RX 计数，使空 FIFO 被高速重复读取。
  v136 现按 bit8 `RXA` 节拍、SR24/SW32/RXOM1、四组 RX map、PB8 下拉和 RAM
  采集后经临时文件完整写入再替换实现，并用墙钟/RXCNT/溢出三重拒绝伪成功。最终代码与镜像构建
  通过后仍须三场景实机试听，才可把麦克风升级为 hardware-passed。
- 当前 I2S 是有界 polling 验收实现；通过后仍需封装标准 openvela/NuttX
  I2S/audio lower-half 与 DMA，不能把验收节点直接称为生产级 Media 设备。
- v136 实机证明采集持续 `5100 ms`、硬件 RX 计数与 `48 kHz` 目标一致，彻底排除
  “CPU 重读空 FIFO”问题；但静音、语音、拍手三份 PCM 均接近满幅电流声，原始字节
  主要为 `0x55/0xaa/0x66/0x99`。这符合 INMP441 未选半帧三态后 PB8 被 BCLK/WS
  串扰的特征，不是声学信号。v137 将默认输入改为 slot 1，并增加
  `captureleft=` / `captureright=`。最新用户实机结果为 right 完全无声、left 仍为
  电流声；left 持续 5100 ms、消费 RX=244800、硬件 RX=244801。映射切换有效，
  但录音内容仍未通过，不能继续把问题归为录制时长或错误半帧。
- v138 增加 `capturecfg=<slot>,<bclk>,<pull>,<ms>` 八组对照，LRCK 固定正常，
  BCLK=0/1、PB8 pull=0/2，每组独立文件。记录配置快照、S16 平方和、零值、
  符号跳变及原始低字节 55/aa 计数，保留 FIFO 和文件提交检查。
  全量构建 2730/2730 targets、主机检查、ext4/GPT/内核回读和官方源码恢复核验
  已通过。随后八组实机全部完成，WAV 数值与日志吻合，但 left 四组 RMS 仍为
  21152..21159，right 四组均为恒定 -11437（AC RMS=0），录音为 audio-content-failed。
  right 恒值不能证明右半帧有效接收，RX_SLOT_NUM=0 与 slot 1 映射的配套行为
  仍有待核验。下一步先检查 PB5/PB6/PB8 和模块 SD 实际波形及接线/供电/CHIPEN，
  获得物理数据后再定向审计 RX 配置，不继续只改 PCM 位移。
  命令与证据见 `docs/evidence/i2s-v138/build-summary.md`。
  本轮文件哈希、精确统计和判断边界见 `docs/evidence/i2s-v138/audio-analysis.md`。
- 官方云端 ASR 接口和本地 ASR 骨架已经调研/保留，但没有真实麦克风 PCM 输入，
  因此当前桌宠语音输入链路未完成。

## 已验证的 VIP2 NPU

- 电源/时钟/复位、IRQ、DMA 内存池、MMU 页表、缓存维护和同步任务 ABI。
- 硬件 CID `0x1000003b`；运行时设备 `/dev/npu0`。
- LeNet 输出 CRC32 `d50f5d79`，字节前缀
  `003c000000000000000000000000000000000000`，与 Linux VIPLite 黄金结果一致。
- YOLOv5s 三输出黄金回归通过。
- YOLOv8n PCQ 六输出黄金回归和动态 640×640 RGB 输入通过。dog 输入有检测结果，
  black 输入无检测结果，证明结果来自给定输入而不是回放固定输出。
- 当前交换路径为：获得授权的 Linux VIPLite prepare/trace → 已核对的 A7PM 包 →
  openvela 执行器。openvela 尚未实现通用专有 NBG 链接。

### 已编译、等待开发板回归的 NPU ABI v4

- 新增 `A733_NPUIOC_RUN_A7PM`，把 A7PM 包、可选动态输入、NPU 执行、输出 CRC
  和 YOLOv8 检测结果合并为一次原子调用。
- 新增原生 `npu` 命令，支持 `info`、`selftest`、`report`、通用 `run` 和便捷
  `yolo` 子命令。后续 IMX214 主线或保留的 UVC 输入都直接使用相同 ABI，而不再
  组合多个 `echo`。
- ABI 使用整数化的千分比分数和百分之一像素坐标，避免向应用暴露内核浮点 ABI。
- 该增量已经完成 AArch64 全量编译与链接；由于本段尚未刷入开发板，所以不把
  ABI v4 的板端行为列入上面的“已通过”清单。

## 摄像头主线与保留的 USB/UAC/UVC

摄像头主线已固定为 Radxa Camera 13M 214（Sony IMX214）：复用 A733 官方 Linux
BSP 的 sensor/VIN/CSI/ISP 资料，按电源/MCLK/reset/I2C ID、RAW10 CSI、VIN DMA、
ISP、`/dev/video0` 顺序移植。现阶段只保存设计，不宣称 MIPI 驱动已完成。

v113 是最后一个取得实机日志的诊断版本，仍未观察到端口连接。v114 候选加入
Combo PHY USB 表、host/UTMI 设置和最小 xHCI ring，但只有构建证据，没有实机
日志。USB 麦克风和摄像头均尚未枚举。完整边界见
`docs/a733/USB_V114_CANDIDATE.md`。

下一实现检查点：

1. 保留当前可用的固件交接状态；
2. 不再复位 DWC3 或 xHCI；
3. 建立 DMA32 DCBAA、命令环、事件环和中断器状态；
4. 启动控制器并发出 Enable Slot / Address Device；
5. 枚举描述符，实现 UVC PROBE/COMMIT 并采集帧；
6. 转换/缩放为 RGB640，送入现有 YOLOv8 动态输入并输出框、类别和分数。

## 尚未完成

- Bluetooth HCI 和协议配置文件；
- Imagination BXM GPU 运行时；
- UFS 主机/存储和 UFS 优先启动；
- MIPI CSI IMX214 驱动；
- SMP 已基本运行；DVFS、热管理和生产级多核长稳仍待加固；
- openvela 内通用 NBG 编译器/链接器。

这些项目有意不计入已完成特性声明。
