# Dogking A733／小派 A7Z 项目完整总结

> **2026-11-04 最终提交收尾**：作者指定 v229 为最终版本，停止 dslocal/v230。已将草稿的10个跟踪文件差异及7个新增文件备份到仓库外归档，工作源码恢复 v229，不生成 v230 镜像。最终入口：[提交说明](FINAL_V229_UPLOAD.md)、[版本指针](FINAL_RELEASE.json)。v229 固件与原镜像不变，保留 v219 稳定回退；44项软件门禁及本机TLS测试不能替代新版真实云多轮板测。三张16:9亮点生图单独交付，见 [图片文案与证据](presentations/HIGHLIGHT_IMAGES_20261104.md)。本轮不推送或创建PR。

> v229已封装：[发布及恢复证据](evidence/product-v30prefix2-v229-image/README.md)。独立2GiB镜像SHA256 `5d8ae2fe97176b05a411f0e38a5ff5db5e0a6c519a47f07207a35f2d53e400f1`；44项继承/新增软件门禁、完整ARM/ABI/ISA/摄像头审计、SDK320项恢复、镜像回读和Windows哈希通过。同进程本机TLS50轮/150连接零FD增长，不能替代真实讯飞板测；9个旧镜像保留、v219仍稳定，无PR/推送。新增 [五分钟拍摄稿](presentations/VIDEO_SCRIPT_5MIN_20261104.md) 与 [分模块完整演示命令](presentations/VIDEO_DEMO_COMMANDS_20261104.md)，修正旧PPT中cloud回退及步数×10的历史口径。

> 2026-11-04 连接修复候选：[v3.0pre-fix2/v229](PRODUCT_V3_0PRE_FIX2_V229.md)。v228首轮成功、后续在HTTP升级前失败；现场UDP为空、IOB充足，不能断言资源耗尽或CPU调度。修正无效fd零状态、保留SO_ERROR及真实TLS阶段、TCP+TLS最多4地址有界重选、WS/TLS有界关闭，并修正SDK截断DNS由UDP转TCP时漏关旧fd的独立缺陷。真实主机TLS同进程50轮/150连接零fd增长；DNS原版泄漏可控复现、修正版200次回退无泄漏。模拟测试不证明板故障根因。保持CLOUD不降级、本地/NPU/I2S/摄像头及全局调度不改；新镜像封存及板测状态以专文和发布证据为准，v219仍稳定。

> v228 已封装：独立2GiB镜像 SHA256 `fce43893a2cfe0b40160dbf83017fa17b77c0bd179dc4f0793c183399332e09c`，42项继承/新增软件门禁、完整链接审计、SDK319项还原、镜像回读和Windows独立哈希通过。[发布证据及仅内核保留配置升级](evidence/product-v30prefix-v228-image/README.md)。CLOUD严格云端，AUTO才允许降级；旧八个镜像保留，v219仍稳定，等待新版实机验证，无PR/推送。

> 2026-11-04 路由修复候选：[v3.0pre-fix／v228](PRODUCT_V3_0PRE_FIX_V228.md)。用户确认 v227 换至 ZBCK-E-5G_Wi-Fi5 后讯飞 TTS 成功；随后发现 `mode cloud` 的常驻服务仍加载本地模型，独立识图却能云播报。已定位 v226 常驻代码把 CLOUD 转成 AUTO、并允许云 ASR 失败后送本地识别。v228 保留 CLOUD 为严格云端，忽略 AUTO 冷却；网络/配置未就绪时可取消等待，不加载本地 ASR/LLM/TTS。仅显式 AUTO 保留降级。版本及软件验证不等于真实多轮板测，最终镜像/哈希以新版本封存证据为准；旧版记录中的 cloud-preferred 政策是历史，不是 v228 的现行行为。稳定版仍为 v219。

> 2026-11-03 新开发候选：[v3.0pre／v227](PRODUCT_V3_0PRE_V227.md)。[v226用户反馈](evidence/voice-v226-xfyun-20261103/README.md)确认讯飞 ASR 成功、TTS 三次超时，不能把旧版标成云整机通过。本轮加入 WS 小写入合并、TCP_NODELAY、服务/空闲预算与阶段日志、DeepSeek flash 显式关闭思考、双槽完整句 TTS 预取、云端情绪人设和显式开启的一轮一个有界 ACTION。UI/语音/工具统一桌宠步×20，原始脉冲使用 move-raw，单包上限不扩大。本地模型数学、NPU/摄像头和全局调度不改；旧制品、v219稳定指针不动。最终镜像与新软件证据以专文封存清单为准；本轮没有新板上云账号/机械/首音时延验收。

> v227已封装独立2GiB镜像，SHA256 `e4348914e72bcf282f42755ee3ca5e0d48985cfb62eceeab165d98827673ce64`，固件冻结 `91ba41760d8a10c2dfa6409d652cc19a5fd29d00`。41门禁、最终完整审计、模型/启动文件回读和Windows独立哈希通过，SDK最终319项还原；[封存与失败纠正记录](evidence/product-v30pre-v227-image/README.md)。恢复标签 `a733-v3.0pre-v227-image-20261103`，旧七个镜像不变；讯飞实际发声仍待新版板测，不提前晋升稳定，无PR/推送。

> 2026-11-03 联网恢复交付：[v2.1pre／v226](PRODUCT_V2_1PRE_V226.md)，独立2GiB镜像SHA256 `63aa91f38a6223e0b3ae0d549f3be17020916824aa5273c15906697188799683`，固件源码 `935a7b10c301f52b00f190d518751576def8583c`。[v225完整板测反馈](evidence/voice-v225-cloud-board-20261103/README.md)证明云ASR／LLM／TTS能成功，但ASR错误及云冷却分支会结束cloud常驻，auto则反复切换／冷加载。本版增加最多3次恢复、120秒本地保持、分发前ASR及未播放TTS句段的有限重试、WSS分段诊断和受控Wi-Fi恢复。常驻cloud改为联网优先并允许本地回退；local仍严格离线，独立文本强制cloud仍明确报错。40项软件门禁、完整ARM链接／ELF／ABI／ISA／摄像头审计、镜像回读及Windows独立哈希通过，SDK316项还原；[封存证据](evidence/product-v21pre-v226-image/README.md)。新云账号／AP／声学长稳仍待实机，不晋升稳定，v1.1/v219及六个旧镜像保留；无PR或推送。

> 2026-11-03封装完成：[v2.0pre／v225 交付与完整测试](PRODUCT_V2_0PRE_V225.md)。2GiB独立镜像 SHA256 `cd4cd64097720b7da15abef1ee7f1e8291c258888a8683590a8c537998ddc51c`；固件源码 `99f56842a67b68cf66b19233c4fc3a00c5eb7dec`。36项软件门禁、完整编译／ELF／ABI／ISA／摄像头检查和模型／启动文件回读通过，Windows/Linux哈希一致，SDK316项按字节还原。原日志漏路由句“前面有个什么物体”已补回归、修正并重编译。恢复标签 `a733-v2.0pre-v225-image-20261103`，[封存证据](evidence/product-v20pre-v225-image/README.md)。本次没有新机械／声学／云账号板测；v1.1/v219仍为稳定版，旧镜像不覆盖，不提交PR或推送。

> 2026-11-03开发预览：[v2.0pre／v225](PRODUCT_V2_0PRE_V225.md)。根据 v224 板测修正识图／运动口语路由，明确语音步数×10、300Hz 默认档；常驻会话不再要求反复 arm，故障／失联／未知 ACK 仍停止且不重发。增加保存的音量／静音／增益设置，以及显式启用、默认关闭的开机桌宠与模型就绪欢迎语。NPU／CPU全局调度与模型不变，v219稳定和v224原制品保留；本次新软件与镜像验收结果见专文，不把旧板测当作新版通过。

> 2026-11-02后续板测与修复：[v223原始反馈](evidence/voice-v223-board-20261102/README.md)证明独立ASR、长句TTS、LLM预加载通过，常驻“你好”“前进三步”却反复limited=1、尚未进入LLM/TTS。已封装独立 [v1.3fix2／v224镜像](PRODUCT_V1_3FIX2_V224.md)：80ms连续活动确认后才重置700ms静音；拒绝轮在采集及consumer健康结束后纯CPU丢弃stream，不做最终NPU解码，下一轮原有decoder初始化与循环状态清零保留。33项软件门禁、真实Linux张量捕获重放、8单元ARM编译、最终ELF/ABI/ISA/摄像头及镜像回读通过，Windows/Linux哈希一致。最后第三轮卡死没有足够现场证明根因，新镜像待实测；旧版本全部保留，v1.1仍稳定，不放宽运动授权或超长保护。

> 2026-11-02最新候选：[v1.3fix／v223交付与完整测试命令](PRODUCT_V1_3FIX_V223.md)。已将下述v221fix1语音修复完整编译进独立2GiB镜像，31项软件门禁、ELF／NPU ABI／ISA／摄像头审计及镜像模型／启动文件回读通过，SDK316项还原，Windows/Linux SHA一致。流式TTS总量60秒、旧缓冲接口10秒；按完整词条24音素分段；端点同强度继续判定并保留超长动作保护。原v219/v220/v221镜像保留、v1.1仍稳定；尚未新板测，英文仍为中文读音字母拼读，MiMo新方案未加入，3～5秒实际首音未验收。以下“源码候选／未封装”是保留的过程时间点，不是当前制品状态。

> 2026-11-02 语音后续：新增 [v221fix1 源码修复候选](VOICE_V221FIX1.md)。板测常驻ASR两次status=0但limited=1，被8秒保护丢弃；旧新门控同输入回归复现，已修改端点继续门限。TTS修正流式仍受10秒总量限制和声学单段过长；保留旧缓冲10秒、新流式60秒及错误／取消边界。Baker仍只能英文字母中文拼读，不是自然英文。软件／真实CPU声学／ARM编译已验证，未生成新镜像或新板测，不与已交付v221混淆；MiMo v222方案仍等待计费／使用范围确认。

> 2026-11-02 最新制品：[v1.2fix／v221镜像交付](PRODUCT_V1_2FIX_V221_IMAGE.md)。独立2GiB镜像、28项软件门禁、完整ELF／NPU ABI／ISA／摄像头软件审计与模型／启动文件回读通过；SDK316项字节还原，旧稳定与v220镜像不变。最新稳定仍v1.1，v221未板测。新回传v220日志确认UART连通，但普通长回复流式-110尚待独立定位；本版修正“电机状态”误入LLM，不宣称解决所有长回复或达到3～5秒首音。

> 2026-11-01 STM32 对接更新：**v1.2fix／v221** 固定对照下位机 `efdb437948eda62cbc782692eb8fd291e5d914ae`，增加配置读取、电机／链路诊断与 Agent 精确查询、带年龄 TOF 缓存、故障／配置事件保护、显式确认配置写入和可选下位机 TW-TTS。保持115200、常驻250ms心跳、无自动 arm／清故障／运动重发，以及默认 I2S 语音。完整细节、接线、安全边界、测试与构建见 [v221 对接记录](PRODUCT_V1_2FIX_V221.md)。这是应用源码预览修订，不是新稳定版；旧镜像不包含新命令，本轮无新整卡镜像或联合机械验收。下方旧 MCU 功能描述保留为历史，新增能力以 v221 专文为准。

> 2026-11-01 最新状态：**v1.1／v219 晋升最新稳定版**，用户板测确认正常。原镜像、Git 标签、源代码恢复包全部保留；[稳定版指针](STABLE_RELEASE.json)、[板测晋升记录](PRODUCT_V1_1_V219.md)。之后独立开发 v1.2 低延迟预览，计划默认关闭聊天历史而保留人设、模型常驻和精确前缀 KV。下方旧候选状态及过程文档作为历史保留，不能与最新稳定状态混读；尚未测试的云账号、机械动作或 3～5 秒首音不因此获得验收。

> 开发主线：[v1.2／v220 预览](PRODUCT_V1_2_V220_PREVIEW.md)已实现本地历史默认 0、旧配置隔离、不冷清 KV 的上下文切换、CPU 预填充工作区复用及细分计时。人设／完整回答预算与 ASR／TTS／YOLO 保持；真实 GGUF 独立对照、27 项门禁、完整链接／摄像头软件审计及模型回读通过，独立测试镜像已生成。构建与软件/实机验收是分开的，实际速度待板测，本预览不晋升稳定。

> 后续扩展：2026-10-31 [v218 云语音候选](CLOUD_VOICE_PROVIDERS_V218.md)加入讯飞／腾讯原生接口及 Edge 可选电脑网关，ASR/TTS 独立选择。完整构建、实际 ARM 编译及软件回归通过，详见第 23 节。第 1～22 节仍记录 v217 封存状态，不因此变为新版云 API／整机通过；旧镜像、文档和恢复点均保留。

更新：2026-10-31；维护主线：**v1.1／v219 修复候选**；保留 v1.0／v217-audit2 及全部历史记录。

最新板测反馈：用户确认 v1.0 的摄像头 YOLO、识物播报及速度/准确性很好，但常驻对话 TTS 报 EBUSY、重启监听未触发，以及 Wi-Fi 四次握手失败。
v219 修复 owner 的配置就绪查询，补充真实 RuntimePorts 联合回归、弱语音噪声学习保护、空闲 MIC 能量诊断及 WPA2/密码输入修复。
新镜像、软件证据、恢复与板测命令统一见 [v1.1／v219 修复记录](PRODUCT_V1_1_V219.md)。**新候选仍需真实 AP 与连续多轮声音复测；不把主机 MOCK 当作整机验收。**

以下 v217 描述与摘要是保留的历史基线；不能再将“软件审计候选”理解为常驻整机链路已经全通过。
本文是当前技术状态总览；部署见 [项目说明书](PROJECT_MANUAL_V1_0.md)，简要变化见 [初赛后功能变化](POST_PRELIMINARY_IMPROVEMENTS.md)。
最终固件、镜像摘要与软件审计以 [v1.0 发布记录](a733/PRODUCT_V1_0_RELEASE.md)、`docs/evidence/product-v1-audit/` 为准。
旧版 [v211 完整总结](PROJECT_FULL_SUMMARY_20261030_V211.md) 和全部过程文档保留，不能把旧版待办或旧版测试直接当作本版结论。

## 1. 项目定位、范围与结论

本项目以全志 A733／瑞莎 Cubie A7Z 为主平台，把 openvela/NuttX 从启动、CPU、内存、外设、网络和存储移植推进到端侧 AI 桌宠产品集成。
暂定产品名“小派 A7Z”，比赛队伍／仓库名仍为 Dogking／`contest2026_274_Dogking`。
系统由厂商引导链直接启动 ARM64 openvela 内核，不是在 Linux 用户空间模拟 RTOS。
Linux 用于厂商 NPU 参考执行、模型转换和黄金输出采集；实际产品固件不依赖板上 Python 或 Linux 共享库。

核心成果分为三层：

1. **平台层**：八核 SMP、4 GiB 分区内存、SD/GPT/FAT、Wi-Fi、维护服务、UART、SPI、I2S、UFS、NPU 与 MIPI 摄像头。
2. **AI 层**：官方云 Agent、本地 GGUF LLM、NPU 通用 ASR、CPU 声学模型＋NPU vocoder、YOLO 检测。
3. **产品层**：整条在线／离线路由、常驻半双工多轮、表情反馈、语音识物、UART4 下位机受控执行、版本和恢复体系。

**当前可以确认的是代码集成和软件证据，不是“整机绝对没有问题”。** 历史子系统板测、最新软件回归、最新整机板测是三个不同集合。
新审计镜像仍需真实录放音、视觉、云 API、机械动作和长稳测试后，才能形成整机验收结论。

## 2. 当前状态矩阵

| 模块 | 已有能力 | 证据与限制 |
| --- | --- | --- |
| 启动／内核 | ARM64 Image、NSH、异常诊断、SMP | 历史八核启动通过；不是自研完整 Boot0/BL31 |
| CPU／内存 | 6×A55＋2×A76，4 GiB 分区堆、专用保留区 | 亲和性需真实身份确认；非生产级 DVFS／热管理 |
| 基础外设 | UART、LED、看门狗、温度、TRNG、I2C、SPI、PWM | 节点和寄存器可读不等于所有外接设备通过 |
| SD／FAT | GPT、`/data`、读写及长文件名 | 主要为已验证 PIO 路径；冷读慢、FAT 无掉电事务保证 |
| UFS | 手工焊接器件的数据访问、挂载、复制／校验、模型读取 | 有历史板测；依赖受控 firmware handoff，不是完全独立冷初始化 |
| 网络维护 | FCU760K、WPA2/DHCP、NTP、SSH、FTP、HTTPS | 历史基本可用；网络环境、SSH 异常仍需长稳验证 |
| 官方云 Agent | DeepSeek/MiMo 接入、消息桥接、配置和工具边界 | 历史中文对话通过；新整条云语音未进行 API 实测 |
| 本地 LLM | MiniCPM5 GGUF、RAM 常驻、多核、KV／有限历史 | 历史真实生成及独立 GGUF 参考证据；不是 NPU LLM |
| I2S MIC／SPK | ICS43434 录音、MAX98357 播放、增益／音量 | 历史实录实放通过；旧 INMP441 不列为成功器件 |
| VIP2 NPU | 内存／MMU／同步、准备包、动态 Tensor、六路检测输出 | 有历史真 NPU 证据；不等于通用 ONNX/NBG 编译器 |
| 通用 ASR | 流式特征、encoder/decoder/joiner、跨块状态、端点 | 有 Linux 参考与历史用户转写反馈；新版仍需整机复测 |
| 本地 TTS | 文本前端、CPU ORT 声学、NPU vocoder、连续 PCM | v216 修正完整短语／队列；新版完整朗读、自然度待复测 |
| 常驻语音 | 初始化一次、空闲等待、多轮、停止、恢复 | 新版代码及故障注入通过；半双工，无 AEC／barge-in |
| ST7735／LVGL | 128×160、RGB565、表情与同帧框 | 颜色、表情历史通过；不依赖已完成 GPU 加速 |
| 13M214 | IMX214 专用 RAW 采集、处理、重复／连续采集 | 历史出图与短时连续通过；非完整 ISP/V4L2/13MP 视频产品 |
| 摄像头→YOLO→SPI | 动态输入、框、分段计时、CRC、双 RGB 缓冲 | 历史通过及软件优化；不宣称本版已达 30 FPS |
| 语音识物 | 新拍一帧、校验真实类别、中文播报 | 新产品软件通过；不是 LLM 视觉模型，不复用旧框 |
| STM32 | 115200 UART4、心跳、ACK、规则、会话使能 | 真实协议／PTY 故障测试；不等于机械动作和安全验收 |
| BLE／ESP32-S3 | 控制器 HCI、官方 Host、扫描、GATT 客户端工作 | 单板生命周期有板测；双板正向 GATT 业务未闭环 |
| 外接 USB／GPU／编码 | 诊断与部分研究保留 | UAC/UVC 全链、GPU 产品运行时、MP4 编码未完成 |

## 3. 软件架构与路由

```text
ICS43434 → I2S PCM → 端点检测／开头保留 → 本轮后端冻结
  ├─ cloud：官方云 ASR → 产品规则或官方 Agent/云 LLM → 官方云 TTS
  └─ local：NPU 通用 ASR → 产品规则或 CPU MiniCPM5 → CPU 声学＋NPU vocoder
                                ↓
                   MAX98357 扬声器＋LVGL 表情

产品规则优先级：明确 MCU 指令 → 明确识物 → 直接事实规则 → LLM 聊天
  识物：释放语音 NPU 会话 → IMX214 → YOLO → 同帧 SPI 图像／框 → 对应 TTS
  MCU：严格白名单 → UART4 → STM32 ACK/错误 → 对应 TTS，不执行 LLM 动作标签
```

`auto` 在每轮监听前检查 Wi-Fi IPv4、时钟、官方 Agent 附着、云 ASR/TTS 注册及独立语音凭据。
这是可尝试云端的资格检查，**不是公网 API 成功证明**。v225及之前的强制 `cloud` 缺条件时明确失败；从v226起，常驻服务的 `cloud` 明确改为联网优先，有限恢复失败后允许本地回退。独立文本强制cloud仍报错，local不自动上传。
自动模式的云 ASR 失败，可在尚未分发任何语义／动作前，用保留 RAM PCM 重新做本地识别。
动作已发送或部分语音已播出后不换链路重放；v226云失败进入120秒本地保持，后续轮次再判定。掉线时即便凭据及Agent仍就绪，也先执行有限Wi-Fi恢复；网络健康则不重连。详见v226专文的超时、取消、计费及实机验收边界。

审计版对缺失／损坏模式文件采用 **local 默认值**。只有完整保存的 `auto` 才启用自动云路由；已保存的 `cloud` 才强制云端。
独立临时文件避免旧 `.tmp` 阻塞配置；FAT 替换仍不能宣称为断电事务。

## 4. 官方组件与项目原创适配边界

云聊天基于官方 `packages_ai_agent`：消息总线、上下文、LLM、TLS、Skills、工具注册等。
项目实现板级驱动、桌宠桥接、路由、凭据保护、音频／显示／视觉／下位机适配和生命周期协调。
本地 LLM 是本项目的 GGUF CPU 执行路径，不能称为官方开箱即用 MiniCPM5 后端。
明确规则可以不调用任何 LLM；动作权限不由生成文本授予。

复用组件包括 NuttX/POSIX/VFS、WAPI/netlib、LVGL 9.1.0、mbedTLS、libssh、官方 Bluetooth/Zblue/libuv、GGML、kaldi-native-fbank、ONNX Runtime 1.23.2。
ORT 为 NuttX/AArch64 静态构建；其来源、补丁与许可证保留。厂商工具和模型有独立许可，不能用项目源码许可证替代。
官方组件复用不代表公共补丁均已被上游合入。

## 5. 仓库、SDK 和目录

主仓库为 `contest2026_274_Dogking-dev-ai-contest-2026`，分支 `a733-cubie-a7z-share`；完整 SDK 是同工作区的 `quickly-openvela`。
永久修改保存在仓库，构建临时暂存到 SDK，结束后逐文件恢复；`out/` 是忽略的本地制品，不是源码副本的替代品。

```text
board/a733-cubie-a7z/openvela-overlay/
  vendor/allwinnertech/chips/a733/        SoC 驱动、硬件和 NPU 运行时
  vendor/allwinnertech/boards/a733/       板级初始化和头文件
  apps/system/aipet/                     产品／语音／MCU
  apps/system/aipetllm/                  GGUF、Transformer、计算池
  apps/system/a733camera/                相机／图像／连续协调器
  apps/system/a733display/               ST7735、LVGL、同帧图像／框
  apps/include/system/                  窄公共接口
  nuttx/                                架构／驱动／公共 ABI 配套
app/a733home/                            BLE 家居客户端
app/a733storage/                         UFS 任务／挂载／文件校验
assets/aipet-online/                     脱敏配置与 SOUL
patches/                                 官方组件配套补丁
tools/                                   构建、转换、回归、打包、恢复
docs/                                    当前指南、历史过程及证据
out/                                     本地私有模型、库、日志、镜像封存
```

官方仓库为 `open-vela/contest2026_274_Dogking`，个人协作远端另行维护。本轮审计／文档是本地 Git 工作，不代表已经推送远端或发布 Release。
初赛报告还记录 RK3506、RV1103 的独立移植工作；它们是相关平台成果，不属于本轮 A733 v1.0 新镜像复测范围。

## 6. 启动、SMP、内存与基础外设

启动复用厂商 Boot0、SCP、BL31、U-Boot，再由匹配脚本 `booti` 进入 openvela EL1。
临时加载地址、Image 运行地址、BL31 交接地址不同，不能用 `go` 或混用旧脚本。
CPU0～5 为 Cortex-A55，CPU6～7 为 Cortex-A76；八核身份、栈、页边界、GIC 重分发器和分区内存曾逐步修复。
4 GiB 不等于任意地址都可分配，固件、NPU、DMA、栈和图像区域要分开；最终 ELF 检查构造器、段、对齐及运行区上界。

亲和性设置成功不等于已迁移；局部工作池检查 MPIDR/MIDR 和阻塞让出后的身份。
CRC 指令只在运行时资格确认后启用，保留切片软件回退。不为提速修改全系统调度或任意写 DVFS。
温控、挂起恢复、长时全负载仍不是已完成的产品能力。

基础节点覆盖 UART0 控制台、UART4、用户 LED、看门狗、THS、TRNG、I2C2/7、SPI1、风扇 PWM 和 SDMMC0。
`a733_hwdiag.c` 汇总状态，`a733_header_peripherals.c` 管理排针总线，`a733_trng.c` 提供随机源，`a733_tsadc.c` 处理温度。
PJ22/23 的 UART/TWI 复用、PB5～8 的 I2S、PD10～13 的 SPI、PJ24/25 的 UART4 必须避免冲突。
SWOUT1 用启动脚本受控检查、读改写、读回；不让 NuttX 随意抢占 SCP 的 PMIC 总线。

## 7. SD、UFS 与文件系统

SD 使用 GPT：固件／EFI／openvela 启动／data。板上 `/data` 是 FAT 数据卷，启动内核位于配套启动分区。
SDMMC0 以已验证的 4-bit／PIO 路径工作，不能套用卡片包装标称速度解释实际模型装载。
FAT 有单文件约 4 GiB 限制、权限限制和断电一致性限制；`rename` 不等于事务。

UFS 器件为用户焊接 Kingston `UFS128-CY14`，用于模型／频繁读取数据。
已形成 handoff、所有权、块设备、数据卷、挂载、进度、停止、复制与完整校验路径。
历史有大文件校验、模型读取及一次重启自动挂载证据；**没有 1000 MB/s 达标证明，也未完成所有冷启动电源／PHY 独立初始化**。
旧 Linux/U-Boot 残留可能影响启动源，该问题未通过擦除引导区解决；本轮没有擦 UFS。

产品准备本地语音时请求既有一次性有界 UFS 挂载，不自行格式化、迁移、修复或反复重置。
完整 UFS 资产优先；存在但残缺的通用语音集报错，不混用不同版本。SD 镜像附 MiniCPM5 备用。
0 字节模型不算有效复制；文件存在不代替大小、SHA256 和加载检查。

```text
/data/aic/                         FCU760K 固件
/data/models/                     SD 模型与 FTP 传输目录
/ufs/models/                      UFS LLM
/data/npu/general-asr/             通用 ASR
/data/npu/general-tts/             本地 TTS
/ufs/npu/general-asr/              可选完整 UFS 集
/ufs/npu/general-tts/              可选完整 UFS 集
/data/npu/yolov8n-pcq-v3.a7pm       YOLO（也可在 /ufs/npu/）
/data/ai_agent/config/             Agent／音频／规则配置
/data/ai_agent/product-mode        产品路由策略
```

## 8. Wi-Fi、维护服务与凭据

FCU760K/AIC8800D80 经过 USB BootROM、固件上传、重新枚举、LMAC/RF/ME、监管域和站点接口初始化。
项目板级驱动与官方 WAPI 分工不同。历史已通过双频扫描、WPA2、DHCP 和基本 TCP/UDP；环境波动、漫游及长稳不能省略验证。
NTP 关系 HTTPS 时钟校验；SSH/FTP 提供维护。服务器应按本机 `ifconfig` 地址连接，不沿用历史 IP。
`service` 是单数命令。自启动是持久策略，`off` 不能解释为驱动初始化失败。
SSH 主机密钥变化先确认设备身份；不全局关闭主机验证。历史 SSH 报文异常仍需专项复核。

`aipet init` 配 DeepSeek/MiMo LLM；`aipet audio setup` 配独立云 ASR/TTS。
原有板端隐藏输入和加密保存保留，不打印 Key、不在 Git 中保存私人配置。
板端加密不等于具备抵抗物理拆解、FAT 复制和根权限读取的安全芯片方案。
录音默认 RAM／显式命令；不自动开机监听。本地与云端各自保留有限 RAM 历史，本地历史不自动上传。

## 9. 官方 Agent 与产品核心

`agent_bridge.*` 负责异步请求、响应关联和错误；`agent_setup.c`、`agent_secret.*`、`agent_startup.c` 管理初始化／保护／启动。
`pet_core.*` 解析回复；`pet_routes.*` 保留底层规则和诊断；`product_core.*` 是 v1.0 确定性产品路由。
`product_runtime.*` 对接真实服务、云对话、摄像头、播报和模式；`product_labels.hxx` 映射 COCO80；`product_cloud_config.c` 隔离 C 配置接口。

直接事实规则不在情绪句中做任意子串匹配；明确运动和识物走规则，而不是让 1B 模型推测硬件。
云聊天撤销执行器工具能力，模型动作标签不驱动机械。ACK 仅表示接收，不表示物理完成。
人格层与 SOUL 提供亲切、诚实、适度陪伴和情绪标签；不承诺小模型具备大模型的理解能力。
语言风格／表情联动不等于已经训练了多情感声学模型或多音色系统。

## 10. 本地 LLM、历史与性能

当前首选 `MiniCPM5-1B-Q4_K_M.gguf`：688065920 字节，SHA256：

```text
81b64d05a23b17b34c475f42b3e72fbde62d4b92cc34541f7a8031d0752deafa
```

早期 Qwen2.5-1.5B 路径仍可追溯，当前产品不依赖该 1117320736 字节（约 1.12 GB）模型。
`aipetllm_modelcheck.c` 名称虽带 check，实际包含计算；GGUF、tokenizer、ChatML、量化矩阵和 Transformer 由本地执行。
权重入 RAM 常驻，多核池、A76/A55 行权重、Q8 工作区、有限 KV 前缀复用和 bounded prefill 减少重复工作。
不把 CPU 多核或 ARM SIMD 说成 NPU LLM；没有随意改变数值或删校验换速度。

本地默认三对 RAM 历史，可清空而保留权重；云端最多三对完整历史，受桥接字节上限约束，两侧不自动互传。
成功完整回复才提交历史，错误／取消／超预算不把半句话当作完整记忆。
输出预算有上限，不能承诺任意长文永不触顶。

历史板测已有首字约 2 秒量级反馈，但这不是本版统一基准。
必须分别记录冷加载、预填充、首可见 token、首短语、后续 token 和上下文长度；整机首音不能拿文本首字代替。
真实 GGUF 独立参考证据在 `out/llm-quality-v216-r4/summary.json`；审计不以模拟回答冒充真实生成。

## 11. I2S 录放与应用 PCM

当前器件为 ICS43434 麦克风＋MAX98357 合并模块，麦克风数字增益默认 x5、音量可调。
历史 INMP441 存在反接损坏疑点，不能把 ICS43434 成功追记为它也通过。
I2S0 提供 48 kHz、双 32-bit 时隙；MIC 取有效通道并转换 S16，ASR 侧低通／重采样至 16 kHz mono。
录制时硬件计数、实际时间、FIFO 和非零样本同时检查，拒绝空 FIFO 重复读取伪造五秒。
PB7 在空闲／录音保持 GPIO 低电平，TX 时切回 I2S，解决录音时扬声器电流声。

`a733_i2s0_audio.c`／`a733_i2s0_playback.inc` 是驱动，`a733_audio_client.c` 和公共 PCM 接口接应用。
这不是“全部标准 Media/audio lower-half 已完成”；底层主要是已验证的轮询路径。
计数、DMA/IRQ 或 `status=0` 只证明数字传输，不能证明喇叭真的出声。
PB5 是 BCLK、PB6 是 LRCK；此前完全无声有明确时钟接线颠倒证据，不应再用错误寄存器改动补偿接错线。

## 12. VIP2 NPU 与模型工具链

硬件身份 CID `0x1000003b`，单 VIP2 执行资源，窄整数 ABI v4；内存池、准备区和 CPU/DMA 地址分别约束。
模型执行包含对齐／范围、NPU MMU、cache clean/invalidate、屏障、提交、等待、原始输出与后处理。
多 Tensor 准备格式与动态输入有版本限制；不是从一次捕获便得到通用编译器。
厂商 Linux VIPLite／Model Zoo 基线、转换参数、prepared 包、黄金输入／输出和许可要一起保留。

验证不能仅看中断、耗时或全零输出：需要不同真实输入、所有输出、重复冷热执行和后处理一致。
YOLO 保留六路头、输入 CRC、输出保护与执行后权重检查；硬件 CRC 需能力确认，等价软件路径继续存在。
ASR 旧 KWS 和通用 Zipformer 的模型、内存、状态不同，不能混用已通过结论。
故障所有权不明时保留原任务和缓冲区，禁止 reset/kill/盲重试；目前依靠重启恢复隔离状态，不宣称任意故障无损热恢复。

## 13. 通用 ASR

链路为实时 PCM → 重采样／特征 → NPU encoder → CPU 调度／转导解码与 NPU decoder/joiner → 文本。
真实跨块状态按模型契约维护，所有输出参与正确性验证；关键词识别不能替代通用转写。
相关源：`voice_fbank.*`、`voice_asr_stream.*`、`voice_asr_decode.*`、`voice_general_asr.*`、`voice_general_provider.*`。
旧 KWS、多模型参考、Linux 通用 ASR、openvela 新会话各有独立证据，不能合并写成一个“全部通过”。

优化包括 40 ms 特征批处理、停止无用尾部计算、流式状态、会话复用、局部工作线程资格检查。
新常驻流程保留约 400 ms 开头音频、约 700 ms 静音端点，限制约 8 秒单次话语；过长或空白不下发半条动作。
需要继续测近远场、静音／噪声、快语速、端点准确性、积压及收尾时间。

## 14. 本地与云端 TTS

本地为文本规范化／词典／未知词处理 → **CPU ONNX Runtime 声学模型** → **NPU vocoder** → PCM 队列 → I2S。
不能称整套 TTS 均跑 NPU；CPU 声学部分是真正模型执行，不是固定录音拼接。
相关源：`voice_tts_frontend.*`、`voice_acoustic.*`、`voice_tts_engine.*`、`voice_phrase_pipe.*`、`voice_play_stream.*`。
模型文件与词表匹配，vocoder 输入布局／窗口／输出长度依据厂商参考验证。

v216 起修正多轮质量、完整短语排队、生产者完成与临时断流区分、尾句刷新、未知英文不丢掉剩余整句。
首短语可与后续 LLM 生成重叠，播放需补缓冲、停止和错误所有权保护；不是按固定字数任意切词。
未知英文可使用保守字符处理，不能宣称达到专业中英混读发音品质。
云端显式调用官方 TTS，不因本地 provider 已配置而偷偷换成本地；按完整分句播报，错误明确返回，不把部分播放说成整句成功。
云 ASR 是端点后的批量接口、云 LLM 等官方完整响应，未伪称已做云端逐 token/音频流式。

## 15. 常驻交互与停止

`aipet run` 显式开始：检查配置、挂载和模型、预热 LLM／声学／NPU，然后 READY MIC ON，安静等待并自动多轮。
不是每次说话都重新初始化，也不是完全免冷启动。准备阶段有分段日志和状态，空白话语不提交 LLM。
MIC OFF 出现在转写收尾、处理和播放阶段，是半双工设计；**不支持播放中语音打断、AEC、自动唤醒词或开机录音**。

常驻任务、摄像头任务和 UI 使用独立持久任务及稳定控制台，不借用断开 SSH 的 stdio 生命周期。
共享开始锁和所有权状态避免前台、常驻和连续摄像头检查后竞争；只允许一套已证明的资源使用方式。
`aipet stop` 是协作请求，必须等 `active=0`；`stop=1` 不等于已经退出。
审计版让前台 Ctrl+C 在资源安全退出前保持协作语义，取消先于动作分发检查；清理所有权失败高于普通取消／超时。
如果所有权不能确认，原任务继续保留、撤销运动使能并要求重启，而不是释放后假装可继续。

## 16. ST7735、LVGL 与摄像头

显示为 1.8 英寸 ST7735、128×160、RGB565 MSB、SPI mode 0、最高 12 MHz；使用已验证 direct 8-bit 路径和 LVGL。
表情支持普通、开心、难过、惊讶、生气、思考、安慰等；tick、状态保持和事件交接曾逐步修复。
LVGL／SPI 由明确所有者使用；先完成屏幕冷初始化，再准备同属 PB 寄存器组的 I2S，避免 GPIO RMW 竞争。
没有宣称 GPU 加速 LVGL。

摄像头是 **Radxa 13M 214／Sony IMX214**，不是误写的 12M314；V1.11 板接 J5 31-pin FPC，J3 是 PCIe。
经过板级只读盘点、电源／时钟控制、I2C ID、模式、MIPI 包、CSI DMA、RAW 格式和图像处理逐级推进。
有历史重复采集、正常颜色和短时连续图像证据；不能从寄存器匹配推定电气测量通过。
当前是专用 RAW 路径与软件处理，不等于完整标准相机框架、硬件 ISP、MP4 编码或 13MP 满速录像。

## 17. YOLO、同帧展示与语音识物

摄像头 RAW → Bayer／YUV／色彩处理 → RGB／letterbox → 动态 NPU 输入 → 六路输出 → 解码/NMS → 同帧底图／框。
`a733_camera_live.c`／`a733_camera_pipeline.inc` 管理采集、预处理、推理和显示，不在每帧写 SD。
优化保留单受控 RAW DMA 区、明确所有权的 RGB 槽、可选独立渲染、常驻模型及分段时间。
减少 CRC 和重复准备工作必须证明等价；全部输出与执行后权重检查没有删除。

v1.0 识物规则如“识别物体”“看看这是什么”“你看到了什么”，不在“不要识别物体”等语句中做盲目子串触发。
每次生成唯一请求 token，新采集、推理和展示均成功后才发布结果；旧、取消、CRC/IRQ 错误或清理失败不能复用上一帧。
依据冻结 COCO80 类表和可信框返回中文类别／数量，过滤低置信度，声明可能有误；没检测到不等于画面为空。
不是多模态 LLM，不让聊天模型编造摄像头所见。

识物前关闭语音 NPU 会话，结束后恢复所选 TTS 和下一轮监听。
连续视频与语音服务互斥；`vision start` 不是后台无约束同时跑两套 NPU 模型。
没有新的 30 FPS 或数小时长视频稳定性验收；性能要分别看采集、预处理、NPU、CRC、解码和 SPI。

## 18. UART4、STM32 与机械权限

旧 TW-TTS 已退役，UART4 交给 STM32，I2S 承担播报，115200/8N1/3.3V TTL。
参考仓库 `hxy299/STM32C8T6Mini-Play` 固定到 `e83a9cac8a0e4a775a38910bdaf08c68bfdffc8c`。
`mcu_protocol.*` 处理帧／CRC／白名单，`mcu_link.*` 处理收发、心跳、状态、ACK 和超时；官方工具桥接与产品规则权限分离。

`arm session` 是显式本次连接会话权限，不再每条命令都开使能；普通有限期 arm 路径也保留。
启动、停止、失联、复位、配置变化、故障、NAK/ACK 超时按策略撤销权限；不自动清故障、不在重连后自动重新 arm、不重发运动命令。
迟到 ACK 和先到故障不能复活运动权限；协议对照和 PTY 测试不是真实机械安全认证。
明确区分 ACK、执行中与实际完成；没有位置／行程反馈时不声称已经运动到位。

“向左看／向右看／回中”是舵机，“左转／右转”是底盘；没有舵机不是软件改动能补救的。
步数有严格界限和中文数词规则，歧义、组合动作、否定和越界拒绝。
半双工不能靠播报期间的语音急停，必须保留物理断电／停机能力和独立控制台。

## 19. BLE、其他外设与扩展板

板载 BT 与 Wi-Fi 共享 FCU760K USB 运行设备，已有只读 HCI 查询、H4 传输、官方 Bluetooth/Zblue Host、扫描和生命周期修复。
`bttool` 命令受理不等于异步完成，需等状态回调；扫描必须先停止再重启，disable 后关注真实 OFF 状态。
GATT 客户端和应用协议工作保留，但 ESP32-S3 N16R8 实际可用 Peripheral 固件和双板无线业务未闭环。
不能因为扫描到广告就说智能家居设备已可控制。

外接 Type-C USB 主机／UVC/UAC 研究、GPU、编码、标准音频／相机服务继续保留，不列作 v1.0 已完成产品能力。
A733 SCP/辅助核目前依赖厂商固件，不能说已开放给用户跑独立舵机实时任务。
RK3506、RV1103 的相关移植在各自资料／工程中核对，不将 A733 测试外推到其他芯片。

## 20. 接口与关键代码索引

| 区域 | 入口／文件组 | 主要责任 |
| --- | --- | --- |
| 产品 | `aipet_main.cxx`、`product_core.*`、`product_runtime.*` | 命令、冻结路由、资源、事实与聊天 |
| 音频应用 | `audio_app.*`、`audio_product.inc`、`audio_capture_live.*` | 配置、常驻、PCM、端点、协作退出 |
| 本地计算 | `local_llm.*`、`aipetllm_*` | 模型、历史、KV、多核、数值执行 |
| ASR／TTS | `voice_general_*`、`voice_asr_*`、`voice_acoustic.*`、`voice_tts_*` | 真模型执行与语音调度 |
| 播放 | `voice_phrase_pipe.*`、`voice_play_stream.*`、`speech_output.*` | 完整短语、连续 PCM、输出选择 |
| MCU | `mcu_protocol.*`、`mcu_link.*`、`mcu_agent_tool.cxx` | 二进制协议、权限和事实反馈 |
| 视觉 | `a733_camera_image.c`、`a733_camera_live.c`、`a733display_main.c` | 几何、连续协调器、单 UI 所有者 |
| BSP | `a733_npu*`、`a733_tensor_*`、`a733_i2s0_*`、`a733_imx214_*` | NPU、Tensor、录放、传感器 |
| 存储／网络 | `a733_ufs_*`、`a733_sdmmc.c`、`a733_wifi_usb.c` | 块设备、数据卷、SD、无线固件与传输 |
| 工具 | `build/check/test/seal/package/checkpoint-*` | 临时 SDK、严谨验证、普通文件封装、可恢复源码 |

典型诊断节点为 `/dev/a733-hw`、`/dev/a733-wifi`、`/dev/a733-bt`、`/dev/a733-uart4`、`/dev/a733-audio`、`/dev/a733-ufs`、`/dev/npu0`，相机另有控制／帧节点。
节点和 ready 只是分层诊断，必须配合真正传输、模型输出、画面和声音。

## 21. 版本、证据、构建和恢复

| 阶段 | 主要结果 | 不应外推的结论 |
| --- | --- | --- |
| 初赛报告 | 基础平台、Wi-Fi、官方云 Agent、Qwen、NPU、UART TTS | 当时 I2S/摄像头/屏幕颜色等仍未收敛 |
| v128 等 | ST7735／LVGL 表情历史通过 | 旧版能亮不保证电源域变化后仍亮 |
| v147～v149 | ICS43434 实录、增益、应用方向 | 不给旧损坏 INMP441 追认通过 |
| v178～v190 | MiniCPM5、通用 ASR/TTS 候选、语音闭环编排 | 参考张量不等于任意语音已转写 |
| v200～v204 | IMX214 颜色／重复采集／YOLO 同帧短时展示 | 不等于完整 ISP 或 MP4 |
| v205～v209 | CRC、准备复用、亲和性、流水线优化／故障修复 | 不把设置 affinity 当成已在 A76 执行 |
| v210～v216 | 语音优化、录放接线定位、常驻、质量／完整短语修复 | 首音 3–5 秒无统一达标证据 |
| v217 | 整条云／本地路由、识物播报、STM32 会话、私有测试镜像 | 软件检查不是整机板测 |
| v217-audit2 | 清理错误优先级、动作前取消、前台 SIGINT、失配默认本地 | 仍待本候选新整机验收 |

主机回归显式模拟引擎／设备，不冒充 ASR、TTS、摄像头或 MCU 板测。
软件检查包括 19 项既有语音门禁、产品规则／实际服务／PTY MCU／9 个实际 ARM 单元、相机回归、实际运行时边界测试、完整 ELF/ABI/ISA。
不删除失败日志、不降低 CRC 或绑定检查来让测试变绿。
新缓存需实际 `CONFIG_A733_PRODUCT_V1=y`，旧缓存成功不能代表产品功能被编译进去。

构建入口 `tools/build-a733-product-v1.sh`；完整检查 `tools/check-a733-product-v1.py`；封装 `tools/seal-package-product-v1.py`。
封装只生成新的普通镜像文件，核对根分区／GPT／脚本／内核、49 项语音资产及新增模型；不写实物 SD/UFS。
源码、构建产物、封存文件、镜像内文件、外部 Windows SHA256 分别绑定，不只检查输入文件。

保留 v204、v216、旧 v217 的镜像／标签与本地恢复，审计版使用不同名称和新恢复目录。
恢复包含源码、Git bundle、补丁、清单和 COMPLETE；大镜像／模型／库是摘要引用，必须保留对应外部文件。
只恢复到新目录，不对原仓库硬重置。历史原始日志不重写，原用户总结未删。

## 22. 限制、验收顺序与下一步

当前**没有**本审计候选的真实云 API、整机声画／机械验收，也没有 3–5 秒声学首音证明。
没有全双工、AEC、barge-in、开机私自录音、自动运动使能、完整 ISP／GPU／MP4、BLE 双板闭环、UFS 标称速率或生产级热／掉电认证。
原 UFS U-Boot、Wi-Fi/SSH 环境异常、长期资源稳定性和各类模型分发许可需继续专项管理。

下一步按顺序：

1. 核对镜像 SHA、引导源、硬件接线及温度／电源。
2. 单项录放、真实 ASR 文件／麦克风、完整中英文 TTS、本地 LLM。
3. 常驻多轮、上下文、停止恢复、静音／长句／异常。
4. 相机／六路 YOLO／同帧框、重复识物、连续模式启停。
5. 配置独立云凭据，逐项验证在线三段与轮次间切网，不重放动作。
6. STM32 心跳、会话、ACK/实际动作、失联／故障撤权及物理急停。
7. 分开测冷加载、ASR 收尾、首 token、首短语、首 PCM 和真实首音，再做长稳与热测试。

对外建议表述：**“基于 A733/openvela 自主平台适配形成的端侧 AI 桌宠参考项目，多项子系统有历史实机成果，v1.0 已完成软件集成与可恢复交付，正在进行最终整机验收。”**
不要表述成“所有模块完美”“所有 AI 都上 NPU”“无网／有网全链已稳定 3 秒”或“全部驱动均已上游合入”。

## 23. v218 独立扩展：多服务商云语音

本节补充后续软件候选，不修改上述 v217 镜像和历史验收结论。

| 服务商 | ASR | TTS | 接入与边界 |
| --- | --- | --- | --- |
| 火山引擎 | 保留 | 保留 | 沿用官方接口及独立语音凭据 |
| 讯飞开放平台 | IAT v2 | 在线 TTS v2 | 板端原生 WSS；APPID、APIKey、APISecret |
| 腾讯云 | SentenceRecognition | TextToVoice | 板端原生 HTTPS／TC3；SecretId、SecretKey及可选 STS token |
| Edge TTS | 不支持 | 实验性网关 | 额外电脑运行 edge-tts＋FFmpeg，通过 HTTPS 返回 PCM；不是板端原生或正式 Azure API |

ASR/TTS 分别选择，可用腾讯识别＋讯飞合成。DeepSeek/MiMo 仍只负责 LLM，不能复用其 Key 作为语音密钥。
`aipet audio setup <provider>` 保存配置，`aipet audio providers asr|tts <provider>` 选择后端；保存／选择不会开始录音或自动改变产品模式。
`aipet audio cloud-speak` 显式测试云合成，避免本地 TTS 已加载时把本地播报误认为云调用。
密钥隐藏输入，并用现有板载文件密钥＋AES-GCM 保存；这不是硬件安全元件。配置存在不等于账号权限、额度或云 API 已通过。

手动测试与常驻链路统一调用 `cloud_voice.*`；`cloud_voice_protocol.*` 负责签名、UTF-8/Base64、完整结果检查，`cloud_voice_socket.*` 负责单所有者 WSS。
配置经 `agent_setup.c` 和 C-only `product_cloud_config.c` 桥接，避免混用 C/C++ ABI。
WSS 验证 CA／主机名／Upgrade，处理分片和 Ping；掩码使用安全随机源，目标符号表另检查 `getrandom` 的 C ABI。
音频合同为 16kHz 单声道 S16LE；ASR 最多 8 秒，TTS 每完整短语最多 10 秒，缺终包或超限不发布残缺结果。
讯飞分帧上传发生在端点录音之后；腾讯为短音频接口；本版云 TTS 收齐完整短语后播放，不称为边录边上传或首 PCM 即时播放。

运行时代码恢复点：`28929a2999586b621230729b74a205c132294ecb`；软件证据提交：`507efb0`；本地标签：`a733-v218-cloud-voice-candidate-20261031`。
独立缓存为 `quickly-openvela/cmake_out/cubie-a7z_nsh_v218_cloud_voice`。完整链接、ELF／ABI／ISA／布局、13 个实际 ARM 编译单元及既有软件回归通过，SDK 暂存的 315 个路径已按字节恢复。
提供商回复为 MOCK；另有本机真实 TLS/WebSocket、HTTPS 网关入口验证，不能替代真实云账号调用、Edge 合成、MP3 解码或板上声学验收。
[封存报告](evidence/cloud-voice-v218/README.md)可随 Git 保留，不依赖整个 `out/`。

**没有新 SD 镜像，没有真实云／整机首音测试，没有 PR 或远端推送。** 用户尚无语音凭据，可继续使用本地模式；Edge 的“无需 API key”不等于永久免费、商业授权或 SLA。
部署、CA 与网关要求、配置命令和逐项测试以 [v218 云语音说明](CLOUD_VOICE_PROVIDERS_V218.md) 为准。
