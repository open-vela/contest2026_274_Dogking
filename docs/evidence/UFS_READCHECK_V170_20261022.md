# UFS v170 软件/交付证据（2026-10-22）

## 输入及恢复点

v169 用户一次 NOP 真实通过另存 `UFS_V169_RUNTIME_20261022.md` 和原始日志，
不修改旧封存。改动前保存 `out/checkpoints/a733-pre-ufs-readcheck-v170-20261022/`：
913 个源文件、3 项外部引用，无 tracked 删除。旧镜像均保留。

本阶段合并验收而不合并越权：只读命令、16KiB 头中尾重复比较和 GPT CRC/基本范围。
具体边界及一次启动的测试次序见 `../a733/UFS_READCHECK_V170.md`。
没有上板、SSH、物理介质写入、格式化或 UFS 文件系统通过。

## 主机与对象证据

- `out/test-ufs-readcheck-v170-r3.log`：直接编译生产核心，ASan/UBSan/-Werror；
  3,249 个故障注入点、21 类数据/响应破坏，以及 UA、sense、计时、几何、GPT 等边界。
  成功 GPT 模拟路径 1,558 次 HCI 读、38 次写；这些是模拟器计数，不是实机吞吐。
- `out/test-ufs-transport-v170-r3.log`：26 个驱动整合场景；包括解析/缺少 NOP 零写拒绝、
  quiesce→NOP→LUN2 只读批次及 clock 丢失隔离。测试重置仅在主机夹具，不是产品恢复入口。
- `out/test-ufs-link-v170-r3.log`：原 link 3,934 故障点及 46 项 staging/tamper 检查。
- `out/test-ufs-nop-v170-r2.log`：旧 NOP 核心回归，生产 NOP/map 文件未修改。
- `out/ufs-v170-arm-objects-r3/summary.json`：10 个真实 ARM64 对象；
  readcheck 最大静态栈帧 672B，UFS 主驱动 256B。硬件诊断 3,392B 是原有独立单元。
- `out/static-ufs-readcheck-v170.log`：GCC analyzer 通过。
- `out/test-staging-restore-v170.log`：构建器正常/信号/恢复失败的三类实际 cleanup 回归。
- `out/test-ufs-quiesce-v170.log`、`test-ufs-handoff-v170.log`、`test-ufs-board-v170.log`：
  原生产停止/交接/板级采样回归通过。
- `out/test-ics43434-v170.log`、`test-i2s-matrix-v170.log`、`test-aipet-audio-v170.log`：
  原麦克风采集、I2S 配置矩阵、音频应用/重采样/完整 TTS/输出选择模拟回归通过。
- `out/test-project-checkpoint-v170-wsl.log`：3 个临时 Git 库恢复测试通过。
  Windows 首轮临时目录创建被 sandbox 拒绝，保留原失败日志；改用 WSL 临时目录后通过，
  没有据此更改原仓库 Git/文件权限。一次拼错不存在的 audio-logic 测试入口也保留日志，
  其后实际执行的是上列现有音频测试，不将缺失入口记作通过。

第一轮编译暴露 signedness warning，修正后重跑；保留失败日志，不将首轮记作通过。
最终核心 SHA-256 `14f585e5ecd2a8a12f399fe8045c666e896da310ae361de7a6887fd5d78acda8`，
头文件 SHA-256 `80870bfce819812f68d459f51626a953ca8d32ec783a9552c8e81f5736105c09`。
`out/v170-staged-sources.json` 记录构建时实际 46 项关键暂存来源。

## 完整构建与交付

使用新 build 目录 `quickly-openvela/cmake_out/cubie-a7z_nsh_v170_ufs_readcheck_candidate`；
tensor、UFS readonly、handoff、quiesce、NOP、readcheck 六项实验开关显式启用。
完整日志为 `out/build-v170-ufs-readcheck.log`。源码在 staging 后冻结。
SDK 原始 164 项输入记录在 `out/sdk-before-v170.json`，必须在构建退出后逐项核对恢复。
构建中持久备份的 164 项已原生逐项核对一致（`out/sdk-backup-verified-v170.log`）；
这不代替退出后对实际 SDK 的再次核对。

完整构建退出 0，实际 ELF 布局通过，164 项 SDK 已原生逐项恢复一致：

- BSS `0x40498000..0x42560e70`；read arena `0x424d8000..0x424eb000`；
  NOP arena `0x424eb000..0x424fe000`，均 77,824B、对齐且互不重叠。
- 初始栈开始 `0x42561000`，image end `0x42579000`，runtime 37,195,776B，未碰 BL31。
- 真实 ELF 的 readcheck 写适配器含受限 0x20/0x50/0x54/0x58/0x60 检查及 DSB；
  `a733_ufs_board_hci_allowed` 实际仍返回 0；地址翻译有 AT S1E1R/W 和 PAR_EL1 恢复。
- `out/v170-source-invariants-final.json` 确认两次 staging 的 46 项内容哈希一致；
  旧 44 项中 41 项不变，改动仅 UFS 主文件/CMake/Kconfig，新增 readcheck C/H。
- `out/sdk-restored-v170-final.log`、`v170-final-layout.txt`、`v170-final-*.txt` 保存证据。

新普通 SD 镜像（不是物理介质烧录）：
`../openvela-a733-cubie-a7z-sd-v170-ufs-readcheck-candidate.img`，2,147,483,648B。
SHA-256 `d2671dd0713aea02d5661c77f74d21d23f9cf6d85142b16db926e32973a38e2b`。
内核 2,717,656B，SHA-256
`594293833e82a8d5a57997c856b8cefadb225b0533821b5017eda29ecf80bed7`。
镜像提取内核一致，ext4 只读检查及 GPT 工具检查通过；p4 末端非 2048 对齐提示
与基底相同，不在此阶段修复分区布局。

源码归档 `out/a733-v170-ufs-readcheck-source-20261022.tar.gz` 共 449 项，SHA-256
`ef6f52a4c23361a969064b71af1eece7685fa79ba00d632f5860a3bab6676a2b`。
原始六项产物及其哈希保存在 `out/v170-ufs-readcheck-candidate-20261022/manifest.json`，
包含 dirty 工作区内容，不把旧 HEAD 冒充唯一可复现状态。
独立 Windows 原生校验已通过（`out/v170-native-integrity.json`）：449 项归档内容与
当前源码逐项一致、46 项关键源码一致、六项产物哈希一致、六项实验配置已启用。
基底 v169 哈希未变，非 p3 的 2,025,848,832B 与基底逐字节相同；没有删除旧源码路径。
`out/test-ufs-collector-v170.log` 的 9 项、`out/test-local-voice-v170.log` 的 4 项回归也通过。

## 交付与完整项目恢复入口

镜像旁 `.img.sha256` 用于烧录前核对；只写已备份的 SD，不写 UFS。
封存目录 `out/v170-ufs-readcheck-candidate-20261022/` 附全部当前 docs、ARM64 对象、
实际测试/构建/恢复日志及 `ufs-status.json`。原始 manifest 的六项产物不追改。
其余补充内容统一由 `supplemental-sha256.json` 记录，最终状态读取
`out/v170-final-integrity.json`，不能把旧失败尝试的日志当成最终通过日志。

完整项目不可覆盖 checkpoint 使用新标签
`out/checkpoints/a733-ufs-readcheck-v170-candidate-20261022/`；
完整文档/代码、dirty patch、保存的 index 和 reachable Git history 均保留，
不是只有 449 项构建源归档。验证/新目录恢复入口：

```sh
python tools/checkpoint-a733-project.py verify \
  out/checkpoints/a733-ufs-readcheck-v170-candidate-20261022 --check-references
python tools/checkpoint-a733-project.py restore \
  out/checkpoints/a733-ufs-readcheck-v170-candidate-20261022 \
  out/recovery-v170-my-new-directory --with-git
```

恢复目标必须不存在；不对原工作区执行 checkout/reset。交付恢复演练使用另一个新目录
`out/recovery-a733-ufs-readcheck-v170-smoke-20261022/`，实际结果在
`out/v170-checkpoint-verified.log` 和 `out/v170-recovery-verified.log`。
Windows 新 Git stat cache 可能显出原有 CRLF 工作文件相对 LF index 的额外修改；
逐字节恢复与状态文本相等是两件事，不为清理状态而转换/覆盖用户文件。
补充清单 `recovery-line-ending-note.json` 记录实际差异，不抹去历史开发过程。
