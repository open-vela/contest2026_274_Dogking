# Dogking A733：面向端侧 AI 的 openvela 新硬件适配

## 最终提交版本：v229（2026-11-04）

提交材料补充：[ASR/TTS/YOLO/LLM优化Skill](skills/embedded-ai-pipeline-optimization/SKILL.md)、[10—11月AI日志导入记录](docs/AGENT_LOGS_20261104.md)。新增2个真实主开发线程的单一快照导出/1340条事件，通过官方格式校验。内部guardian审查已排除，其他续接片段不拼接、不造ID，不冒充完整历史。作者已要求通过PR合入；当前同步策略见 [发布策略](docs/submission-v229/UPLOAD_POLICY_R2.md)：A733全量，其他平台保留，日志/Skill/文档增量。

按项目作者要求，开发在 **v229** 收尾，停止 dslocal/v230；最终源码保持 v229 的固件实现，不把未完成的混合模式带入提交。见 [v229 最终提交说明](docs/FINAL_V229_UPLOAD.md)、[版本指针](docs/FINAL_RELEASE.json)、[项目完整总结](docs/PROJECT_FULL_SUMMARY.md)。

v229 原镜像与哈希保持不变，走正式PR发布，不直接覆盖官方保护分支，不绕过CLA/检查。**最终提交版本不等于新增板测验收**：v229已通过44项软件门禁与镜像回读，真实云账号连续多轮仍待复测；已获历史整机确认的稳定回退仍为v1.1/v219。下方按时间保留的“未推送/仅准备”等为当时记录，实际发布状态以GitHub PR及官方分支为准。

三张答辩亮点生图的技术文案与证据索引见 [亮点图说明](docs/presentations/HIGHLIGHT_IMAGES_20261104.md)。完整图片保存在工作区独立的决赛答辩目录，不与固件大文件混入 Git。

2026-11-03 开发候选：[v3.0pre／v227 改动与顺序测试](docs/PRODUCT_V3_0PRE_V227.md)：讯飞 TTS 传输诊断/超时预算、DeepSeek flash 非思考、完整句 TTS 双槽预取、云情感人设与显式授权的一轮一个白名单动作。桌宠步统一×20，move-raw 保留原始脉冲。v219稳定与旧镜像保留；最终制品以专文封存证据为准，不把软件测试等同于新云账号/机械/延迟验收，不创建PR或推送。

v227独立2GiB镜像已生成，41门禁、完整审计、模型/启动回读、SDK319项还原及Windows独立哈希通过。[封存证据](docs/evidence/product-v30pre-v227-image/README.md)。镜像SHA256 `e4348914e72bcf282f42755ee3ca5e0d48985cfb62eceeab165d98827673ce64`；恢复标签 `a733-v3.0pre-v227-image-20261103`。新讯飞实际发声与延迟仍待板测，v219稳定不变。

2026-11-03：[v2.1pre／v226 联网恢复镜像与测试](docs/PRODUCT_V2_1PRE_V226.md)已生成，40项软件门禁、完整ARM链接／ELF／ABI／ISA／摄像头审计、镜像回读及Windows独立哈希通过，SDK316项还原。[封存证据](docs/evidence/product-v21pre-v226-image/README.md)。修复v225云ASR错误／冷却导致常驻退出，增加最多3次恢复、120秒本地保持、可取消的保存配置Wi-Fi重连及WSS分段诊断；不重放动作或已播放语音。v219稳定与六个旧镜像保留，新版尚待云账号／AP掉电恢复及声学长稳板测，不晋升稳定，无PR或推送。

v225 已封装：独立 [v2.0pre 测试镜像与顺序命令](docs/PRODUCT_V2_0PRE_V225.md)，36 项软件门禁、完整构建／链接审计、镜像回读及 Windows/Linux 哈希通过，SDK316项还原。[封存证据](docs/evidence/product-v20pre-v225-image/README.md)。新增识图口语、桌宠会话免反复 arm、语音数字步数×10、持久音量／静音／增益，以及显式开启的开机准备／欢迎语／监听。尚待新板测，不晋升稳定；旧v219/v220/v221/v223/v224制品哈希不变，无PR或推送。

2026-11-03：[v2.0pre／v225 开发预览与完整测试步骤](docs/PRODUCT_V2_0PRE_V225.md)：补齐识图／动作语音路由、明确步数×10及300Hz默认档、常驻桌宠命令授权、持久音量设置、显式开机监听与就绪欢迎语。保留故障和未知ACK不重发；不改NPU驱动、模型或全局调度。最新稳定仍v1.1/v219，旧制品不覆盖，新版状态以专文封存记录为准。

2026-11-02：[v1.3fix2／v224常驻端点修复镜像](docs/PRODUCT_V1_3FIX2_V224.md)已生成，33项软件门禁、完整固件与镜像回读／Windows独立哈希通过。[v223真实反馈](docs/evidence/voice-v223-board-20261102/README.md)确认独立ASR/TTS正常，但常驻短命令反复8秒超长拒绝；新版增加80ms持续活动确认，并在采集/consumer健康结束后丢弃拒绝轮、不追加最终推理。保留700ms静音、8秒动作保护；最后一次卡死根因尚未证明，新版仍待板测。原v223/v221/v220/v219镜像不变，v1.1仍为稳定版。

2026-11-02：[v1.3fix／v223修复镜像](docs/PRODUCT_V1_3FIX_V223.md)已生成：修正常驻语音端点、流式TTS仍受10秒总量限制及声学分段过长，补UTF-8尾边界；完整构建、31项软件门禁、ELF／ABI／ISA／摄像头检查及镜像回读通过，Windows/Linux SHA一致。旧v219／v220／v221镜像保留，最新稳定仍v1.1；本版待板测。英文仍为字母中文拼读，非自然英文；未加入尚待确认的MiMo Token Plan方案，不宣称全部硬件问题或3～5秒首音已验收。

2026-11-02：[v1.2fix／v221 独立测试镜像已生成](docs/PRODUCT_V1_2FIX_V221_IMAGE.md)，完整构建、28项软件门禁、ELF／NPU ABI／ISA／摄像头检查及最终模型／启动文件回读通过，SDK316项还原，Windows/Linux SHA一致。新增MCU诊断已在真实固件中；v1.1稳定镜像和v220预览镜像未变。v220新日志中的普通长回复流式超时已记录，不能宣称本版通用修复；v221仍待板测。

2026-11-01：[v1.2fix／v221 STM32 同步](docs/PRODUCT_V1_2FIX_V221.md)，对照下位机 `efdb437` 手册与真实代码，新增配置／电机／链路诊断、TOF事件、可选 MCU TTS 及明确的配置确认；保留无自动解锁／无运动重试和默认 I2S 播报。属于 v220 后的源码预览修订，不替代 v1.1 稳定版；本轮没有新整卡镜像或机械验收。

2026-11-01：**最新稳定版为 v1.1／v219**。用户完成板测并确认正常；本地多轮 ASR→LLM→TTS、识物播报、停止恢复和 Wi-Fi 已有本轮证据。[稳定版指针](docs/STABLE_RELEASE.json)、[验收与回退](docs/PRODUCT_V1_1_V219.md)。沿用通过测试的原镜像字节，文件名中的 `candidate` 仅为历史命名，不重编译、不覆盖。下方 10 月 31 日内容保留为历史交付记录；云账号等未测试项目不因此获得新验收。

开发分支：[v1.2／v220 低延迟预览](docs/PRODUCT_V1_2_V220_PREVIEW.md)。独立 2 GiB 测试镜像已生成，27 项门禁、完整链接／摄像头软件审计和回读校验通过。默认关闭本地聊天历史，保留小派人设、常驻权重、精确前缀 KV 和完整句子；上下文配置与稳定版隔离。真实 GGUF 数值对照通过，实际速度仍待板测，不替代 v1.1 稳定版。

2026-10-31：[v1.1／v219 修复候选与测试步骤](docs/PRODUCT_V1_1_V219.md)已生成新 2GiB SD 镜像，合并 v218 软件扩展；26 项门禁和完整构建/摄像头软件审计通过，尚待真实 AP 与常驻多轮声音复测。修复 v1.0 板测暴露的常驻 owner 就绪检查 EBUSY，补充 MIC 门控/诊断及 Wi-Fi 握手和隐藏密码输入修复。v1.0 原镜像和恢复点保留，不提交 PR、不推送。

2026-10-31：[v218 多服务商云语音扩展](docs/CLOUD_VOICE_PROVIDERS_V218.md)：保留火山，增加讯飞／腾讯原生 ASR+TTS 和可选 Edge TTS 电脑网关，与 v217 封存版分开。新增云账号和整机验收未执行；Edge 不是板端原生或微软正式开发接口。
v218 已通过完整构建、实际 ARM 编译及软件回归，[验证与恢复记录](docs/evidence/cloud-voice-v218/README.md)已封存，本地标签 `a733-v218-cloud-voice-candidate-20261031`。该 v218 批次当时只有独立候选固件，未生成 SD 镜像；后续 v219 已生成整卡候选。旧 v217 镜像不包含这些新命令。

项目整理入口：[目录与保留策略](docs/PROJECT_DIRECTORY_GUIDE.md)、[首版决赛答辩记录](docs/presentations/FINAL_DEFENSE_V1.md)。代码、历史文档、镜像和恢复包保持原位置；决赛材料独立版本化，本轮不提交 PR。
首版答辩 PPT 已另存 r7 字体兼容修复版（英文 Arial／中文微软雅黑）；原 r4 保留，详见上述答辩记录。修正版已渲染检查，原生 PowerPoint 放映仍待确认。

当前文档：[项目完整总结](docs/PROJECT_FULL_SUMMARY.md) · [v1.0 项目说明书](docs/PROJECT_MANUAL_V1_0.md) · [初赛后新增／改进简述](docs/POST_PRELIMINARY_IMPROVEMENTS.md)。
2026-10-31：[v217-audit2 审计修正版](docs/a733/PRODUCT_V1_0_AUDIT.md)已重新构建／封装，保留旧镜像；修正取消／所有权和模式失配默认值。软件门禁通过，整机／云 API／首音仍待验收。

2026-10-31：已生成[小派桌宠 v1.0 候选版（v217）](docs/a733/PRODUCT_V1_0.md)：
明确的在线/离线 ASR＋LLM＋TTS 路由、常驻多轮、真实摄像头 YOLO 同帧显示与识物播报、
以及保留故障保护的 UART4 STM32 会话使能。v204/v216 和历史文档保留。
[镜像、校验和、构建及恢复](docs/a733/PRODUCT_V1_0_RELEASE.md)、[软件证据](docs/evidence/product-v1/README.md)。
**软件构建、镜像回读不等于整机板测；云语音需独立凭据，3–5 秒首音没有新验收证据。**

2026-10-30：[v211语音热路径优化候选](docs/a733/VOICE_V211.md)继续减少重复工作：
ASR 40ms批处理＋停止无用尾部FFT、LLM Q8工作区和TTS引擎缓冲复用、
40ms首次真实PCM起播。90组ASR边界输入逐字节等价、216条Linux捕获重放、
实际GGML量化器/10000次复用及完整短尾/断流/取消/错误回归通过。
14组语音、16个ARM单元、22组摄像头和最终ELF/ISA检查通过。
[上板命令](docs/a733/VOICE_V211_TEST.md)、
[镜像/构建/本地恢复](docs/a733/VOICE_V211_RELEASE.md)、
[封存证据](docs/evidence/voice-v211-final/README.md)。
v210及旧版本保留；没有新硬件验收，**3–5秒真实声学首音仍待实测**。

2026-10-30：v209 已保留、板测暂缓，转回
[v210 语音链路优化候选](docs/a733/VOICE_V210.md)。
v190 三条 ASR 文件测试均为 -20，不能认定执行过 ASR；
新版内置锁定 WAV、细分失败阶段、实际 A76 迁移确认、Tensor 可选 ARM CRC、
LLM 持久池热路径及更早首短语合成。12组语音＋22组摄像头回归、
16个语音目标单元、完整链接/内存/ISA和镜像回读通过。
[测试命令](docs/a733/VOICE_V210_TEST.md)、
[镜像/构建/恢复说明](docs/a733/VOICE_V210_RELEASE.md)、
[封存证据](docs/evidence/voice-v210-final/README.md)。
尚未板测；3–5秒真实结束说话到声学起音**没有达标证据**。

2026-10-29：v208-r2 实机短测确认双槽每帧YOLO稳态7.733FPS、
231帧/231推理、error/cleanup/fallback均0，[原始记录](docs/evidence/camera-v208-board-20261029/README.md)。
但A76和硬件CRC未生效，实际仍CPU1/A55与portable。已封存独立
[v209迁移确认与三阶段并行候选](docs/a733/IMX214_V209_AFFINITY_ASYNC_TEST.md)：
阻塞让出后双重核心证明、独立CPU/CRC执行测试、唯一LVGL线程、
两槽同帧快照与完成后计数；22组回归、ARM/最终ELF、297项SDK恢复和镜像校验通过，
[102项证据](docs/evidence/camera-v209-final/README.md)。v208-r2完整恢复点保留。
v209尚未板测，不宣称A76/硬件CRC已在本板生效或30FPS已达标。
以下历史“待测”表述保留作过程记录，以最新证据为准。

2026-10-29：v207 实机短测通过，[原始日志](docs/evidence/camera-v207-board-20261029/README.md)：
六路输出/检测的 legacy、cold、warm 对照一致；每帧 YOLO 稳态 5.158 FPS，
30 秒 154 帧/154 次推理，错误/清理/fallback 均为零。v207 镜像和 Git 恢复点保留。
[v208 优化与板测流程](docs/a733/IMX214_V208_CPU_PIPELINE_TEST.md)新增能力检测＋自检后的 ARM CRC、
完整覆盖限定的输入模板省略、A76 工作线程及只读频率估算、局部像素/解码优化和显式所有权的双 RGB 缓冲。
最终交付为 **v208-r2**：20组回归、ARM/完整链接、实际ISA/局部O2核验、
296项SDK恢复、镜像回读与Windows独立SHA通过，[封存证据](docs/evidence/camera-v208-final/README.md)。
初始r1的CMake作用域问题已修复，仅保留为未交付记录，使用文档链接的r2镜像。
这是“采集/预处理”与“NPU/显示”两阶段重叠，**仍待上板**，不宣称三阶段完全并行或30 FPS已达标。
旧版下面的“待测”为当时记录；最新状态以上方日志和 v208 测试文档为准。

2026-10-29：v206 用户连续预览/每帧 YOLO 短测通过，[原始记录](docs/evidence/camera-v206-board-20261029/README.md)：
稳态预览 8.667 FPS、每帧检测 2.483 FPS，无错误/清理错误。v206 原镜像与恢复包继续保留。
独立 [v207 软件性能候选](docs/a733/IMX214_V207_SOFTWARE_PERF_TEST.md)已生成并完成主机/ARM/完整链接与镜像校验：等价 slicing8 CRC、输出融合扫描、
所有权租约内减少重复权重扫描、每次硬件后保留权重检查、RAW10 快速计划与 LVGL 脏区刷新。
**仍待上板；30 FPS 是目标，不是已经实现的结果。** NPU 时钟、MMU/DMA 实现、模型精度/尺寸、SPI 12MHz 与摄像头时序不改。

2026-10-29 更新：v205 板测已暴露连续模式第二帧 CSI 地址未就绪/丢行 `0x00220083`，
单帧循环与同输入冷/热 NPU 核验通过，但**连续模式失败，不能称整版通过**。
已生成独立 [v206 修复候选及测试步骤](docs/a733/IMX214_V206_STREAM_REPAIR_TEST.md)：重新提交 DMA 地址、
分离操作/清理错误、确认异常排空时资源隔离。15 组主机回归、完整 ARM 链接、293 项 SDK 恢复、
镜像回读与 Windows 独立 SHA 均通过，[封存证据](docs/evidence/camera-v206-final/README.md)。
**连续采集修复和新 FPS 仍待板测**；保留 v204 通过版与 v205 全部恢复记录。

2026-10-29：v204 已获用户实机通过，[板测日志与核对记录](docs/evidence/camera-v204-board-20261029/README.md)独立保存。
独立的 [v205 性能／受控连续采集候选](docs/a733/IMX214_V205_PERF_STREAM_TEST.md)已生成：CRC／像素计划、同帧一次刷新、
NPU 模型常驻、冷热同输入验收、分段计时与一帧预取。完整构建、293 项 SDK 恢复、14 组主机、ARM／ELF、
镜像回读和 Windows 独立 SHA 均通过，[封存证据](docs/evidence/camera-v205-final/README.md)。
原 v204 镜像、封存和 Git 恢复点保留；以下为当时封存记录，最新 v205 板测结果以上方更新为准。

2026-10-29 开发候选：[v204 摄像头→RAM→NPU YOLO→LVGL/SPI长时循环](docs/a733/IMX214_V204_RAM_LIVE_TEST.md)。
新增独立后台任务、状态/合作停止、同帧序号/CRC底图叠框、零检测清框、逐阶段耗时及真实计数。
模型每会话缓存到官方tmpfs，固定RGB文件复用，不逐帧向SD/UFS导出；NPU执行核/ABI不改，
仅在实验配置额外许可两个精确RAM读路径。保留v203-r2通过版，失败不重置DMA/NPU。
完整构建、旧摄像头回归、新故障测试、原生ARM/ELF、291项SDK恢复、镜像回读和Windows独立SHA均通过；
[封存证据](docs/evidence/camera-v204-final/README.md)。**新镜像尚待上板长稳；这是逐帧停流/检查/恢复的循环，
不是常开CSI/DMA、MP4或30fps。**

2026-10-29 实机更新：[v203-r2 摄像头两次实拍＋SPI显示通过](docs/evidence/camera-v203-board-20261029-r2/README.md)。
用户确认色彩正常；采集代数1→2、两帧不同、发布/恢复错误为零。两份九文件经生产C重放及串口FNV绑定通过。
沿用原v203-r2镜像和SHA，不重编译、不收录人像。**这是两次手动单帧通过，不是连续视频/完整ISP或长稳通过。**

2026-10-29 当前：[v203 偏黑/方向修正与受控重复RAW候选](docs/a733/IMX214_V203_TONE_REPEAT_TEST.md)。
用户v200日志现已证明该次SWOUT1自动开启、RAW发布、SPI预览成功；下面旧版“待测”是当时记录，
以本次新增板测证据为补充，不删除历史。v203保留线性输出，另存RAW10域手动增益/Gamma/180°预览；
仅在上次停流/DMA/寄存器恢复成功且无帧读者时允许再次单帧采集，最多32次，失败不强制重启DMA。
新增曝光/增益只读诊断；不是完整ISP、AE/AWB或连续视频。构建时待板测的记录现由上方实机证据补充。

2026-10-29 追加：[v202 RAW/模型输入逐字节核验与离线图像报告](docs/a733/IMX214_V202_OFFLINE_RAW_REPORT.md)。
从RAW独立重算模型RGB及预览，拒绝旧图配新CRC；统计RAW10数据并绑定v201日志，将YOLO框映射回原图。
生成无网络依赖的HTML/PNG/JSON报告，无日志不造框。四种Bayer与实际C导出器CPU夹具逐字节对照。
**仍是主机工具，不改v200固件或新增镜像；真实RAW、自动供电、摄像头色彩/识别准确率均待板测。**

2026-10-29 追加：[v201 一键单帧 RAW/YOLO/LVGL 主机联调工具](docs/a733/IMX214_V201_HOST_COORDINATOR.md)。
复用 v200，不改固件/启动脚本，不新增2 GiB镜像。两路SSH按实际就绪/CRC/序号/整批消息确认推进，
缺文件/失败/超时立即拒绝，不自动重试或复位；支持下载后离线核对完整单帧目录。
**默认不连接，v200自动供电和真实RAW仍待板测；不是实时视频或图像质量通过。**

2026-10-28 追加：[v200 SWOUT1 启动检查与 RAW 测试镜像](docs/a733/SWOUT1_V200_RAW_TEST.md)。
用户在 v128 上仅手动开启 SWOUT1 后，三色块、LVGL 和七表情恢复，原始摘录保留。
v200 只修改启动层：核对已验证的3.3V配置，控制17->1f单次开启，已开则不写，读回通过才启动；
不在 NuttX 访问 PMIC、不改电压、不擦 UFS。自动 boot.scr 与手工入口相同，原 extlinux 内容保留备份。
内核逐字节复用 v199，551项命令模型、临时ext4安装和v199全主机/ARM回归、镜像回读及独立SHA通过。
**v200 自动启动与真实 RAW 采集仍待用户板测**；不是重新编译的新内核，也不是摄像头出图通过。

2026-10-28 追加：[v199 修复 v197/v198 摄像头失败](docs/a733/IMX214_V199_SIZING_REPAIR.md)。
已归档两版真实日志：v197 把自动派生的 BUF_LEN 当普通可写寄存器，准备和恢复都失败；
v198 该次未通过摄像头脚本启动，缺少同启动交接，零 MMIO 拒绝，未生成后续输入文件。
修复沿用厂商 DMA140 自动尺寸模式，派生字段只读诊断，保留严格帧发布与恢复门禁。
新增 `camera boot-help` 和失败后停止下游提示；不得绕过 handoff，也不是未接屏幕造成。
主机/ARM/完整链接/289 项 SDK 恢复/镜像回读与未改分区校验通过，独立 2 GiB 镜像已生成。
**v199 尚待真实 RAW/显示/NPU 板测**；旧日志、历史文档及 v196/v197/v198 镜像均保留。

2026-10-28 追加：[v198 单帧摄像头/同输入 YOLO/LVGL 底图叠框候选](docs/a733/IMX214_V198_SAME_INPUT_VISION.md)
已生成独立 2 GiB 镜像。真实帧导出新增 model-rgb.bin，`display vision-image` 绑定底图与
推理输入 CRC/序号，检测框整批收齐后显示，并补查 DMA 迟到错误和完成标记原子发布。
主机回归、ARM 编译、完整链接、289 项 SDK 恢复、镜像回读/分区校验通过。
**摄像头单帧和本版识别展示仍待板测；不是实时视频/ISP 验收。** v197、v196 恢复点保留。

2026-10-28 追加：[v197 真实 RAW 单帧/文件导出/SPI 预览候选](docs/a733/IMX214_V197_RAW_FRAME.md)
已生成独立 2 GiB 镜像。272 项单帧主机场景、265 项回调故障注入、ARM 编译、完整链接、
289 项 SDK 恢复、独立 4 MiB 帧池布局、镜像回读/分区摘要验证通过。
新增 `camera capture /data/camera197` 和 `display image /data/camera197/preview.rgb`。
**v197 新增硬件路径尚未板测，不宣称已经出图**；保留 v196 实机包级通过镜像和恢复点。

2026-10-28 追加：[v196 一次真实 MIPI 包接收通过](docs/evidence/camera-v196-board-20261028/README.md)：
ID=`0214`、模式 14/14；已开流，RAW10 包头观察 119 次、FS/FE 各 14 次、错误为零，
停流和控制/接收端恢复成功。已保存原始日志与包级通过恢复点，旧镜像不改。
**没有 RAW 图像，观察次数不是帧数/FPS**；下一步仍是 CSI parser/DMA 真实取帧。
本段未含启动 CRC，精确二进制身份未在本次日志独立复验；历史待测段落保留。

2026-10-28 追加：[v195 板测未通过](docs/evidence/camera-v195-board-20261028/README.md)，
失败在开流之前。已转入 [v196 接收端寄存器修正](docs/a733/IMX214_V196_REGISTER_REPAIR.md)：
修正 DESKEW1 偏移、排除动态结果/计数器的错误恢复，并新增独立语义回归和首错诊断。
完整构建、287 项 SDK 恢复、镜像回读与分区摘要核验通过，已生成独立 2 GiB 镜像；
**尚待 v196 板测，不是实机通过**；历史通过版本、UFS/语音待测状态均保留。

2026-10-28：[v195 MIPI 包接收候选](docs/a733/IMX214_V195_MIPI_PACKETS.md)：
在 v194 真实模式通过版之上接入 J5/MCSIB Combo PHY/协议端口、独立有界开流/停流，
收集新的 RAW10/VC0 包头与 FS/FE/CRC/ECC 状态。主机故障注入/ARM 对象回归通过，
完整链接/镜像回读及分区摘要核验已通过，已生成独立 2 GiB 候选镜像，待上板。
**包级候选，不是 CSI/RAW 帧或实时 YOLO 完成**，摘要与命令见版本说明。
保留 v194 镜像/日志/恢复标签；不写 UFS、不改语音待测状态。

2026-10-28 追加：[v194 一次真实板测通过](docs/evidence/camera-v194-board-20261028/README.md)：
ID=`0214`，153 次模式配置写入、14/14 回读一致，102ms 后恢复且所有错误为零。
保留原始日志/候选镜像/历史待测记录。**这是模式控制验收，不是 CSI/RAW 出图**；
下一阶段仍需独立接收链路与真实帧验证，UFS 引导清理和语音待测状态不变。

2026-10-28：[v194 摄像头待机模式实验](docs/a733/IMX214_V194_STANDBY_MODE.md)：
v193 已取得真实 IMX214 `0x0214` ID，原始串口日志和通过源码另存。
下一步把一次 ID、153 次模式配置、14 项回读和控制恢复合为一条测试命令；
主机故障注入/目标对象回归、完整链接及镜像回读通过，已生成独立 2 GiB 测试镜像。
**待实机模式验收；不启动视频流，CSI/RAW 仍未完成**，摘要/命令见版本说明。
旧通过镜像、语音待测状态和 UFS 引导问题暂停状态保留。

2026-10-28：[v193 摄像头 ID 受控实验](docs/a733/IMX214_V193_ID_EXPERIMENT.md)已生成独立 2 GiB 候选镜像，待上板。
v192 一次板测已通过启动、reset-held 准备/时钟配置切换/恢复，原始日志已保存。
v193 在明确供电保留/名义时钟假设下只做一次固定 ID 读取，读完恢复；不宣称电气实测或出图。
旧通过镜像和语音待测状态不变。

2026-10-28：[v192 摄像头复位保持控制候选](docs/a733/IMX214_V192_CONTROL_TEST.md)：
v191 已有一次 NSH/CPU 自测/只读快照板测；现开发同次启动 RAM 电源交接与
TWI3/MCLK1 控制测试，避免运行时夺取 SCP 的 PMIC 总线。**已恢复构建并生成独立 2 GiB 测试镜像**；
供电保留是假设而非实测，传感器复位不释放，**不是出图版本，尚待板测**。
旧版镜像/语音待测状态保留；摘要、启动脚本和测试命令见版本说明。

2026-10-27：[v191 摄像头资格检查候选](docs/a733/IMX214_V191_TEST_IMAGE.md)：
v190 已校验存档，用户暂缓板测；V1.11 单摄 J5 使用独立 SDK 构建及镜像。
新增板上 CPU 像素自测和只读配置解释，**不是能拍摄的镜像，真实 CSI/DMA 尚未接通**；
任何软件自测或配置匹配都不会绕过电气/时钟/所有权门槛，所有历史版本保留。

2026-10-27：[v190 离线会话与 ASR/TTS 等待优化候选](docs/a733/OFFLINE_SESSION_V190.md)：
采集侧能量端点不再等待 NPU 消费；同命令复用 ASR/vocoder、逐轮清空全部状态，
CPU 声学和 ASR staging 使用临时 A76 亲和性。新增 `aipet audio offline` 显式
多轮本地闭环及文件 ASR 对照；缺 LLM 时不录音，跨任务 fd 隔离，无云/动作降级。
主机/CPU 参考回归通过；**真实 NPU 实时麦克风、完整听音和 3–4 秒目标仍待板测**。

2026-10-27：[v189 流式 TTS 补缓冲候选](docs/a733/TTS_REBUFFER_V189.md)：
用户确认 v188 能启动并听到“你好”，但后半句因 `-32` 中断。
修复生产 PCM 间隔被当成致命错误的应用逻辑；主机延迟生产回归输出完整且与
无延迟基准逐字节相等，**流式完整播报仍待实机验证，ASR/LLM 全链路未通过**。
按用户要求清理两份失败的中间镜像，释放 4 GiB；通过版、必要基线和开发文档保留。

2026-10-27：v185 首次板测未进入 NSH，已转入
[v188 启动加固与定位候选](docs/a733/BOOT_V188_CANDIDATE.md)。
独立初始化线程和逐构造函数/SMP 进度用于排查；**根因与修复效果尚待板测**。
v185、v186/v187 摄像头源码、历史文档和恢复点均保留。

2026-10-27：[v185 通用本地语音闭环候选](docs/a733/GENERAL_VOICE_V185.md)：
真实通用 NPU ASR、CPU Matcha＋NPU vocoder 应用连接、流式 LLM 回复和离线入口；
整合固件与内存布局检查通过，已生成内置 ASR/TTS 模型的 v185 私有测试镜像；
**首次板测启动卡住，未证明完整语音或 3–4 秒首音频达标**，完整测试命令及恢复信息见文档。

2026-10-25：[v182 流式采集/降采样/端点与回放镜像候选](docs/a733/LIVE_VOICE_V182.md)，
到达需要真实FIFO/调度/听音验证的节点；并非完整离线ASR/TTS已完成。

2026-10-25：[v181 ASR/LLM/TTS 延迟优化候选](docs/a733/VOICE_LATENCY_V181.md)：
LLM主机批量prefill约13.5%提速、流式ASR真实CPU输出一致、短句TTS/连续I2S软件路径。
目标NPU通用ASR与RTOS Matcha声学provider仍缺失，不能宣称离线全链路或3–4秒首音频已完成。

2026-10-25：[v180 本地LLM多轮上下文与KV缓存](docs/a733/LLM_CONTEXT_V180.md)。
默认3轮、RAM共享设备会话、可清空/关闭；真实MiniCPM5/Qwen数值回归通过。
本轮不发布新整卡镜像，新增功能与提速幅度仍待统一板测；旧记录全部保留。

2026-10-25：先补齐LLM、通用ASR与本地TTS，再统一进行整机测试，中间不要求反复刷卡。
[v179进展与缺口](docs/a733/OFFLINE_VOICE_V179.md)；
[本地Git保存/回退规则](docs/LOCAL_GIT_RELEASES.md)。新本地provider默认未安装，
不会拿云服务、关键词识别或预录音频冒充离线全链路完成。

2026-10-25：[v178 MiniCPM5与离线语音进展](docs/a733/LLM_V178_MINICPM5_AND_VOICE.md)。
新增受限MiniCPM5-1B、真实UTF-8流式输出、分词/张量目录/提示词KV缓存；真实模型12个
位置全部logits与固定主机参考一致。首段1–2秒仍待板测，不混淆冷加载和预热。
通用ASR三个fp16 NBG已导出并核对A733 CID；仍缺Linux动态golden与RTOS接入，
完整本地ASR/TTS没有全部完成。STM32本轮不改，旧镜像/文档和UFS引导清理暂停状态保留。

2026-10-23：优先推进 [v177 UART4/STM32语音控制链](docs/a733/AGENT_UART4_STM32_V177.md)。
对照下位机提交e83a9cac，将UART4专用于115200二进制控制，旧UART TTS停用、I2S输出保留。
云ASR→安全控制路由/官方Agent工具→MCU ACK→I2S播报已完成主机模拟集成；不是双板实测。
运动默认锁定，须明确台架检查及手动arm；不自动绕过TOF/故障保护。旧UFS启动链清理暂停，
v175已取得一次正常重启自动挂载通过的新用户日志，旧文档/镜像仍全部保留。

2026-10-22 追加：v174 已获模型完整复制/摘要、重启 compare、约 67.58 MiB/s 连续读取
和 15.99 s UFS 冷加载的用户实机证据。当前推进 [v175 开机挂载与 Agent 模型自动选择](docs/a733/UFS_AUTO_V175.md)，
只接管已经验证的现有 FAT 卷，失败保留 SD 回退，不自动格式化/迁移/重做 scratch 写入。
v175 自启动仍须新一轮板测；后续摄像头确认使用 Radxa 13M214（IMX214）。历史记录不删除。

2026-10-22 新补充：v173已取得小文件/备份重启持久性证据，大模型普通cp中断后完整性
未知；不覆盖旧日志/镜像。现在增加 [v174大块存取、串口进度/停止和恢复检查](docs/a733/UFS_BULK_V174.md)，
256KiB复制＋完整SHA256校验，不覆盖旧模型，不再推荐GB文件使用原1KiB cp/128B cmp。
这是软件吞吐候选，非已测UFS3.1满速；旧记录的状态保持历史原文。

2026-10-22 最新UFS阶段：v172固定64KiB写入/flush/回读/还原已获
[用户实机通过证据](docs/evidence/UFS_V172_RUNTIME_20261022.md)。末尾失败是NSH `dd`
把64位seek结果截断为32位，不是UFS scratch失败；v173已修复四处判断。
下一关 [v173独立数据卷、官方FAT32及模型文件测试](docs/a733/UFS_DATA_V173.md)。
仅明确启用后的 `/dev/ufsdata` 可写；整盘 `/dev/ufs0` 保持只读。
新文件系统仍待实机，不开机格式化、不自动挂载或迁移。
旧Linux仅限已确认的Kingston测试盘，SD不在清空授权范围，失败/断电可能损坏测试区。
历史开发记录原样保留，当前综合状态见 [IMPLEMENTATION_STATUS](docs/IMPLEMENTATION_STATUS.md)。

2026-10-19 后续：[v153 本地语音前端](docs/a733/LOCAL_NPU_VOICE_V153.md)。
已取得用户提供的 Model Zoo v1.1.0，新增有界 Kaldi fbank 与 `aipet audio features`；
新版包含 Zipformer 源码/校准集，另已补齐并静态审查上游三份ONNX参考图；
厂商NBG转换与真实对照仍缺，NPU语音推理/全链路尚未完成。

2026-10-19：开始[本地 NPU 语音迁移 v152](docs/a733/LOCAL_NPU_VOICE_V152.md)。
已加入多张量Linux对照工具和22.05k→48k应用播放转换；语音模型在openvela上的
KWS/ASR/TTS全链路尚未完成，缺新版Model Zoo及真实语音golden。现有通过版本保留。

## 一、作品简介

本项目将 openvela/NuttX 原生适配到瑞莎 Radxa Cubie A7Z 开发板。该板搭载
Allwinner A733（2× Cortex-A76 + 6× Cortex-A55）、4 GiB LPDDR4/4X 和
3 TOPS VIP2 NPU。本项目不在 Linux 用户空间模拟 openvela，而是由板载
Boot0/SCP/BL31/U-Boot 直接引导 ARM64 openvela 内核进入 EL1。

当前已经完成可交互 NSH、8 核 SMP、4 GiB 内存、microSD/GPT/FAT、基础板级
外设、FCU760K 双频 Wi-Fi、WPA2/DHCP/IPv4、SSH/FTP/curl/wget，以及 VIP2 NPU
上的 LeNet、YOLOv5s、YOLOv8n 真实硬件推理。基于 openvela 官方
`packages_ai_agent` 的联网桌宠已完成首次加密配置、自启动、DeepSeek/MiMo
后端和连续中文对话；本地 Qwen2.5-1.5B CPU 推理也已达到基础生成检查点。
外接 USB 音频/UVC、I2S PCM 和桌宠硬件表现层仍在开发，不把候选代码声明为
实机完成。

作品面向离线 AI 助手和视觉识别终端：先把 openvela、联网、文件服务和 NPU
执行环境建立在 A733 上，再打通摄像头输入、YOLOv8 前后处理和结果输出。

## 二、选题方向

主赛道：**新硬件适配**。

扩展方向：**AI 硬件产品创新**。A733 VIP2 NPU 已在 openvela 下执行真实模型，
YOLOv8n 六输出和动态输入均已通过真机验证，而不是固定输出回放。

## 三、主要成果

| 子系统 | 状态 | 真机证据摘要 |
| --- | --- | --- |
| ARM64 启动 | 完成 | U-Boot `booti` → BL31 → EL1 → NSH |
| SMP | 8 核基本验证通过 | 6×A55 + 2×A76，逐核身份/亲和性验证；保留栈页对齐修复 |
| 本地 Qwen2.5-1.5B | 基础生成实机通过 | RAM 常驻、6 核工作池、28 层前向、KV cache、中文流式输出和有限多轮已实测；尚未接入桌宠本地回退接口 |
| 控制台 | 完成 | UART0 115200 8N1，输入、termios、Ctrl+C |
| 内存/系统 | 完成 | 4 GiB split-region heap、procfs、`free/ps/uptime` |
| microSD | 完成 | SDMMC0 PIO，12 MHz/4-bit，多块读取 |
| 文件系统 | 完成 | GPT 分区节点，FAT 数据分区读写挂载到 `/data` |
| 板级外设 | 完成 | LED、WDT、THS、I2C2/I2C7、SPI1、风扇 PWM |
| Wi-Fi | 完成，v71 长稳通过 | FCU760K 驱动、标准 WEXT/openvela WAPI、2.4/5 GHz 扫描、WPA2、DHCP、DNS |
| 网络服务 | 完成 | SSH、SCP、FTP、curl、wget、NTP、iperf、可选自启动 |
| NPU | 完成基线 | VIP2 ABI、MMU/DMA/IRQ，LeNet/YOLOv5/YOLOv8 真机运行 |
| 官方 AI Agent | 联网对话实机通过 | `packages_ai_agent`、DeepSeek/MiMo、API Key 板级加密、自动启动和连续中文对话 |
| AI 桌宠路由 | 混合文字链路完成 | 快速规则、官方 Agent、本地 Qwen 回退、回复/表情/动作解析和 UART4 播报 |
| UART4 TTS | v118 实机通过 | `/dev/ttyS4`、TW-TTS UTF-8、sun60iw2 CCU/pinctrl、FIFO 发送和重复播报均通过 |
| I2S 音频 | v150 录音文件回放通过 | ICS43434 默认×5；两次5秒录音、五次48kHz mono WAV→MAX98357A回放，50%音量，用户确认说话/拍手清楚。数字音量0..100；长稳、标准音频框架与ASR/云TTS仍待完成 |
| ST7735/LVGL | v128 产品 UI 与 AIPET 联动真机通过；v129 FPS 真机通过；v130 NPU 框层候选 | 128×160、SPI Mode 0/12 MHz、低有效 CS、RGB565 高字节优先；七类表情、Agent 联动和 29.563 fps 已验证；新增文件 RGB→NPU→LVGL 非阻塞检测框路径待板测 |
| UVC 摄像头 | 进行中 | Type-C/PHY/xHCI 检查点完成，设备枚举尚未完成 |
| Bluetooth BLE | v145五轮扫描/启停、退出重进及轻负载共存实机通过；GATT正向待测 | 官方Bluetooth/Zblue Host + FCU760K USB HCI；`home`运行/负面清理已测，正向发现/读写/通知/业务ACK与双板仍待外设验收 |
| GPU/UFS | 待完成 | 不计入当前完成项 |

详细状态、测试值与限制见 [实现状态](docs/IMPLEMENTATION_STATUS.md) 和
[测试证据](docs/TEST_EVIDENCE.md)。

蓝牙智能家居的当前客户端、服务启动前提、下位机协议和验收流程见
[v145 官方 GATT 客户端](docs/BLE_GATT_CLIENT_V145.md)；保留 v144 实机通过恢复点。
v145后续 [SSH实机回归](docs/evidence/BLE_V145_SSH_RUNTIME_ACCEPTANCE.md) 已记录扫描期间
47包ping零丢失，但默认SSH协商会话曾损坏；显式AES128-GCM为本轮可用的临时客户端
选择，不宣称所有SSH算法/会话已经稳定。

蓝牙暂存恢复点与 ESP32 后续事项见 [v145 暂停记录](docs/evidence/BLE_V145_PAUSE_FOR_ICS43434.md)。
当前转向 [ICS43434 合并模块 v146](docs/a733/ICS43434_V146.md)，旧录音与失败证据全部保留。
v146首次实机返回-EIO的定位与后续候选见 [v147计数与波形](docs/a733/ICS43434_V147_COUNTER.md)。
后续用户已完成v147首次有效录音，文件/时序分析及限制见
[基础录音通过记录](docs/evidence/ICS43434_V147_RUNTIME_ACCEPTANCE.md)；原始声音留在本地，不上传Git。
后续开发 [v148左声道与可调增益](docs/a733/ICS43434_V148_VOICE_GAIN.md)，
原始采集保持不变，新版全量构建/镜像校验已通过，语音入口仍待上板验收；
[镜像哈希与交付记录](docs/evidence/i2s-v148/build-summary.md)。
用户随后实测确认×5合适，已在[v149源码](docs/a733/ICS43434_V149_DEFAULT_GAIN.md)
设为默认；现有v148镜像仍需显式指定,5，本次未重新生成镜像。
后续转向[v150 I2S扬声器WAV播放](docs/a733/I2S_SPEAKER_V150.md)，
保留默认麦克风×5及UART TTS；全量构建/镜像验证已通过，
原[交付与哈希](docs/evidence/i2s-v150/build-summary.md)保留。
后续用户实机已通过两次录音、五次48kHz mono WAV回放，音量50%，确认拍手/说话清楚；
现有数字音量0..100（默认25%，重启不保留）无需新镜像。
见 [v150实测与边界](docs/evidence/I2S_SPEAKER_V150_RUNTIME_ACCEPTANCE.md)。
后续 [v151应用层接入](docs/a733/AIPET_AUDIO_V151.md) 提供 `aipet audio`、
可保存的音量/增益/输出选择、本地录放和官方ASR/Agent/TTS链路；
本轮主机已回归，新应用/云端语音仍需板测，不覆盖v150已通过版本。
v151候选构建、布局、SDK恢复和2GiB镜像校验已通过，
见 [交付与哈希](docs/evidence/i2s-v151/build-summary.md)。

本地 LLM 最新合并开发说明和测试命令见 [v94 生成链路](docs/a733/LLM_V94_MERGED_GENERATION.md)。
后续 attention 数值对照和单轮对话候选见 [v95 ChatML](docs/a733/LLM_V95_ATTENTION_AUDIT_CHAT.md)。
合并开发 v96 加入流式输出、有限多轮历史和协作停止，原候选说明及板端验收步骤见 [v96 合并开发](docs/a733/LLM_V96_STREAM_HISTORY_STOP.md)。
后续用户日志已验证 v96 中文流式回答与两轮记忆；运行中停止及 Ctrl+C 清理仍待验收/修复。正规 `llm` 应用入口及已知边界见 [v97 应用封装](docs/a733/LLM_V97_APP_FRONTEND.md)。
v97 用户中文多轮测试也已通过；桌宠开始按 Linux 原代码移植，首轮业务核心与 69 个解析行为对照已完成，完整进度和后续接口见 [桌宠移植阶段 1](docs/a733/AIPET_LINUX_PORT_STAGE1.md)。
本地 Qwen 与桌宠路由的正式库级接入、配置命令和板测流程见
[本地 LLM Agent 接入](docs/a733/AIPET_LOCAL_LLM_AGENT.md)。

## 四、仓库结构

```text
contest2026_274_Dogking/
├── app/a733home/                                    # 官方 GATT 客户端与共享报文协议
├── board/a733-cubie-a7z/
│   ├── openvela-overlay/
│   │   ├── vendor/allwinnertech/chips/a733/        # A733 芯片与驱动
│   │   ├── vendor/allwinnertech/boards/a733/       # Cubie A7Z 板级代码
│   │   ├── apps/system/a733wifi/                    # Wi-Fi 管理命令
│   │   ├── apps/system/a733services/                # 自启动服务管理
│   │   ├── apps/system/a733ftpd/                    # FTP 服务封装
│   │   ├── apps/system/a733npu/                     # NPU 用户命令
│   │   ├── apps/system/aipet/                       # 官方 Agent 桌宠路由
│   │   ├── apps/system/aipetllm/                    # 本地 Qwen2.5 推理
│   │   ├── apps/system/aipetasr/                    # 本地 ASR 接口骨架
│   │   ├── apps/system/a733display/                 # ST7735/LVGL 测试与 UI 入口
│   │   └── apps、nuttx 的必要兼容文件                # 逐文件 manifest 映射
│   └── README.md                                    # 板级说明
├── docs/
│   ├── IMPLEMENTATION_STATUS.md                     # 功能矩阵和边界
│   ├── TEST_EVIDENCE.md                             # 真机验收证据
│   ├── ARTIFACTS.md                                 # 镜像/模型校验与获取规则
│   ├── CAMERA_YOLO_LVGL_PIPELINE.md                  # 摄像头、NPU 与显示分阶段门禁
│   └── UPSTREAM_PLAN.md                             # 公共仓拆分计划
├── patches/                                         # 公共仓可审查兼容补丁
├── tools/build-a733.sh                              # 完整、可恢复的构建入口
├── logs/                                            # 官方工具导出的真实 AI 日志
├── contest2026_274_Dogking.xml                      # 仓库 manifest 与 linkfile
└── openvela.xml                                     # 大赛官方基线 manifest
```

Wi-Fi 和其他组件的 NuttX 内核层、openvela 公共组件层及板级私有层边界，见
[`docs/OPENVELA_COMPONENT_BOUNDARIES.md`](docs/OPENVELA_COMPONENT_BOUNDARIES.md)。
AI 桌宠的产品层强制采用“openvela 官方能力优先”架构，具体 API 选择、Linux 原型
迁移映射和自动边界检查见
[`docs/AI_PET_OPENVELA_FIRST_ARCHITECTURE.md`](docs/AI_PET_OPENVELA_FIRST_ARCHITECTURE.md)。
当前端到端状态和各分支完成边界见
[`docs/a733/AIPET_CHAIN_OVERVIEW_20260920.md`](docs/a733/AIPET_CHAIN_OVERVIEW_20260920.md)。
ST7735 接线、官方驱动边界、构建产物和首轮板测步骤见
[`docs/ST7735_LVGL_PORT.md`](docs/ST7735_LVGL_PORT.md)。
正式桌宠场景、Agent 事件边界和 v128 测试证据见
[`docs/LVGL_PET_UI.md`](docs/LVGL_PET_UI.md)。
摄像头、YOLOv8 和 LVGL 实时显示的模型契约、阻塞边界和分阶段验收见
[`docs/CAMERA_YOLO_LVGL_PIPELINE.md`](docs/CAMERA_YOLO_LVGL_PIPELINE.md)。
后续开发者或 Agent 接手前，请先阅读
[`docs/PROJECT_HANDOFF_20260920.md`](docs/PROJECT_HANDOFF_20260920.md)，其中包含
当前恢复点、完整构建/测试流程、失败方案、未完成任务和安全边界。

模板中的 hello app、quickapp 和示例日志已删除。本作品只使用板级适配形态。

## 五、获取完整 openvela 工作区

推荐 Ubuntu 22.04/24.04 或 Windows 11 + WSL2 Ubuntu。以下操作均在 Linux/WSL
终端执行；不要把 Linux 构建工具直接换成 Windows 工具。

```bash
mkdir -p ~/openvela-contest
cd ~/openvela-contest

repo init \
  -u https://github.com/open-vela/contest2026_274_Dogking.git \
  -b dev-ai-contest-2026 \
  -m contest2026_274_Dogking.xml
repo sync -c -j8
```

同步后，公开协作仓库在 `contest2026_274_Dogking/`，其代码由 manifest 的
`linkfile` 自动映射到外层 `vendor/`、`apps/` 和 `nuttx/`。开发者只应提交本
协作仓库；公共仓修改的长期合入方式见 `docs/UPSTREAM_PLAN.md`。正式向组委会
提交前，需要再切换回官方比赛仓库及其指定分支。

## 六、构建

在完整工作区根目录执行：

```bash
cd ~/openvela-contest
bash contest2026_274_Dogking/tools/build-a733.sh
```

该入口还会临时应用官方 Agent、readline 和 ARM64 兼容补丁，并在退出时恢复公共
仓文件；不能用一条裸 `nuttx/tools/build.sh` 等价替代完整产品构建。

增量构建：

```bash
bash contest2026_274_Dogking/tools/build-a733.sh --incremental
```

主要产物：

```text
cmake_out/cubie-a7z_nsh/nuttx.bin
cmake_out/cubie-a7z_nsh/nuttx
cmake_out/cubie-a7z_nsh/System.map
```

`nuttx.bin` 带 Linux ARM64 Image header，必须由 U-Boot `booti` 启动；不能用
`go 0x40200000` 进入 AArch64 内核。

完整的依赖、非标准目录变量、镜像封装与独立校验命令见
[`docs/BUILD_AND_IMAGE.md`](docs/BUILD_AND_IMAGE.md)。

## 七、SD 卡启动与部署

当前开发板是 4 GiB、未安装 UFS 的 Cubie A7Z，优先从 microSD 启动。Boot0、
SCP、BL31 和 U-Boot 复用瑞莎官方可工作的启动固件，openvela 已不依赖 Debian
rootfs。比赛仓库不提交 2 GiB/4 GiB 镜像，避免 GitHub 大文件限制；镜像校验值
和发布规则见 `docs/ARTIFACTS.md`。

已经具备 A733 启动固件的 SD 卡可直接替换 openvela 分区中的：

```text
/boot/openvela/a733/Image
```

替换内容为构建出的 `nuttx.bin`。extlinux 条目参考：

```text
LABEL openvela A733
  KERNEL /boot/openvela/a733/Image
  FDT /boot/dtb/sun60i-a733-cubie-a7z.dtb
```

串口：115200 8N1，无硬件流控。预期最终出现：

```text
NuttShell (NSH)
nsh>
```

已有基础镜像时，可以使用 `tools/package-a733-kernel-image.sh` 生成一个不覆盖
原镜像的新候选文件，再用 `tools/verify-a733-image.sh` 检查 ext4、GPT 和内嵌
内核哈希。具体命令见 `docs/BUILD_AND_IMAGE.md`。

## 八、上板验收

基础系统：

```text
uname -a
free
ps
mount
ls /dev
cat /dev/a733-hw
sensortest -n 3 temp0
```

存储：

```text
dd if=/dev/mmcsd0 of=/dev/null bs=512 count=1
dd if=/dev/mmcsd0 of=/dev/null bs=16384 count=64
ls /data
```

Wi-Fi 与网络（密码只在板端交互输入，禁止写入源码）：

```text
wifi scan
wifi connect <SSID>
ifconfig
ping -c 4 <LAN_GATEWAY>
nslookup example.com
curl http://example.com/
```

NPU（A7PM 文件需按 `docs/ARTIFACTS.md` 合法准备并复制到 `/data/npu`）：

```text
cat /dev/npu0
echo selftest > /dev/npu0
echo apitest > /dev/npu0
echo prepared=/data/npu/lenet.a7pm > /dev/npu0
cat /dev/npu0
```

联网桌宠：

```text
aipet init                     # 仅首次运行
aipet status
aipet route "你好"
aipet ask "请只回答：启动测试成功"
```

初始化时选择 DeepSeek/MiMo、模型并隐藏输入 API Key。完整桌宠链路与 UART/I2S
候选测试分别见 `docs/a733/AIPET_CHAIN_OVERVIEW_20260920.md` 和
`docs/a733/AUDIO_UART_I2S_STAGE.md`。

USB 摄像头当前只能用于诊断，不能作为已完成验收项：

```text
cat /dev/a733-uvc
echo probe > /dev/a733-uvc
dmesg
```

## 九、AI Coding 使用说明

本适配使用 AI 完成资料归纳、启动阶段拆解、寄存器对照、驱动骨架、故障假设、
构建脚本、真机日志分析、回归清单和文档整理。所有硬件结论都以串口日志和真机
命令结果验证；“能编译”不被当作“适配完成”。

AI 辅助的典型闭环：

1. 从原理图、A733 BSP、Linux/U-Boot 和手册中提取可验证假设；
2. 一次只增加一个硬件检查点，并为轮询设置超时；
3. WSL 构建、生成新版本镜像，不覆盖已验证恢复点；
4. 用户在真机执行命令并提供完整串口日志；
5. AI 根据寄存器、错误码和前后版本差异定位；
6. 通过回归后再合并下一批功能。

官方归集工具导出的原始会话位于 `logs/`。这些 JSONL 不应手工修改；不提交的
会话应整段删除，而不是删改单条事件。

## 十、安全、许可与隐私

- 项目源码使用 Apache License 2.0；第三方代码继续遵循其原许可证。
- 不提交 Wi-Fi 密码、SSH/FTP 密码、私钥、API key、个人路径或账号令牌。
- 不重新分发许可不明的 Allwinner VIPLite/NPU 编译器、闭源库或固件镜像。
- 模型和大镜像使用校验和、来源和可重复生成步骤描述，不直接塞入 Git 历史。
- 所有未完成子系统均在文档中明确标注，不以检查点冒充完整功能。

## 十一、已知限制和后续计划

1. 外部 USB 设备尚未枚举；v114 Combo PHY/xHCI ring 是未实机验收候选，后续再
   打通 UAC/UVC → PCM/frame → ASR/YOLOv8。
2. Bluetooth HCI、Imagination GPU、UFS 尚未适配。
3. Wi-Fi 已可用但仍需长稳、断线重连、并发吞吐和 DFS 压测。
4. NPU 当前采用官方 Linux VIPLite golden trace → A7PM → openvela 执行路线；
   通用 NBG 编译/链接仍依赖官方授权工具。
5. 公共 `apps`/`nuttx` 兼容修改需要按官方流程拆分并提交对应仓库的
   `dev-ai-contest-2026` PR。
6. 本地 Qwen 已注册到桌宠 `LocalRouteBackend`，但首次从 SD 载入约 1.1 GiB
   模型耗时较长，且当前为 CPU 贪心解码；MAX98357A、INMP441、屏幕表情和
   动作执行仍需实机与产品层验收。

## 十二、官方大赛资料

- [大赛总览](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/contest_overview.md)
- [参赛代码提交指南](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/code_submission_guide.md)
- [AI Coding 日志手册](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/ai_coding_log_guide.md)
- [新硬件适配指引](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/hardware_porting/hardware_porting_track_guide.md)

## 许可证

Apache-2.0。详见 [LICENSE](LICENSE)。
