# 外部制品与完整性校验

## 2026-11-03：v2.1pre／v226 联网恢复预览

`../openvela-a733-cubie-a7z-sd-v2.1pre-v226-candidate.img`，2,147,483,648字节，SHA256 `63aa91f38a6223e0b3ae0d549f3be17020916824aa5273c15906697188799683`，旁有`.sha256`。内核SHA256 `cff8aa15e8e0c243a78970f416f03be2ead9ad053da4a2ee0a1c1b3038c6eb30`，固件源码 `935a7b10c301f52b00f190d518751576def8583c`。
40项软件门禁、完整构建／ELF／ABI／ISA／摄像头审计和镜像回读通过，Windows/Linux哈希一致，SDK316项还原。[修改与完整测试](PRODUCT_V2_1PRE_V226.md)、[封存证据](evidence/product-v21pre-v226-image/README.md)。恢复标签 `a733-v2.1pre-v226-image-20261103`；完整本地Git bundle `out/checkpoints/a733-v21pre-v226-image-20261103.bundle`。
最多3次联网恢复后回退本地并保持120秒；不重发动作／已播前缀，不修改NPU驱动、模型或全局调度。六个旧镜像保留，v1.1/v219仍稳定。未做本版云账号／AP／声学板测，不写物理盘或UFS，无PR或推送。

## 2026-11-03：v2.0pre／v225 桌宠控制与开机运行预览

`../openvela-a733-cubie-a7z-sd-v2.0pre-v225-candidate.img`，2,147,483,648字节，SHA256 `cd4cd64097720b7da15abef1ee7f1e8291c258888a8683590a8c537998ddc51c`，旁有 `.sha256`。内核 SHA256 `c95c1b47a98feaafd45bcc40eca20645d2d2fc8e1bd3dd964e814145235d86aa`，源码 `99f56842a67b68cf66b19233c4fc3a00c5eb7dec`。
36项软件门禁、完整构建／ELF／ABI／ISA／摄像头检查、模型及启动文件回读、Windows/Linux独立哈希通过，SDK316项还原。标签 `a733-v2.0pre-v225-image-20261103`；bundle `out/checkpoints/a733-v20pre-v225-image-20261103.bundle`。
[修改与完整测试](PRODUCT_V2_0PRE_V225.md)、[封存证据](evidence/product-v20pre-v225-image/README.md)。补齐识图／运动口语、语音步数×10和300Hz档、会话免反复arm、持久设置与默认关闭的显式开机监听。旧v219/v220/v221/v223/v224哈希复验不变。未新增板测，不晋升稳定，不写物理盘／UFS，不提交PR或推送。

## 2026-11-02：v1.3fix2／v224常驻端点修复候选

`../openvela-a733-cubie-a7z-sd-v1.3fix2-v224-candidate.img`，2,147,483,648字节，SHA256 `30ca81ce9d0aeb846207b0f816603bfcafb389804763a393c4a18186cb0be25b`，旁有`.sha256`。内核SHA256 `49c7f8dca641bdeefd335e5d73732e22c5eee34a7c3226079c3fa327ffc36443`。固件源码 `e54e867cfb567e1cc6c124572e0861dacd538135`；33项门禁、完整审计／回读及Windows独立哈希通过。恢复标签 `a733-v1.3fix2-v224-image-20261102`，源码bundle `out/checkpoints/a733-v13fix2-v224-image-20261102.bundle`。
[修复与测试](PRODUCT_V1_3FIX2_V224.md)、[封存证据](evidence/product-v13fix2-v224-image/README.md)。旧v223常驻端点已有失败板测，原镜像不覆盖；v224尚待板测，卡死根因未完全证明，v1.1仍稳定。

## 2026-11-02：v1.3fix／v223修复候选镜像

`../openvela-a733-cubie-a7z-sd-v1.3fix-v223-candidate.img`，2,147,483,648字节；SHA256 `43380317ae7958d0d2eb12e61984603d411b05d478fbfa1db8d7ad20b522bb31`；旁有`.sha256`。内核SHA256 `828f52d9ec92f3d2e3fab1fd1ad57ad5b58dfa8a4903816a1113add454574c19`。完整构建、31项软件门禁、完整链接审计及最终镜像回读通过；Windows独立哈希一致，SDK316项还原。
固件源码 `fb824fbfa9a3db6d4d64fa87be612849b275e49f`；标签 `a733-v1.3fix-v223-image-20261102`；源码bundle `out/checkpoints/a733-v13fix-v223-image-20261102.bundle`。完整固件封存 `out/v223-product-v13fix-release-r1`。
[修复与上板命令](PRODUCT_V1_3FIX_V223.md)、[封存证据](evidence/product-v13fix-v223-image/README.md)。旧v219/v220/v221镜像哈希复验未变，v1.1仍为稳定版。本版尚待板测；英文非自然发音，MiMo新集成未加入，无物理盘写入、PR或推送。

## 2026-11-02：v1.2fix／v221 独立测试镜像

`../openvela-a733-cubie-a7z-sd-v1.2fix-v221-preview.img`，2,147,483,648字节，SHA256 `c07d26842cc0ddaa4a1e16b957c2c06297c94980c1af9c92bd6a303f5d8bac2c`；同目录有`.sha256`。内核SHA256 `0618fa61eb30683607ed77f25118e3a9c5711f7ba1af950445050e263be16727`。Windows/Linux校验一致；完整构建、28项门禁、ELF／ABI／ISA／摄像头、模型／启动脚本回读及SDK316项还原通过。
[交付与上板步骤](PRODUCT_V1_2FIX_V221_IMAGE.md)、[证据](evidence/product-v12fix-v221-image/README.md)。固件源码`716ac8c5b070355abc53db2720976c837bcbfa3c`；镜像标签`a733-v1.2fix-v221-image-20261102`。旧镜像与原源码批次保留，不晋升稳定；未执行本镜像板测，普通长回复超时不宣称通用修复。

## 2026-11-01：v1.2fix／v221 STM32 源码预览修订

[对接与测试](PRODUCT_V1_2FIX_V221.md)、[软件证据](evidence/product-v12fix-v221/README.md)。固定下位机 `efdb437`；603组真实编码器对照、主机虚拟串口／Agent联合回归、10个实际 ARM 单元编译通过。运行时代码 `b6000ba43e26a8d8c2511190dabd45427cf7a799`；本地标签 `a733-v1.2fix-v221-preview-20261101`，源码 bundle `out/checkpoints/a733-v12fix-v221-20261101.bundle`。
**本轮没有 v221 整卡镜像、完整链接或机械／声学板测；旧 v220 镜像不包含新命令。** 默认 I2S、v220 单轮低延迟策略和 v1.1 稳定指针保留。不得把旧版制品改名当作 v221。

## 2026-11-01：v1.2／v220 独立预览镜像（非稳定版）

`../openvela-a733-cubie-a7z-sd-v1.2-v220-preview.img`，2,147,483,648 字节；SHA256 `89a6d7f7d508427da0b4f193566c49d958451cc52c8e628a680f32a1062200a1`，旁有校验文件，Windows/Linux 核对一致。
运行时代码 `133662be8c0f369f87ae1fc6ef24df2768960ed0`；固件 SHA256 `772fea9837de156719e172d870c4dadfcca883d59d5c0f9f32e48724bd2bdccf`。
27 项门禁、真实 GGUF 独立对照、完整 ELF/ABI/ISA/摄像头软件检查、模型回读和 SDK 316 项还原通过；[说明与测试](PRODUCT_V1_2_V220_PREVIEW.md)、[证据](evidence/product-v12-v220/README.md)。
默认本地历史 0，仍保留人设、权重常驻、精确前缀 KV 和完整回复。预览标签 `a733-v1.2-v220-preview-20261101`；私有恢复目录 `out/checkpoints/a733-v12-v220-recovery-20261101` 需有有效 `COMPLETE` 才可用。
**实际速度／声音仍待板测，最新稳定仍是 v1.1。** 不修改旧镜像，不推送，不提交 PR，不写物理盘。

## 2026-11-01：最新稳定版 v1.1／v219

用户已确认本轮板测成功；直接晋升下方 v219 原镜像，不复制或改写 2 GiB 文件。
[稳定指针](STABLE_RELEASE.json)、[脱敏验收记录](evidence/product-v11-v219/board-accepted-20261101.json)、[完整说明与回退](PRODUCT_V1_1_V219.md)。
稳定标签 `a733-v1.1-v219-stable-20261101`。v1.2 预览不得覆盖此镜像、原候选标签或恢复包。
下方历史“待板测”描述为 10 月 31 日交付状态；本次没有新增真实云账号或机械动作验收。

## 2026-10-31：v1.1／v219 修复候选镜像（待板测）

本地 `../openvela-a733-cubie-a7z-sd-v1.1-v219-candidate.img`，2,147,483,648 字节。
SHA256 `fe7a28ce9996e3344a1866610c7cdb9137a1088965b0207161df6ffab4c3359f`；
运行时代码 `f6531dbc0db75590ef76904afe4c786ec4bd8503`，封装工具 `67dbf45e0cbdd7b8adcbda5118a3e550441b6885`。
26 项门禁及完整 ELF/ABI/ISA/摄像头软件检查通过，模型/私有 LLM 从最终镜像回读校验，SDK 暂存 316 项字节还原。
保留 v1.0／v217-audit2 镜像和标签；新标签 `a733-v1.1-v219-candidate-20261031`，本地恢复目录 `out/checkpoints/a733-v11-v219-recovery-20261031`。
[修复、测试及构建说明](PRODUCT_V1_1_V219.md)，[封存证据](evidence/product-v11-v219/README.md)。
**v217 的常驻语音和 Wi-Fi 已有失败板测反馈，不能称整机通过；v219 新镜像尚未板测，不承诺已解决所有真实 AP/声学环境问题或达到 3–5 秒首音。** 没有 PR、推送或 UFS 写入。

## 2026-10-31：v218 云语音软件候选（不是 SD 镜像）

源码 `28929a2999586b621230729b74a205c132294ecb`；软件证据提交 `507efb0`；本地恢复标签 `a733-v218-cloud-voice-candidate-20261031`。
工作区 SDK 文件 `quickly-openvela/cmake_out/cubie-a7z_nsh_v218_cloud_voice/nuttx.bin`，7,234,408 字节；
SHA256 `c4cca0df64af446e51abc82c950284221d26008d7af2d4d84c9d4282bb0726be`。
完整构建／ARM／ELF／ABI／ISA／布局及软件回归通过，SDK 315 项按字节还原；[封存证据](evidence/cloud-voice-v218/README.md)。
**本文件不是整卡镜像，不要按 2 GiB SD 镜像方式整卡烧写。** 本轮没有新 SD 镜像、真实云 API 或板上声音验收，没有 PR／推送。
旧 v217 审计镜像和恢复点保持不变；新增命令、独立服务选择与 Edge 网关限制见 [v218](CLOUD_VOICE_PROVIDERS_V218.md)。

## 2026-10-31：当前推荐 v1.0／v217-audit2 审计候选

本地 `../openvela-a733-cubie-a7z-sd-v1.0-v217-product-audited.img`，2147483648 字节。
SHA256 `98daee1f3b4653d3d65a14a3e92c2713560aac2fd6183946d2b06b7a3c79bc21`；
源码 `8c5a7b3b267128b7859c37d1e1000ec9054732f4`。
实际构建、模型回读、Windows 独立摘要及软件门禁通过；不等于新整机／云 API／声学首音验收。
旧 v1.0／v216／v204 未覆盖，权重镜像暂作私有板测，未自动上传 Release。
[审计记录](a733/PRODUCT_V1_0_AUDIT.md)、[发布和恢复](a733/PRODUCT_V1_0_RELEASE.md)、[说明书](PROJECT_MANUAL_V1_0.md)。

## 2026-10-30：v215 语音加载/console/UFS 候选（待板测）

`../openvela-a733-cubie-a7z-sd-v215-voice-load-ufs-candidate.img`，2147483648 字节，
SHA256 `a2cb7afe4bf003dfba82f9798e35cd76cfa4918438efebb2f5c393de7ccca771`。
内核 7176896 字节，SHA256 `c238b0d990060d935ea49adc2bf1db494d5f06456ff63c11e246947eb952e6d3`。
源码 `2bba18123bb93f133cc15decf9ca94e081c6ff02`；封存 `out/v215-release-20261030`。
报告、恢复包和测试见 [v215](a733/VOICE_LOAD_UFS_V215.md)。v214/v213 原镜像保留；
Windows/WSL 独立摘要一致，物理 SD/UFS 未写入。未宣称 READY 或 1000 MB/s 板测通过。

## 2026-10-30：v214 常驻半双工语音候选（待板测）

镜像 `../openvela-a733-cubie-a7z-sd-v214-voice-resident-candidate.img`，2147483648 字节，
SHA256 `c9569622ad928b0730bd79c8be6f17156249b5f5f9e4e858ea3aa1df9ce0bfa6`。
内核 7172800 字节，SHA256 `e2da629baee4fd6418784dc59c1601029b9963efb7bf6342576bff0f990ac1a5`。
源码 `0890208`；17 项语音回归、17 个 ARM 单元、22 项摄像头主机回归、完整构建/链接/ISA、
SDK 恢复、镜像回读与 Windows 独立摘要通过，不等于新镜像板测或 3–5 秒达标。
v213 镜像/恢复点不变；封存 `out/v214-release-20261030`，证据 `docs/evidence/voice-v214`。
常驻、接线/模型沿用、命令和限制见 [v214 说明](a733/VOICE_RESIDENT_V214.md)。
没有写实际 SD/UFS；整卡烧录前备份个人模型/配置，不把待测版称为通过版。

## 2026-10-25：v182 流式语音传输镜像候选（需要板测）

实现提交`e837aed`；最终标签`a733-v182-live-voice-20261025`。
新镜像`../openvela-a733-cubie-a7z-sd-v182-live-voice-candidate.img`，2GiB，
SHA256以`out/v182-live-candidate-20261025/manifest.json`和证据summary为准。
本次整卡SHA256：`0bb4bae686bb7132f256eee89aae7da12dd78a50c78112445a33099efb4e88c6`。
内核2817576B，SHA256 `98788e66f033b4fe6adde0b787dffea08dd857658cbd67365de8bb4812e1a426`。
2998项目标构建、ARM栈检查、SDK243路径恢复及镜像内嵌内核/GPT/文件系统检查通过；
不等于真实启动或听音验收。没有修改/擦除实际SD或UFS，整卡烧录需用户先备份。
本地同名bundle/源码归档在`out/`、checkpoint在`out/checkpoints/`，不覆盖旧版本。
新功能是stream-features/stream-endpoint/stream-echo，非完整通用ASR/TTS provider。
详情与板测命令见[v182](a733/LIVE_VOICE_V182.md)。

## 2026-10-25：v181 ASR/LLM/TTS延迟优化候选（非全链路通过版）

实现提交`f8ee31e`，最终证据提交见标签`a733-v181-voice-latency-20261025-r2`。
本地bundle和源码归档：`out/a733-v181-voice-latency-20261025-r2.bundle`、同名`.tar.gz`。
checkpoint：`out/checkpoints/a733-v181-voice-latency-20261025-r2/`，仅向新目录还原。
无r2首标签保留不覆盖；r2保护原始证据LF/CRLF字节，固件二进制不变。
13份证据/摘要位于`docs/evidence/voice-latency-v181/`；内部内核/ELF/配置位于
`out/v181-voice-latency-20261025/`，Image为2813480B，SHA-256：
`bb18194d9f5d9f311bc1cc35754a6e91de4c014d6aafa818ac06b07e06a672ee`。
完整目标2997项构建与SDK恢复通过，不发布新整卡镜像、不推送远端、不触碰SD/UFS。
LLM主机约13.5%提速不等于板上性能；目标通用ASR NPU provider与Matcha RTOS声学
执行器仍缺，未宣称3–4秒首音频或完整离线链路通过。见[v181](a733/VOICE_LATENCY_V181.md)。

## 2026-10-25：v180 本地LLM有界上下文（不发布中间SD镜像）

实现提交`a583948`，最终构建证据提交见本地标签
`a733-v180-llm-context-20261025`；bundle和源码归档为
`out/a733-v180-llm-context-20261025.bundle`、同名`.tar.gz`。
原始用户SD驻留测速、主机/ARM证据在`docs/evidence/llm-context-v180/`。
内部内核/ELF/配置/摘要封存到`out/v180-llm-context-20261025-r2/manifest.json`，
这不是整卡镜像；新增上下文、提速幅度和完整语音链路仍未实机验收。
最终checkpoint为`out/checkpoints/a733-v180-llm-context-20261025/`，
还原只使用新目录，不覆盖工作区或旧镜像。见[v180](a733/LLM_CONTEXT_V180.md)。

## 2026-10-25：v179 离线语音软件核心（无新的用户测试镜像）

源码提交 `45317e1`，最终证据提交另行保存，版本标签
`a733-v179-offline-voice-core-20261025`。真实CPU ONNX/C解码三音频与Matcha
12组文字前端已通过；目标NPU ASR provider与Matcha RTOS声学执行器仍未完成。
不宣称NPU语音助手已可用，不发中间刷卡任务。细节见
[v179](a733/OFFLINE_VOICE_V179.md)，证据在 `docs/evidence/local-voice-v179/`。
旧v178镜像、全部开发文档和旧恢复点不覆盖。

本地Git bundle：`out/a733-v179-offline-voice-core-20261025.bundle`；
源码归档：`out/a733-v179-offline-voice-core-20261025.tar.gz`；
完整恢复点：`out/checkpoints/a733-v179-offline-voice-core-20261025/`。
以 `out/v179-final-integrity.json` 的成功记录为最终保存/恢复门禁。
主机逻辑张量/语义证据包 `out/a733-v179-host-semantics-20261025.tar.gz`，SHA256
`6f5bea63961aa72bd729580e8bd06ecf3edc199eb03111b05eaa75a9fabee76e`；
不是可部署NPU模型包，不包含厂商共享库或源模型权重。

## 2026-10-25：v178 MiniCPM5 / 本地LLM修复候选（板测待验收）

新SD镜像 `openvela-a733-cubie-a7z-sd-v178-llm-candidate.img`，2147483648B，
SHA256 `e9352cccd0ce73d895e0741a4808195d707d34937097a339bfc2d87b8fb835d3`。
内核2792944B，SHA256 `a83e5237b57cca3f911cea9bccd0f3ead8132443a8fcd8043c633cb1dbfb20f9`。
源码525文件 `out/a733-v178-llm-source-20261025.tar.gz`，
SHA256 `8559b1197b1150043c455cc3f33719507cf1feffaf0f6c65e49da4cc7c06a608`。
封存目录 `out/v178-llm-candidate-20261025/`；根目录文档另由完整工程恢复点保存。

MiniCPM5真实模型的词表、模板、12次全词表logits与CPU参考对照通过，
旧Qwen同项回归通过；这不是ARM板测或1–2秒首token达标证明。
完整内核构建、五个真实ARM64编译单元、SDK226路径逐字节恢复、
ELF布局与镜像内核回读校验通过；STM32协议与安全门禁未修改。
实现范围、模型校验及板测命令见 [v178说明](a733/LLM_V178_MINICPM5_AND_VOICE.md)。

独立Linux ASR契约包 `out/a733-linux-asr-contract-v178-20261025-r2.tar.gz`，48728089B，
SHA256 `3a774076e0108668a150ae3687636e5318d6d19f18d54d07489488d7efbb3c86`。
三模型已导出并核对CID，但尚未完成本地动态语音、转写或TTS全链路验收，
该包不等于可直接部署的openvela语音模型。

开始前恢复点 `out/checkpoints/a733-pre-v178-offline-20261025/`；最终恢复点
`out/checkpoints/a733-v178-llm-candidate-20261025/`，新目录恢复演练
`out/recovery-a733-v178-smoke-20261025/`。以 `out/v178-final-integrity.json`
成功记录为最终封存门禁，旧v177及历史开发文档、失败记录全部保留。
本次不操作实机、不刷写或清理UFS；GGUF模型不内嵌。镜像仅写已备份的SD，
烧录会覆盖SD原有配置/凭据/文件；UFS未就绪时可用SD模型，不要为测试重新格式化。

## 2026-10-23：v177 UART4/STM32语音控制候选（双板待验收）

新SD镜像 `openvela-a733-cubie-a7z-sd-v177-mcu-uart-candidate.img`，2147483648B，
SHA256 `3a14cf7bf03336160bb7e5ccd1c1da484d0c6a5902c9ba1ea2c69564a0f95ba1`。
内核2764168B，SHA256 `21c131801f0994715656bf2819782e7869b15e8ba9aad580c06ea10cc16d8361`。
新源码归档504文件 `out/a733-v177-mcu-uart-source-20261023.tar.gz`，
SHA256 `15f31482f35135060be3e498c9e9828aa74d48e9ab998fd8492f9d5823b9741a`。
原v175镜像完全保留。
封存目录 `out/v177-mcu-uart-candidate-20261023/`，以其中manifest的源码/镜像摘要为准。

完整构建、实际暂存源hash、SDK224路径逐字节恢复、ARM64九单元编译和栈检查、
ELF/MMU/内存边界、孤立GOT防护、镜像内核提取、ext4与GPT检查已通过。
603帧对照下位机真实C编码及PTY语音控制集成测试通过；不是实际云ASR/TTS或电机验收。
对照STM32提交e83a9cac，UART4改为115200 8N1独占，停用旧UART TTS，保留I2S播报。
安全门禁、接线和逐级板测命令见 [v177说明](a733/AGENT_UART4_STM32_V177.md)。

恢复点 `out/checkpoints/a733-pre-uart4-mcu-20261023/`；最终候选恢复点
`out/checkpoints/a733-uart4-mcu-v177-candidate-20261023/`。历史开发文档/代码/镜像不删除。
只将新镜像写入**已备份的SD**，不要写到UFS；SD原有凭据/配置/文件先另存后恢复。
本轮没有UFS清理、格式化、模型迁移或物理串口/电机操作。语音为云ASR/云TTS，
本地NPU语音诊断保留，但不宣称本地NPU语音助手已完成。

## 2026-10-23：v176 IMX214只读探测核心（不是摄像头镜像）

新代码/严格ID/超时故障测试及真实ARM64对象见 [限定与缺口](a733/IMX214_V176_QUALIFICATION.md)。
只探测核心编译通过，未提供板级供电/时钟provider，未注册视频节点，也未装入v175。
本阶段没有声称新的摄像头镜像已可用，不覆盖已封存的v175 UFS镜像。

Linux一次性对照包 `out/a733-linux-imx214-v176.tar.gz`，SHA256
`042127e03dc15354d753e5c4e5a39b43dc1acfaa9f8ec75849c3d53a7f6b1fa1`。
默认只读取证，只有显式`--capture`才调用官方Linux链路抓三帧；不扫描I2C/安装/刷写。
新阶段封存目录 `out/v176-imx214-probe-20261023/`，源归档
`out/a733-imx214-v176-source-20261023.tar.gz`；完整工程恢复点
`out/checkpoints/a733-imx214-v176-pre-hardware-20261023/`。以本阶段manifest和
checkpoint verify记录为准，旧974文件v175恢复点保留，当前新开发文档也全部保留。

## 2026-10-22：v175 UFS 自动挂载 / Agent 自动模型候选

新 SD 镜像 `openvela-a733-cubie-a7z-sd-v175-ufs-auto-candidate.img`，2147483648B，
SHA256 `b321a8d6fa53dac5053891f1df2bf3425e7280256a78405c74a0823336b42271`。
内核2747552B，SHA256 `054f7b55d2d95ee1c915e93a8a9f03b185382582e15fc0902f1100385c77d417`。
源码478文件 `out/a733-v175-ufs-auto-source-20261022.tar.gz`，SHA256
`70a6cbe006cefecb8af256347a99386184b3a13753b09aef6dff59db9fdb8c2a`。

2985目标完整构建、四个真实存储服务符号链接、17单元 ARM64 编译、57关键源/6应用源
早晚暂存一致，SDK173路径恢复、ELF布局与镜像内核提取已通过。新行为见
[v175流程](a733/UFS_AUTO_V175.md)。v174 已由用户实测复制/重启完整性、约67.58MiB/s
读取和15.99s模型冷加载；**v175新开机自动流程尚待板测**，不改写历史manifest。
不自动格式化、迁移、scratch写入或切换HS Gear；独立冷PMIC/PHY初始化未实现。

封存 `out/v175-ufs-auto-candidate-20261022/`；完整恢复点
`out/checkpoints/a733-ufs-auto-v175-candidate-20261022/`，新目录演练
`out/recovery-a733-ufs-auto-v175-smoke-20261022/`。以
`out/v175-final-integrity.json` 成功为最终封存门禁，原v174及全部开发文档/镜像保留。
只刷已备份的SD，不刷UFS，已有卷不要重新mkfatfs。

## 2026-10-22：v174大块UFS存取候选（持续速度待板测）

新镜像 `openvela-a733-cubie-a7z-sd-v174-ufs-bulk-candidate.img`，2147483648B，
SHA256 `7851972d7f853b2552b55edec0e2315d2a24a95d34334e6675a5c1742c10323d`。
内核2743400B，SHA256 `072820b69ee4616aeee481545ac423cff12bd6c084bbcba68c48cf1ced768ddc`。
源码470文件 `out/a733-v174-ufs-bulk-source-20261022.tar.gz`，SHA256
`11d93c1548bdc5caa3f4a318bc50a3846aa27aff7084d79a813b40552f28ac2d`。

最终完整2984目标构建、ufs_main实际链接、ELF布局、内核提取和171路径SDK恢复已通过。
51关键overlay源/5应用源早晚暂存一致；13真实ARM64对象及主机故障回归通过。
原v173、v172及全部历史版本不覆盖；v173已有小文件持久性板测，但大cp中断后的模型
完整性未知，不把本候选宣称为已测高速版。已保留本次用户原始日志。

封存 `out/v174-ufs-bulk-candidate-20261022/manifest.json`；最终项目恢复点
`out/checkpoints/a733-ufs-bulk-v174-candidate-20261022/`，新目录恢复演练
`out/recovery-a733-ufs-bulk-v174-smoke-20261022/`，最终门禁
`out/v174-final-integrity.json`，其成功才表示所有恢复/补充材料校验完成。
软件证据见 [v174](evidence/UFS_BULK_V174_20261022.md)，
[挂载、旧模型检查和批量复制流程](a733/UFS_BULK_V174.md)。

镜像只写**已备份的SD**，不要写UFS；当前SD配置/模型先外部备份。
已有UFS卷**不再mkfatfs**。新ufs应用使用256KiB缓冲、后台进度/协作停止、全目的SHA256，
已有目的文件拒绝覆盖。新原始RO块视图不是可挂载的RO FAT；仍沿用通过的RW挂载入口。
未改HS Gear/PHY、未关闭底层写后回读/CACHE10，速度以板测为准。

## 2026-10-22：v173独立UFS数据卷候选（文件系统待板测）

镜像 `openvela-a733-cubie-a7z-sd-v173-ufs-data-candidate.img`，2147483648B，
SHA-256 `7e606b28ee9f169992447a3c50f6b904667a52436846a54c39d60d77c6648fbf`。
内核2738136B，SHA-256 `12f78062388e0fa528a17068de10f67654c8d7091069d0d8566f33d6c06f5194`。
源码461文件：`out/a733-v173-ufs-data-source-20261022.tar.gz`，SHA-256
`9768f61f54decf73b5c67ee8d28bc9fed56b3c5bfdcecd1dce1c1197d423f7fc`。
封存 `out/v173-ufs-data-candidate-20261022/manifest.json`；完整构建、ELF、嵌入内核
回读及170路径SDK恢复通过。原v172镜像、文档、归档和恢复点均保留，不覆盖。
完整项目恢复点 `out/checkpoints/a733-ufs-data-v173-candidate-20261022/`，
演练输出至新的 `out/recovery-a733-ufs-data-v173-smoke-20261022/`，
最终一致性结果 `out/v173-final-integrity.json`。不把旧工作树HEAD单独当成复现源。

v172后续用户板测确认scratch通过，末尾dd误报已修复；v173仍需format/mount/模型
文件/重启持久性板测。[一轮测试和首次格式化风险](a733/UFS_DATA_V173.md)。
镜像只写已备份SD，不写UFS；mkfatfs仅在首轮明确清空UFS第三分区时执行一次。
当前源码版本不自动格式化、挂载、迁移或删除SD模型。

主机/编译/镜像证据见 [v173软件记录](evidence/UFS_DATA_V173_20261022.md)。

## 2026-10-22：v172固定UFS写入窗口候选（待实机）

独立新SD镜像 `openvela-a733-cubie-a7z-sd-v172-ufs-scratch-candidate.img`，
2147483648B，SHA-256 `09f92202f63792afa9ee17f61a1d8235d29b3b377039efe88eddc1b462733eeb`。
内核2725848B，SHA-256 `841704d8e301f7aca48128316b927caefc022d6ab4d9374a2b5f5c466a3a16f6`。
源归档454文件：`out/a733-v172-ufs-scratch-source-20261022.tar.gz`，SHA-256
`9b89f447156221cc3bc74b1f1236a72bbb876a6338ccff34d96a1917bb1c4553`。
构建/ELF/镜像提取/源归档/167路径SDK恢复已核对；非SD分区3区域逐字节比较
2,025,848,832B不变，原v171镜像未覆盖。旧GPT分区4非2048扇区对齐提示继承自基线，
没有在本轮“修复”GPT或修改分区。

封存 `out/v172-ufs-scratch-candidate-20261022/manifest.json`，完整项目恢复点
`out/checkpoints/a733-ufs-scratch-v172-candidate-20261022/`，最终完整性/新目录恢复
结果以 `out/v172-final-integrity.json` 为准。先后暂存迭代的日志不混作最终源码链：
以r2的49关键源与11个ARM64对象记录为准。

仅新增显式确认的Kingston测试Linux分区内64KiB试验，3次WRITE/4次CACHE10/回读/还原。
仍无通用块写/文件系统/自动挂载/模型迁移；备份仅RAM，失败/断电可能破坏测试Linux。
见 [全部命令和风险](a733/UFS_SCRATCH_V172.md)、[软件证据](evidence/UFS_SCRATCH_V172_20261022.md)。
v171现在已取得 [标准只读块接口用户实机通过](evidence/UFS_V171_RUNTIME_20261022.md)，
原镜像SHA仍 `baa130bf61d45705bb3cacd13c07e95ec5716f2154b9593705d73ee2973473ac`。
本地大型制品不自动上传Git，也不将新候选称为写入实机通过版。

大型镜像、NPU 模型、厂商固件和专有工具不存放在 Git 历史中。这样既可
避免 GitHub 体积限制，也不会重新分发受许可证限制的材料。

## 已通过、不可覆盖的恢复基线

蓝牙最新恢复基线为 **v144 五轮生命周期实机通过**，源码标签
`a733-ble-v144-lifecycle-5cycle-pass`，详见
[五轮实机证据](evidence/BLE_V144_FIVE_CYCLE_HARDWARE_PASS.md)。
镜像 `openvela-a733-cubie-a7z-sd-ble-v144-reopen-candidate.img`，2147483648 bytes，
SHA-256 `829a1e972129c35a3347575ee5dac219a060cdddec6206fbd410954c44b66716`。

新增 **v145 GATT 客户端候选**：
`openvela-a733-cubie-a7z-sd-ble-v145-gatt-client-candidate.img`，2147483648 bytes，
SHA-256 `d66bc9afdf8ecc690fca77b8d42b2f1b177a40cfbdeab9c2c1be893a57f40d32`。
完整构建、布局、文件系统/GPT、内嵌内核和独立 Windows 哈希校验通过，
后续已上板完成运行、BLE短循环和轻负载共存检查，真实GATT/双板仍未测，
默认SSH报文损坏仍待定位，不覆盖v144恢复基线。详见
[v145实机记录](evidence/BLE_V145_SSH_RUNTIME_ACCEPTANCE.md)；构建/打包命令、
内核/ELF哈希见 [v145交付记录](BLE_GATT_CLIENT_V145.md)。以下显示链路恢复点继续保留。

```text
名称：openvela-a733-cubie-a7z-sd-lvgl-pet-v128-tick-fix-candidate.img
大小：2147483648 bytes
SHA-256：5e7c1f6044957cbc38881619bc082e738bfc180f1753b367e42c2cb09454a830
```

镜像内嵌内核：

```text
大小：2195392 bytes
SHA-256：9f176e6ffed714bd7b530de70046f35579f6a17807c12377c11a61bab63cef3b
```

v128 在已验证可启动的 v127 完整镜像上只替换 openvela 内核。完整交叉构建、ext4、GPT
和内嵌内核一致性校验已经通过。v126 恢复低有效 CS 的物理电平，明确发送
`COLMOD=0x05`，并让 raw8 与 LVGL 都通过 direct 8-bit SPI 路径发送高字节优先的
RGB565。2026-09-27 实机确认 raw8 白框/RGB 三色块连续两次通过，LVGL 桌宠界面
连续运行 30 秒并返回 NSH，测试后无 SPI 或异常日志。ST7735/LVGL 状态已提升为
hardware-passed。v127 在这条稳定链路上加入七类程序化表情、眨眼、10 秒回闲、
初始化清屏以及 Agent 到 UI 的非阻塞消息队列。实机已确认首帧和事件接收，但画面
因 LVGL tick 未推进而保持普通表情。v128 只补齐 `lv_tick_inc()`；2026-09-27 实机
确认七种表情连续切换、手动事件和 AIPET 思考/回复联动全部通过，且无 SPI、异常或
堆错误。该镜像的显示产品链路为 hardware-passed；尚未执行数小时压力测试，文件名
保留 candidate 以避免与最终比赛发布镜像混淆。I2S 本轮暂停，不包含在通过结论中。

该文件现固定为显示链路的已通过恢复点。后续开发镜像必须使用新文件名，禁止覆盖或
就地修改 v128。

## 最新显示性能通过镜像（v129）

```text
名称：openvela-a733-cubie-a7z-sd-lvgl-fps-v129-candidate.img
大小：2147483648 bytes
SHA-256：406697b0da50ba18809ed016fb215f7933e023a32a08fdac04ad05aca5719f5c
```

镜像内嵌内核：

```text
大小：2195392 bytes
SHA-256：8fe4c531b12223e0dd785f8cd009426fcb51de2f90c51992c81a06c08043d2b1
```

v129 从 v128 的副本封装，仅新增 `display fps [seconds]` 全屏刷新基准。全量构建、
ext4 检查、GPT 检查以及镜像内嵌内核反向哈希均通过。2026-09-27 实机取得稳定的
10/30 秒结果：`29.564/29.563 fps`、`1182 KiB/s`；随后七种表情完整回归，日志无
SPI、异常或内存错误。因此 v129 的显示性能检查点为 hardware-passed。v128 仍保留
为改动更少的产品 UI 恢复基线；文件名继续保留 candidate，表示未完成数小时压力
测试和比赛最终发布流程。

## NPU/LVGL 联动失效归档（v130，不应继续烧录）

```text
名称：openvela-a733-cubie-a7z-sd-yolo-lvgl-v130-candidate.img
位置：A733-A7Z-ALL-Files/openvela-a733-cubie-a7z-sd-yolo-lvgl-v130-candidate.img
大小：2147483648 bytes
SHA-256：a8f84509632f73805c95a43d8afaf7aff735038a62741b3d2e4d18e55e39d7da

内核：quickly-openvela/cmake_out/cubie-a7z_nsh_v130_yolo_lvgl/nuttx.bin
大小：2199488 bytes
SHA-256：df2ab035a71bc0703fd84370ad1c9e27596e8f662793776681914a18936ac8b9
```

v130 上板后确认 `display vision` 存在，普通 `npu yolo` 也能在真实 NPU 上完成 dog
三框和 black 零框推理，但 `npu yolo-ui` 未进入最终 CLI。原因不是 NPU、模型、LVGL
或消息队列实现，而是 `tools/build-a733-wapi.sh` 的显式暂存清单漏掉
`apps/system/a733npu/`，导致构建时沿用了官方工作区中的旧 NPU 应用。该镜像仅作为
失败证据保留，状态为 **hardware-failed (packaging omission)**，不应继续烧录。

## 最新 NPU/LVGL 联动通过镜像（v131，文件输入实机通过）

```text
名称：openvela-a733-cubie-a7z-sd-yolo-lvgl-v131-candidate.img
位置：A733-A7Z-ALL-Files/openvela-a733-cubie-a7z-sd-yolo-lvgl-v131-candidate.img
大小：2147483648 bytes
SHA-256：a8dba939ae52f94890abc0f5665ef294386b42af87aae89f6f4558b7ee5e7389

内核：quickly-openvela/cmake_out/cubie-a7z_nsh_v131_yolo_ui_fix/nuttx.bin
大小：2199488 bytes
SHA-256：eff2afa0a20c7e956b47dc0bdfb1b493a4bed636cdd46211021d066fa35cd816
```

v131 以不可覆盖的 v129 为只读底图重新封装。构建包装器现显式暂存
`a733npu/CMakeLists.txt`、`Kconfig` 与 `a733npu_main.c`；最终 BIN 和 ELF 均已静态
确认同时含有 `npu yolo-ui <640x640-rgb.bin>` 与 `YOLO LVGL overlay active`。
全量构建、ext4、GPT、镜像内核回读和哈希均通过。2026-10-11 实机验证 dog 输入
在约 `1.687 s` 内得到三框并由 LVGL 正确显示，black 输入在约 `1.681 s` 内得到零框
并清除旧框，测试后空闲内存 `4239645200` bytes，未见 NPU/SPI/LVGL/堆错误。因此
“文件 RGB → NPU → 队列 → LVGL 框层”提升为 **hardware-passed**。它仍不包含实时
摄像头流或数小时压力测试；v128/v129/v130 均保留，禁止覆盖。

## 最新 I2S0 诊断候选镜像（v132，等待实机）

```text
名称：openvela-a733-cubie-a7z-sd-i2s-v132-fifo-diag-candidate.img
位置：A733-A7Z-ALL-Files/openvela-a733-cubie-a7z-sd-i2s-v132-fifo-diag-candidate.img
大小：2147483648 bytes
SHA-256：e3212a5f107cd842b7090b7f8f2afdcc53da5023a08721cc18861e67471b958a

内核：quickly-openvela/cmake_out/cubie-a7z_nsh_v132_i2s_fifo_diag/nuttx.bin
大小：2199512 bytes
SHA-256：3875d699853878c4a99e4b7e4e532fabe23de0405247332ed462fd69ad30599f
```

v132 从 v131 只读副本封装，不覆盖 v131。默认 `tone=` 对齐厂商 Linux 的 16-bit
FIFO 语义，保留 `tone32=` 作为对照；INMP441 采集使用 24-bit sample resolution、
100 ms warmup，并新增原始 FIFO 抓取 `capture32=`。全量构建、ext4、GPT 和镜像
内核反向哈希均通过。实机曾有一次停在 `N8` 且未进入 NSH，后续确认是供电问题，
不是 I2S 固件缺陷；为保持版本线清晰，仍推荐使用包含惰性探测改进的 v133。
完整证据见 `docs/evidence/i2s-v132/build-summary.md`。

## I2S0 启动安全候选镜像（v133，等待实机）

```text
名称：openvela-a733-cubie-a7z-sd-i2s-v133-bootsafe-candidate.img
位置：A733-A7Z-ALL-Files/openvela-a733-cubie-a7z-sd-i2s-v133-bootsafe-candidate.img
大小：2147483648 bytes
SHA-256：f6f7fb9048f5809840d1968090036b9f6907f9d79ea093abbc06ecd76ba60b6e

内核：quickly-openvela/cmake_out/cubie-a7z_nsh_v133_i2s_bootsafe/nuttx.bin
大小：2199512 bytes
SHA-256：acf73e883300b635832fb063e2fc553b9184f2624a332d5c7e8ef3b69ab6b760
```

v133 保留 v132 的全部 I2S FIFO 诊断，但将可选音频硬件改为真正的惰性探测：
系统 bring-up 只注册 `/dev/a733-audio`，直到用户读取节点或执行显式音频命令才
访问 CCU、PIO 与 I2S 寄存器。全量构建、ext4、GPT 与镜像内核反向哈希已通过；
v133 已正常进入 NSH；MAX98357A 的 `tone=` 与 `tone32=` 均实际发声，其中正式
16 位 FIFO 路径音量明显更大，144000 样本、TX count=144031、timeout=0。因此
镜像为 **boot-passed**，MAX98357A 播放链路为 **hardware-passed**。随后
INMP441 连续两轮得到全非零原始样本、正确 warmup 和精确文件大小且无超时，
因此时钟/FIFO/文件传输通过；但三段实际 WAV 均为电流噪声。已确认 v133 的
S24→S16 转换误用 `>>16`，应为 `>>8`，故麦克风 PCM 内容仍为
**hardware-pending**，必须使用后续修复镜像重新试听。
启动故障分析与测试门槛见 `docs/evidence/i2s-v133/boot-safety.md`。

镜像由已验证可启动的 A733 基础镜像复制后，仅替换第 3 分区中的 openvela 内核；
基础镜像不会被修改。可重复命令见 `docs/BUILD_AND_IMAGE.md`。

## INMP441 PCM 修复候选镜像（v134，等待实机）

```text
名称：openvela-a733-cubie-a7z-sd-i2s-v134-inmp441-pcmfix-candidate.img
位置：A733-A7Z-ALL-Files/openvela-a733-cubie-a7z-sd-i2s-v134-inmp441-pcmfix-candidate.img
大小：2147483648 bytes
SHA-256：e644ce550879a7025a58f531e30568bb37a8fe27126ad0994cdd7da0f2cc2750

内核：quickly-openvela/cmake_out/cubie-a7z_nsh_v134_inmp441_pcmfix/nuttx.bin
大小：2199512 bytes
SHA-256：6dfd383ef74c4dda331f493301b8ac0d6e622437a9d84ee5cfeb7f2267c15f60
```

v134 基于 v133 已知可启动镜像，仅替换第 3 分区的 openvela 内核。它把 A733
RX FIFO `S24_3LE` 样本到 WAV S16 的位移从 16 位改为 8 位。全量构建、ext4、
GPT、镜像结构和内嵌内核反向哈希均已通过。

实机录音已经否决 v134：三种声场均为接近满量程的电流声。该镜像仅保留作失败
复现，不应再用于功能验收。

## INMP441 单声道 S32 修复候选镜像（v135，等待实机）

名称：openvela-a733-cubie-a7z-sd-i2s-v135-inmp441-s32mono-candidate.img

位置：A733-A7Z-ALL-Files/openvela-a733-cubie-a7z-sd-i2s-v135-inmp441-s32mono-candidate.img

大小：2147483648 字节

SHA-256：c38e1ed47ff486658434894223a8272f7845c0578acff4d5ba5c8997d7b2a110

内核：quickly-openvela/cmake_out/cubie-a7z_nsh_v135_inmp441_s32mono/nuttx.bin

内核 SHA-256：aab132f34b836c48ebe1821d878ec7f7a92ac474aa56c71255d376a4e9a8884a

v135 从 v133 已知可启动镜像复制封装，只替换第 3 分区的 openvela 内核。采集
路径曾按 mono S24_LE/S32_LE 设置；全量构建、ext4、GPT 及内嵌内核哈希均已
校验，但实机录音证明 RX FIFO 空状态被误判为 128 个可读字。三段音频均为数字
电流声，v135 已降级为 **hardware-failed**，仅保留作失败复现，不应继续烧录。

## INMP441 RX FIFO 节拍修复候选镜像（v136，等待实机）

```text
名称：openvela-a733-cubie-a7z-sd-i2s-v136-inmp441-rxfifo-candidate.img
位置：A733-A7Z-ALL-Files/openvela-a733-cubie-a7z-sd-i2s-v136-inmp441-rxfifo-candidate.img
大小：2147483648 bytes
SHA-256：3146cc601ee5a59f6815dbb08701d43c6d993bef6a4f852d91444f02253eb358

内核：quickly-openvela/cmake_out/cubie-a7z_nsh_v136_inmp441_fifo_fix/nuttx.bin
大小：2199576 bytes
SHA-256：68adbe5ac9a258c44fe3f3ea4628ffd5323738b0993bfea255c11e6725a3cf5f
```

v136 从 v133 已知可启动镜像复制封装，只替换第 3 分区的 openvela 内核。它修复
了 v135 把 `FIFOSTA` 固定 bit7 当作 128 个可读字、从空 FIFO 高速重复读取旧值的
根因，并补齐 RXA 节拍、四组 RX map、SR24/SW32/RXOM1、PB8 下拉、RAM 采集后
落盘和严格采集证明。全量构建 1783/1783、ext4、GPT、镜像结构和内嵌内核反向
哈希均已通过。当前为 **build-passed / image-verified / hardware-pending**；必须
以 5 秒真实墙钟、`capture-proof` 和静音/语音/拍手三段试听完成最终验收。

详细根因、构建证据与命令见 `docs/evidence/i2s-v136/build-summary.md`。

## INMP441 半帧选择候选镜像（v137，实机录音内容失败）

```text
名称：openvela-a733-cubie-a7z-sd-i2s-v137-inmp441-slotfix-candidate.img
位置：A733-A7Z-ALL-Files/openvela-a733-cubie-a7z-sd-i2s-v137-inmp441-slotfix-candidate.img
大小：2147483648 bytes
SHA-256：580178196fe68ac7db0a0591360bdca13f668c5c8417971cc2ce7b18aa618191
内核：quickly-openvela/cmake_out/cubie-a7z_nsh_v137_inmp441_slot_fix/nuttx.bin
内核 SHA-256：07a3a5432ad713c1a995fb2396586a4834c09894c59e5542846474310b147c47
```

v136 已实机证明 5 秒录制节拍正确，但 slot 0 内容是未驱动半帧的时钟串扰。
v137 默认改取 slot 1，并提供 `captureleft=` / `captureright=` 对照命令。镜像从
可启动 v136 复制并只替换 openvela 内核；ext4、反向内核哈希和 GPT 校验通过。
详细依据见 `docs/evidence/i2s-v137/build-summary.md`。

最新实机：right 静音、left 电流声；半帧切换有效，但真实语音未通过。
v137 保留为 v138 只读封装底图，不能将其列为麦克风通过镜像。

## INMP441 采样矩阵候选镜像（v138，实机录音内容失败）

```text
名称：openvela-a733-cubie-a7z-sd-i2s-v138-inmp441-matrix-candidate.img
位置：A733-A7Z-ALL-Files/openvela-a733-cubie-a7z-sd-i2s-v138-inmp441-matrix-candidate.img
大小：2147483648 bytes
SHA-256：3d19c04540fc433567694b3030dba4b5c74a3f9ed041aa135945d5746723bff1
内核：quickly-openvela/cmake_out/cubie-a7z_nsh_v138_inmp441_matrix/nuttx.bin
内核大小：2203816 bytes
内核 SHA-256：bbe97a27643bc5c8f7441290b58bd049b66c47a822dfb7c54a0f2d42a8ab9cc2
```

v138 以 v137 为只读底图复制封装，保留所有旧镜像。全量构建 2730/2730 targets、
ext4、GPT 与内核回读一致性校验通过；96 个官方暂存路径条目在构建前后完全一致。
`capturecfg=<slot>,<bclk>,<pull>,<ms>` 提供左右半帧、BCLK 正常/反相、PB8
下拉/禁用的八组独立 WAV，内容统计用于后续实机鉴别语音与串扰。
用户随后提供的八组实机录音仍失败：left 四组 RMS 约 21152..21159，right 四组
逐字节相同且每个样本恒定 -11437。状态更新为 **build-passed / image-verified /
boot-observed / transport-passed / audio-content-failed**。
右侧恒值尚不能证明 slot 1 正常接收，具体原因仍待物理波形和 RX 配置配套核验。
完整命令和证据见 `docs/evidence/i2s-v138/build-summary.md`。
文件哈希与精确内容分析见 `docs/evidence/i2s-v138/audio-analysis.md`。

最终比赛发布版本必须重新构建、实机回归并验证镜像，将其作为 GitHub Release 资源，
同时在本文件中补充下载地址、大小和 SHA-256。

## 开发期间使用的 NPU 预处理包

以下信息可供拥有官方模型和 VIPLite 工具合法访问权限的评估者核对确切
输入：

| 制品 | 大小 | SHA-256 |
| --- | ---: | --- |
| `lenet.a7pm` | 441856 | `1e65fc966d5aabbda46f573e16c41c63e6ca3019d241feea12190b1cb38aa027` |
| `yolov5s-zero.a7pm` | 11239104 | `3feb08593d743bb5ca239110013954fe08ad8a864c81fd719b65b2630dbc4472` |
| `yolov8n-pcq-v3.a7pm` | 8017024 | `3c2891ef77ce9a44dd02cd32bdd7db5ec92d14185014e6de8db5803a6cbecce2` |
| dog RGB 输入 | 1228800 | `07a1654c992b1a2e38ccc595a571994889134df9594eef09b5a85d70f4820c60` |
| black RGB 输入 | 1228800 | `3630e065eb7b4540fbab11dbfd2619e8500f211b9c404380a1867fdc44b77c0c` |

A7PM 包含通过官方 Linux VIPLite prepare/run 流程获取并核对过的 VIP 虚拟
地址布局。它们不能替代或解码 Allwinner 专有的 `.nb` 编译器/链接器。

## 仅运行时使用的数据

以下内容应放在开发板数据分区中，不应进入源代码管理：

- 从官方 BSP 获取的 FCU760K D80-U02 固件；
- HTTPS CA 证书；
- 生成的 SSH 主机密钥；
- 本地 Wi-Fi 凭据和服务配置；
- 许可条款允许在本地使用的 A7PM/模型文件；
- 用户数据、采集帧和推理输出。

构建本仓库不需要密码、私钥或 API 令牌。
