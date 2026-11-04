# 本地版本保存规则

v215：`5c3d406` 保存加载诊断/LLM 门禁/UFS 支持，`2bba181` 修正 polling console。
改动前标签 `a733-v215-before-load-ufs-20261030`，最终标签
`a733-v215-voice-load-ufs-candidate-20261030`；新源码/证据与镜像另存，
不覆盖 v214/v213，不删除用户总结与历史文档。恢复命令见
[VOICE_LOAD_UFS_V215](a733/VOICE_LOAD_UFS_V215.md)。

v214 实现记录：[常驻语音候选](a733/VOICE_RESIDENT_V214.md)。改动前标签
`a733-v213-before-resident-voice-20261030`；新提交/标签不覆盖 v213，镜像与 checkpoint 另存，
用户未跟踪的 `PROJECT_FULL_SUMMARY_20261030_V211.md` 保留原样。

用户要求：先完成LLM和ASR＋TTS，再统一进行本地整机全链路测试；每版保留本地Git，
便于溯源和回退。中间主机/交叉编译回归继续，不把“全部完成”改成“导出成功”。

## 已保存的基线

- `a733-v192-camera-control-candidate-20261028`：次日恢复构建完成的独立测试镜像，
  固件源码 `e499648`，打包工具版本记录 `10985fc`。285 路径 SDK 恢复一致；
  Image/两份脚本回读、GPT/ext4、根分区以外摘要检查通过。不是摄像头取帧通过版。
  ELF/bin/map/config/交接脚本单独封存；v190/v191 及昨日暂停记录不覆盖。
  见 [v192 摘要、启动脚本、实机步骤](a733/IMX214_V192_CONTROL_TEST.md)。
  恢复期间另建源码检查点 `a733-v192-source-resumed-20261028`，1426 文件/Git
  历史已实际恢复到新目录验证。昨日取消的额外归档没有 COMPLETE，不视为有效备份。

- `a733-v192-camera-control-paused-20261027`：用户关机前的开发暂停点，
  不是镜像发布。控制源码 `e499648`、工具及回归 `05cbdaf` 已存本地；
  全量构建在 2663/3019 项后主动中断，285 个 SDK 路径恢复逐字节一致。
  无 v192 最终镜像；恢复命令和硬件边界见
  [v192 暂停与继续说明](a733/IMX214_V192_CONTROL_TEST.md)。
  v190/v191 完整封存、历史过程文档和本轮各次主机回归均保留。

- `a733-v191-camera-qualification-candidate-20261027`，固件源码 `ffb4781`，
  打包工具 `0083212`：摄像头资格检查候选单独构建、镜像及检查点；
  [说明与回退规则](a733/IMX214_V191_TEST_IMAGE.md)。
  v190 因用户暂不具备测试条件而继续待测，不标记通过，不覆盖其任何恢复产物。
  本轮重新验证 v190 检查点 1349 文件与五项引用完整。
  v191 完整链接、281 路径 SDK 恢复、内核回读/分区摘要和 23 个打包拒绝路径已检查；
  最终 ELF/bin/map/config 在 `out/v191-release-20261027` 独立封存。
  v191 的诊断、自测或模拟结果不表示物理提供层和摄像头取帧已完成。

- `a733-v190-offline-voice-candidate-20261027`，功能源码
  `59808e18c3749fd0fc2e5a28e86e7d02a3acb61d`：有界 offline 多轮全链路入口、
  采集侧端点、同命令 ASR/vocoder 复用、逐轮 35 状态清空、A76 计算窗口与
  跨 task-group fd 保护。CPU/重放/mock 回归、完整目标链接、281 路径 SDK 恢复、
  镜像结构/摘要检查通过；**实时麦克风、完整听音和 3–4 秒目标未上板验收**。
  最终 ELF/bin 在 out/v190-release-20261027 独立封存，checkpoint 带五项外部引用。
  见 [v190 说明及全部测试命令](a733/OFFLINE_SESSION_V190.md)。

- `a733-v189-tts-gap-candidate-20261027`，功能源码
  `2e5530ca5ed70c45c7f7518e083340016897720b`：
  流式 TTS 不再把生产间隙误判成整句播放失败；正常结束设备段后补缓冲，
  所有真实设备错误仍失败退出。主机回归、交叉链接、280 路径 SDK 恢复与
  新镜像结构/摘要检查通过；尚待实机启动、听音、取消及多轮验证。
  见 [v189 说明](a733/TTS_REBUFFER_V189.md)。本轮只删除两份明确失败的
  v185 中间镜像，保留完整历史文档和镜像基线，清理摘要随源码保存。

- `a733-v188-boot-candidate-20261027`：针对用户报告的 v185 启动卡死，
  使用独立 256 KiB AppBringUp 线程执行全局构造，并增加逐 CPU/构造函数
  诊断。启动源码提交 `a38aa6f`；完整交叉链接、暂存恢复和镜像核验通过，
  原构建时尚未板测；后续用户日志确认**一次启动进入 NSH、自检通过**，
  不等于反复启动稳定性或唯一根因确认。原始构建证据不改写。见
  [v188 说明](a733/BOOT_V188_CANDIDATE.md)。v185 镜像不覆盖，v186/v187
  摄像头源码不删除；前后两个独立 checkpoint 保存在 out/checkpoints。

- `f638874` / `a733-v178-llm-candidate-20261025`：将之前累计的v154–v178源码、
  文档及历史实机记录整体保存。并非伪造各历史版的独立已测提交。
  v178镜像SHA256为
  `e9352cccd0ce73d895e0741a4808195d707d34937097a339bfc2d87b8fb835d3`。
  它的旧manifest记录的是封存前HEAD和dirty源码，保持原样；实际源码可按该manifest的
  525文件SHA核对到此提交。旧checkpoint/bundle/制品不覆盖。
- `45317e1` / `a733-v179-offline-voice-core-20261025`（标签包含后续构建证据提交）：
  通用ASR搜索、TTS文字处理与显式离线应用接口。见
  [状态与未完成门禁](a733/OFFLINE_VOICE_V179.md)。不是已完成的离线语音助手版本。

## v193 ID 实验候选（2026-10-28）

标签 `a733-v193-camera-id-candidate-20261028`。固件源码 `c7ff173`，功能与安全说明
分别为 `795077c` / `9aacc1d`，打包提交 `1669515`。
v192 一次真实控制板测原始日志已追加保存；通过镜像与封存内核摘要复核未改。
本版提供一次固定 ID 读取实验，不伪造实测/严格资格，不出图。
镜像、封存和回退步骤见 [v193](a733/IMX214_V193_ID_EXPERIMENT.md)。
最终 host-r5、SDK 成功子构建/285 路径恢复、24 构造函数/内存/镜像回读均独立记录；
前两次失败及 r3 外层退出 1 的原因不删、不改写为 0。
新 checkpoint/bundle 保存在 out/checkpoints，恢复到新目录，不重置原工作区。

## v194 待机模式实验候选（2026-10-28）

固件源码 `0929fea`＋`afd8e59`；本地候选标签 `a733-v194-camera-mode-candidate-20261028`。
v193 一次真实 ID 板测通过，另加 `a733-v193-id-board-pass-20261028` 指向原发布提交，
不覆盖旧标签。新旧镜像/封存内核分别校验，旧 v193 摘要不变。
v194 只新增一次性模式写入与待机回读，不宣称电气/CSI/RAW/YOLO 验收。
最终 host-r2、完整构建 r1、285 路径 SDK 恢复和镜像回读证据见
`evidence/camera-v194-host-r2`、`evidence/camera-v194-final`。
镜像与独立封存、完整构建命令见 [v194](a733/IMX214_V194_STANDBY_MODE.md)。
checkpoint/bundle 使用新目录 `out/checkpoints/a733-v194-camera-mode-candidate-20261028`；
实际恢复校验使用新目录 `out/restore-check-v194-final-r1`，不重置原工作区。
不自动推送，不写物理 SD/UFS，不删除历史开发文档/证据/通过制品。

## v194 一次实机模式通过（2026-10-28）

新增标签 `a733-v194-mode-board-pass-20261028`，不移动候选标签或覆盖镜像。
真实 ID/153 次写入/14 项回读及恢复均通过，原始日志与边界见
`evidence/camera-v194-board-20261028`。只新增证据/文档，不改固件。
新 checkpoint/bundle 为 `out/checkpoints/a733-v194-mode-board-pass-20261028`，
新目录恢复检查为 `out/restore-check-v194-board-pass-r1`。
CSI/RAW/连续稳定性未通过，UFS 问题仍暂停，语音待测保留。

## v195 MIPI 包接收候选（2026-10-28）

功能源码 `d6d6bb4`，暂存清单修正 `4996d19`，PK10–PK19 pinctrl/恢复修正 `43b0d10`。
候选标签 `a733-v195-mipi-packets-candidate-20261028`，不移动 v194 通过标签。
完整构建 r1 部署遗漏/SDK 恢复、中间 r2 链接/恢复均保留；最终选择包含 pinctrl 的 r3。
最终回归为 host-r5，host-r4 也通过但随后补强了小块只读核验工具；
此前生成编译数据库时提前运行的 r3 检查不用于最终证据。
独立封存 `out/v195-release-20261028`；镜像/摘要/命令见
[v195](a733/IMX214_V195_MIPI_PACKETS.md)，包级实验不称 RAW 或 YOLO 完成。
最终 checkpoint/bundle 为 `out/checkpoints/a733-v195-mipi-packets-candidate-20261028-final`，
同名不带 final 的较早索引/工作树归档保留，不覆盖；
新目录恢复校验使用 `out/restore-check-v195-final-r1`。不重置原工作区、不推送、
不刷物理 SD/UFS、不删除历史开发文档、通过镜像或失败记录。

## 后续每版必须做

v180：在v179最终提交`75b5faf`后新增有界多轮上下文与精确KV前缀复用；
来源/速度基线/验证边界见[版本说明](a733/LLM_CONTEXT_V180.md)。
本地标签为`a733-v180-llm-context-20261025`，在验证和提交完成后创建，
不覆盖v178/v179；对应bundle/checkpoint另存out目录，不自动推送。

1. 改动前确认本地Git HEAD、干净/已有用户改动和基线；不丢弃用户修改。
2. 记录范围、来源、软件测试、真实硬件证据及未完成项。模型/固件/私钥/账号状态不提交。
3. 生成普通本地提交＋唯一版本标签；不得用amend或强制标签覆盖上个版本。
4. 大制品保留外部文件、大小与SHA256，源码归档/镜像与Git版本明确关联。
5. checkpoint、Git bundle及新目录恢复检查通过；历史开发文档和失败记录不删除。
6. 不自动推送GitHub、不自动切换原工作区到旧版本、不刷写SD/UFS或格式化。

回退优先在**新的目录**恢复完整checkpoint或clone本地bundle后选择版本标签；
不要对仍有修改的当前目录使用reset --hard。旧镜像需另外核对摘要，烧录前另存SD私有数据。
恢复目录位置变化后，显式设置真实`OPENVELA_WORKSPACE`，不要盲用旧相对路径。
