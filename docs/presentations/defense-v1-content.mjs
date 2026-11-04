// Content is intentionally separate from layout so subsequent iterations can update evidence.
const S='PROJECT DEFENSE · 项目答辩';
const core='docs/PROJECT_FULL_SUMMARY.md';
const manual='docs/PROJECT_MANUAL_V1_0.md';
const delta='docs/POST_PRELIMINARY_IMPROVEMENTS.md';
const audit='docs/a733/PRODUCT_V1_0_AUDIT.md';
const repo='https://github.com/open-vela/contest2026_274_Dogking';
const two=(title,a,bodyA,b,bodyB)=>[S,title,'01',a,bodyA,'02',b,bodyB];
const flow=(title,nodes,footer)=>[S,title,...nodes.flatMap(([a,b],i)=>[String(i+1).padStart(2,'0'),a,b]),footer];
const four=(title,items,footer)=>[S,title,...items.flatMap(([a,b],i)=>[String(i+1).padStart(2,'0'),a,b]),footer];
const table=(title,values,footer,template=14)=>({template,title,texts:[S,title,footer],table:values});

export const slides=[
  {
    template:1,title:'Vela伴星',
    texts:['Vela伴星\n端云协同 AI 桌面交互终端','2026 首届 openvela AI 硬件开发者大赛','Dogking  ·  hxy299'],
    talk:'各位评委老师好，我是 Dogking 的 hxy299。Vela伴星以瑞莎 A7Z 为载体，把原生 openvela 平台适配，推进到语音、视觉、表情和下位机统一编排。接下来我会分别介绍产品用途和核心技术，并展示有证据支持的进展。',
    sources:[core,'../../DogkingA733-submission-docs/SUBMISSION_TECHNICAL_REPORT.md'],seconds:18,
  },
  {
    template:4,title:'答辩内容',
    texts:['答辩内容','01　产品与场景','用户需求与桌面交互','02　整机与平台','原生 openvela 与适配边界','03　语音与路由','本地计算与联网增强','04　视觉与执行','识物反馈与受控动作','05　验证与交付','实机记录与恢复机制','06　后续迭代','从工程样机到整机验收'],
    talk:'这次汇报分成六部分。产品方案解释用户为什么需要它，技术方案解释系统如何运行。最后给出当前验证状态和下一阶段计划，避免把不同版本的结果混在一起。',sources:[core],seconds:12,
  },
  {
    template:18,title:'产品定位与用户需求',
    texts:[S,'产品定位与用户需求','需求','方案','桌面轻陪伴','希望用自然语音交流，并从表情和声音获得及时反馈。','可控的 AI 交互终端','用户选择本地或联网模式，以声音、屏幕和执行器回应。','面向桌面交互与端侧 AI 开发者，当前为持续迭代的工程样机'],
    talk:'产品主要面向桌面轻陪伴和端侧 AI 开发者。用户希望交流更自然，同时能明确控制数据是否上云。我的方案是在本地保留计算能力，通过小屏、扬声器和下位机形成可观察的反馈。目前它是一套可复现的工程样机，还不是量产产品。',sources:[core,manual],seconds:25,
  },
  {
    template:19,title:'三个核心使用场景',
    texts:[S,'三个核心使用场景','聊','看','动','日常交流','持续等待下一轮，保留有限上下文，并联动表情。','语音识物','用户提出识物请求后，采集新画面并播报可信类别。','受控执行','明确指令匹配白名单，经 UART4 请求 STM32 执行。','语音服务采用半双工，机械动作需要用户显式授予会话权限'],
    talk:'核心体验有三类：闲聊时能接续有限上下文，识物时真正拍摄当前画面，控制下位机时先判断明确意图和权限。这里特意把聊天与硬件动作分开，避免模型一句生成文本就驱动电机。',sources:[core,manual],seconds:24,
  },
  {
    template:26,title:'实机交互与显示反馈',
    texts:[S,'实机交互与显示反馈','历史 LVGL 与 RGB 板测照片','看得见的系统状态','ST7735 128×160，正常颜色与 LVGL 桌宠画面。','情绪事件联动界面，区分等待、思考和回复。','I2S 数字麦克风与扬声器承担真实录放音。','照片来自 v126 显示板测，完整语音和视觉能力以各自版本证据为准'],
    photo:'docs/evidence/st7735-v126/02-lvgl-face.jpg',
    talk:'这张照片是历史显示板测的原件。屏幕不是静态宣传图，而是开发板驱动的 LVGL 场景。之后项目补上了表情事件、麦克风输入和 I2S 播报。照片只用于证明当时的显示结果，不将它当成最新整机验收。',sources:['docs/evidence/st7735-v126/README.md',core],seconds:22,
  },
  {
    template:16,title:'整机架构与设备分工',
    texts:[S,'整机架构与设备分工','A733 · openvela','产品路由与资源协调','语音输入输出','ICS43434 录音，MAX98357 播放，端点检测保留开头音频。','本地与云计算','CPU LLM、NPU ASR，或显式选择官方云 ASR／Agent／TTS。','视觉与表情','IMX214 采集，YOLO 检测，LVGL 同帧画面与识别框。','下位机与存储','UART4 对接 STM32，UFS 存放模型，SD 保留备用资产。','共享 NPU 和显示资源由明确所有者使用，视觉与语音按受控顺序交接'],
    talk:'A733 负责理解和编排，STM32 负责执行器。麦克风、屏幕和摄像头都由 openvela 直接管理。因为 ASR、声码器和 YOLO 共用 NPU，系统必须明确资源所有权，识物前退出语音会话，处理结束后再恢复监听。',sources:[core],seconds:28,
  },
  {
    template:12,title:'原生平台适配与官方组件复用',
    texts:[S,'原生平台适配与官方组件复用','本项目的适配工作','ARM64 内核直接启动与八核 SMP\n4 GiB 分区内存及专用保留区\nWi-Fi、SD／UFS、UART、SPI、I2S\nVIP2 NPU 的内存、提交与校验\nIMX214 专用 RAW 采集与图像处理\n产品路由和整机生命周期协调','复用与保留的基础','厂商 Boot0、SCP、BL31 与 U-Boot\nopenvela／NuttX 的系统与网络组件\n官方 packages_ai_agent 云对话框架\nLVGL 9.1、mbedTLS、libssh\nGGML、fbank、静态 ONNX Runtime\n厂商模型工具与独立模型许可证'],
    talk:'项目主赛道仍然是新硬件适配。产品固件由引导链直接启动 openvela，不是在 Linux 里模拟。原创工作覆盖 SoC、外设和产品协调；同时明确复用了哪些官方及开源组件。Linux 只承担厂商参考推理和模型验证。',sources:[core,'../../DogkingA733-submission-docs/SUBMISSION_TECHNICAL_REPORT.md'],seconds:28,
  },
  {
    ...table('端云路由与数据边界',[
      ['环节','本地模式','联网模式'],
      ['语音识别','NPU 通用 ASR','官方云 ASR'],
      ['对话生成','CPU MiniCPM5-1B','官方 Agent＋DeepSeek／MiMo'],
      ['语音合成','CPU 声学＋NPU vocoder','官方云 TTS'],
      ['使用条件','模型就绪，语音不上传云端','网络、时钟与独立凭据就绪'],
      ['切换策略','local 明确锁定本地','cloud 强制云端，auto 按轮判定'],
    ],'自动模式仅在语义分发前允许失败回退，动作已下发或音频已播出后不重放',17),
    talk:'路由覆盖识别、对话和合成整条链路。local 明确使用本地，cloud 明确要求云端，auto 在每轮开始前检查条件。为了避免重复动作，只有还没分发语义时才能回退，已经执行或播出的内容不能再重放。最新云语音仍待真实 API 验收。',sources:[core,'docs/a733/PRODUCT_V1_0.md'],seconds:28,
  },
  {
    template:13,title:'离线语音的模型执行分工',
    texts:flow('离线语音的模型执行分工',[
      ['I2S 录音','48 kHz 采样与端点'],['NPU 识别','通用 ASR 与跨块状态'],['CPU 对话','MiniCPM5 与有限历史'],['本地播报','CPU 声学＋NPU 声码器'],
    ],'PCM 队列连接合成与 I2S 播放，首短语可与后续文本生成重叠'),
    talk:'离线链路中，识别的主要模型跑 NPU，语言模型跑 CPU，TTS 声学模型跑 CPU，再由 NPU 声码器生成波形。这个分工很重要：不能把整个 TTS 或 LLM 都称为 NPU 加速。当前优化重点是跨阶段重叠和减少重复初始化。',sources:[core],seconds:27,
  },
  {
    template:6,title:'常驻语音与自然回复的改进',
    texts:two('常驻语音与自然回复的改进','持续等待与多轮','显式启动后只做一次准备\nREADY 后等待用户说话\n保留约 400 ms 开头音频\n约 700 ms 静音端点\n空白和过长话语不触发动作\n协作停止后等待资源退出','上下文与完整播报','默认保留三对 RAM 历史\n完整成功回复才写入历史\n按完整短语组织播放队列\n区分临时断流和真正结束\n未知英文不丢掉后续整句\n情绪风格与 LVGL 表情联动'),
    talk:'前期整机测试暴露了等待 MIC ON、多轮胡说以及播报截断的问题。现在常驻服务复用已初始化模型，保留开头音频，并且只将完整成功的回复写入上下文。TTS 改为完整短语队列，同时修正尾句和未知词处理。当前仍是半双工，不支持播放中语音打断。',sources:[core,delta,'docs/a733/VOICE_QUALITY_V216.md'],seconds:30,
  },
  {
    template:11,title:'本地 LLM 的体积与热路径',
    texts:[S,'本地 LLM 的体积与热路径','1B','MiniCPM5 Q4_K_M','688,065,920 字节模型','RAM','权重与工作区常驻','减少重复读盘和分配','KV','有限上下文与前缀缓存','局部多核工作池','口径','历史用户反馈首字约 2 秒。整机首音需计入 ASR 收尾与 TTS，3–5 秒仍是目标。'],
    talk:'语言模型从早期 Qwen 1.5B 切换到约 688 MB 的 MiniCPM5-1B。权重加载到 RAM 后常驻，复用工作区、KV 前缀和多核池。历史首字已到约两秒量级，但它不等于用户说完后听到第一声的时延，后者还必须包含 ASR 和 TTS。',sources:[core,delta],seconds:26,
  },
  {
    template:9,title:'摄像头识物与同帧反馈',
    texts:[S,'摄像头识物与同帧反馈','新拍画面','Radxa 13M214／IMX214，以专用 RAW 采集路径获取当前画面。','处理与推理','RAW 处理、RGB 与 letterbox，动态输入 YOLO，校验全部六路输出。','同帧显示','画面、检测框和请求序号关联，零检测清框，不复用上一帧结果。','类别播报','依据 COCO80 类别和置信度组织中文反馈，不让聊天模型猜图。'],
    talk:'视觉业务从一句识物请求开始，真正采一帧新画面，再送入 YOLO。底图和识别框绑定同一帧，请求过期或失败就不发布旧结果。播报依据检测类别和数量生成，能力边界是目标检测，不是通用视觉大模型。',sources:[core,'docs/evidence/camera-v204-board-20261029/README.md'],seconds:27,
  },
  {
    template:27,title:'视觉热路径优化的板测记录',
    chart:{categories:['v206','v207','v208-r2'],values:[2.483,5.158,7.733]},
    texts:[S,'视觉热路径优化的板测记录','','2026-10-29 不同版本短时板测。v208-r2 为双槽流水线，不是 v1.0 复测或 30 FPS 达标。','历史板测数据',''],
    talk:'这组数据来自不同版本的短时板测。每帧检测稳态从 2.483 提高到 5.158，再到双槽的 7.733 FPS。工作主要集中在等价 CRC、融合扫描、模型准备复用和像素处理，而没有删掉六路输出检查。v208 这次测试仍在 A55 上执行，硬件 CRC 和 A76 没有资格通过，不能把它们算作这组提升原因。',
    sources:['docs/evidence/camera-v206-board-20261029/README.md','docs/evidence/camera-v207-board-20261029/README.md','docs/evidence/camera-v208-board-20261029/README.md'],seconds:30,
  },
  {
    template:23,title:'STM32 执行与动作安全边界',
    texts:[S,'STM32 执行与动作安全边界','明确规则','运动指令先走严格白名单与数值边界。聊天模型生成的动作标签不直接驱动硬件。','会话权限','UART4 以 115200 通信，显式 arm session 后本次会话可多次操作，不需逐条开启。','故障撤权','失联、故障、NAK 或 ACK 超时撤销权限。不自动清故障、不重发运动、不重连后自动 arm。','真实反馈','区分接收 ACK 和物理运动完成。真实执行器、行程和机械安全仍需单独验收。','半双工服务不能承担播放期间的语音急停，必须保留物理停机能力'],
    talk:'UART4 已从串口 TTS 转给 STM32，播报改由 I2S 完成。会话使能解决了反复 arm 的操作负担，但不能牺牲撤权机制。主机协议和故障测试已经覆盖，真实电机和舵机还需要独立验收，尤其不能把 ACK 当作已到位。',sources:[core,manual,audit],seconds:27,
  },
  {
    ...table('初赛之后的主要增量',[
      ['方向','初赛报告','决赛前推进'],
      ['语音输入输出','UART TTS，I2S 未收敛','ICS43434 采音，I2S 与本地 TTS 播报'],
      ['本地对话','Qwen 1.5B，冷读慢','MiniCPM5 1B、常驻与有限上下文'],
      ['视觉显示','屏幕条纹，相机未闭环','正常表情、IMX214、YOLO 同帧框'],
      ['产品编排','命令式演示为主','常驻多轮、端云路由、识物与 MCU'],
      ['存储交付','已有构建与镜像机制','UFS 模型读取、回归与恢复演练'],
    ],'八核、4 GiB、Wi-Fi、官方云 Agent 与基础 NPU 已在初赛中存在，不重复计为新增'),
    talk:'与初赛相比，增量主要在输入输出和整机编排。基础八核、网络和 NPU 在初赛就已存在，这里不重复计算。后续补齐录放音、较小的本地模型、真实相机和同帧检测，并把它们组织成统一的产品路由。',sources:[delta,'../../DogkingA733-submission-docs/SUBMISSION_TECHNICAL_REPORT.md'],seconds:26,
  },
  {
    template:18,title:'产品价值与开源复用',
    texts:[S,'产品价值与开源复用','用户','开发者','可观察、可选择的交互','用户明确选择本地或联网，屏幕和声音反馈当前状态，机械动作保留权限边界。','可复现的端侧 AI 参考','复用 BSP、模型契约、测试与镜像，追溯版本并继续迭代。','当前优先完成工程样机整机验收，量产成本、外壳和用户研究留待后续验证'],
    talk:'对用户，价值是更直接的语音交互和可选择的数据路径。对开发者，价值是能从 BSP 一直追溯到模型与镜像的开源参考。目前还没有量产成本和用户规模数据，我会先完成可靠性与整机体验验收，再讨论产品化。',sources:[core,manual],seconds:23,
  },
  {
    ...table('当前验证状态与整机边界',[
      ['范围','已经形成的证据','仍需完成'],
      ['基础平台／外设','历史启动、网络、录放和 UFS 板测','新镜像冷启动与长稳复测'],
      ['相机／YOLO／SPI','历史实拍及短时同帧检测','本候选长稳与性能验收'],
      ['语音／产品路由','23 项综合软件门禁通过','真实多轮、整句、端云切换'],
      ['STM32／云语音','协议／PTY，接口与配置实现','机械动作与真实云 API'],
      ['首音时延','分段计时与优化入口','统一声学起音测量，目标 3–5 秒'],
    ],'本版为 v1.0 软件审计候选。宿主模拟、历史板测与本候选整机验收分别记录'),
    talk:'当前成果是多项历史实机能力，加上新一版完整的软件集成与可恢复交付。软件门禁和镜像回读已经通过，但还不能替代新候选的整机验收。下一轮将重点测连续对话、完整播报、真实云 API 和机械动作，并用相同口径量首音。',sources:[audit,'docs/evidence/product-v1-audit/summary.json'],seconds:30,
  },
  {
    template:10,title:'后续迭代的优先级',
    texts:four('后续迭代的优先级',[
      ['整机语音验收','确认多轮、上下文和整句中英播报，统一测量首音。'],
      ['稳定与性能','测冷／热启动、长稳、温度和资源释放，再针对真实瓶颈优化。'],
      ['执行器与云路由','真实 STM32 动作和失联撤权，配置独立凭据测试联网三段。'],
      ['后续产品化','改进结构与交互，继续评估全双工、BLE 双板与标准媒体接口。'],
    ],'' ).slice(0,-1),
    talk:'后续迭代先保证回答完整和连续多轮，再用真实分段时延找瓶颈。同时验证执行器撤权和联网链路。全双工、BLE 双板和标准媒体接口放在后续路线图中，不作为当前已完成能力。',sources:[core,audit],seconds:20,
  },
  {
    template:20,title:'整机验收的演示顺序',
    texts:[S,'整机验收的演示顺序','01','02','03','04','本地多轮','启动就绪，连续对话并验证记忆','完整播报','长句与英文朗读，停止后可恢复','实时识物','新拍物体，同帧框与播报一致','端云与执行','验证切网、会话权限和故障撤权','连续实机操作验证版本、结果与恢复状态，保留可追溯日志'],
    talk:'我将按这个顺序录制下一轮整机证据：先本地多轮，再完整播报，然后新拍识物，最后测试切网和受控执行。每个环节保留版本和日志。这样评委能看到一段连续使用过程，也方便定位问题，而不是只看到孤立命令成功。',sources:[manual,audit],seconds:22,
  },
  {
    template:31,title:'感谢聆听',
    texts:['THANKS','感谢各位评委老师聆听，欢迎提问与交流','Vela伴星  ·  Dogking / hxy299','原生 openvela 平台适配与端侧 AI 桌面交互'],
    talk:'Vela伴星当前的成果，是可追溯的平台适配和持续完善的端侧 AI 交互参考。我会继续用实机证据完成整机验收，也欢迎评委和开发者提出改进建议。谢谢。',sources:[core,repo],seconds:15,
  },
  {
    ...table('附录 A：关键接线与接口',[
      ['接口','A7Z 物理引脚／GPIO','模块对应'],
      ['I2S 时钟','12 / PB5，35 / PB6','BCLK／SCK，LRCLK／WS'],
      ['I2S 数据','40 / PB7，38 / PB8','扬声器 DIN，麦克风 SD'],
      ['SPI 时钟／数据','23 / PD11，19 / PD12','SCL，SDA／MOSI'],
      ['SPI 控制','24 / PD10，22 / PL5，7 / PB0','CS，DC，RESET'],
      ['UART4／相机','16 / PJ24，18 / PJ25；J5','TX→STM32 RX，RX←TX；13M214'],
    ],'所有模块共地，GPIO 为 3.3V TTL。模块供电按资料，差分扬声器端不能接地'),
    appendix:true,talk:'I2S 的 PB5 是 BCLK，PB6 是 LRCLK，之前无声就出现过这两条线接反。UART4 使用 115200、8N1。相机走 V1.11 板的 J5，J3 是 PCIe。供电和共地必须按模块资料核对。',sources:[manual],
  },
  {
    template:12,title:'附录 B：ASR 与 TTS 实现边界',
    texts:[S,'附录 B：ASR 与 TTS 实现边界','通用识别与流式状态','48 kHz PCM 转 16 kHz 特征输入\nNPU encoder／decoder／joiner\n跨块状态与全部 Tensor 输出验证\n关键词模型与通用 ASR 分开管理\n端点、开头保留和过长话语检查\n近远场、噪声和积压继续实测','合成执行与播放','文本前端、词典与未知词处理\nCPU ORT 运行声学模型\nNPU vocoder 生成实际波形\n完整短语、尾句与连续 PCM 队列\nI2S 音量可调，麦克风默认 x5\n无多情感声学模型与播放中打断'],
    appendix:true,talk:'如果进一步展开语音实现，识别侧需要维护跨块状态，合成侧需要区分声学模型与声码器。情绪语言和表情属于产品层，不能据此宣称已经训练了多情感声学模型。',sources:[core],
  },
  {
    template:11,title:'附录 C：可恢复交付与证据',
    texts:[S,'附录 C：可恢复交付与证据','23','综合软件门禁','明确区分模拟与真实硬件','4,194','恢复包源码文件','已在新目录还原并带 Git 历史','398','外部引用核验','大模型与镜像保留哈希引用','恢复点','a733-v1.0-v217-audited-20261031；审计镜像、旧版镜像与历史开发文档均保留'],
    appendix:true,talk:'交付包括版本化代码、构建和镜像回读、证据摘要及恢复材料。恢复包已在新目录实际还原。大模型和镜像通过外部哈希引用，因此整理文件时不能随意删除这些被引用的大文件。',sources:[audit,'out/product-v1-audit-checkpoint-r1.log','out/product-v1-audit-restore-proof-r1.log'],
  },
  {
    template:23,title:'附录 D：限制与后续验证',
    texts:[S,'附录 D：限制与后续验证','交互限制','当前半双工，不支持回声消除和播放中打断。\n无自动唤醒词，开机不录音，动作需要授权。','视觉限制','专用 RAW 与软件处理。完整 ISP、13MP 满速视频、MP4 编码和 GPU 产品链尚未完成。','存储与无线','UFS 依赖受控 handoff，未证明 1000 MB/s。BLE 扫描有进展，ESP32 双板业务仍待联测。','验收与许可','新版整机、云 API、机械安全、长期稳定与 3–5 秒首音仍待统一验证，模型按各自许可分发。','源码仓库：github.com/open-vela/contest2026_274_Dogking'],
    appendix:true,talk:'这些限制保留在仓库中。工程上承认边界比把所有模块写成完美更重要。接下来每一代版本都会保留恢复点，再用同口径的实机数据更新答辩内容。',sources:[core,audit,repo],
  },
];
