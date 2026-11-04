# A733 完整构建、镜像生成与校验

v215 修正后台语音误用 generic IRQ console 的阻塞风险，增加分阶段加载日志、
LLM preflight 和限定 UFS 语音读取；构建/镜像摘要、无损迁移、测试与恢复见
[VOICE_LOAD_UFS_V215](a733/VOICE_LOAD_UFS_V215.md)。发布只采用 regression r3/build r2，
保留 v214；没有改 UFS PHY、格式化或宣称 1000 MB/s。

v214 常驻语音候选的命令、构建与验收边界见 [VOICE_RESIDENT_V214](a733/VOICE_RESIDENT_V214.md)。
保留 v213，通过软件检查后另存镜像，不覆盖旧版，不自动烧写。

## v180 本地LLM上下文（2026-10-25，内部编译，不发中间镜像）

`bash tools/build-a733-context-v180.sh out/NEW_RECORD`，独立目录
`cubie-a7z_nsh_v180_llm_context`。重复构建另选新记录并加`--incremental`。
核对全部暂存源、真实ARM64目标链接与SDK还原；版本与限制见
[v180](a733/LLM_CONTEXT_V180.md)。不得把当前工作树伪装成旧v178镜像。
v178/v179旧制品保留，完整语音链路补齐后再统一生成用户测试镜像。

## v179 离线语音软件集成（2026-10-25，不交付中间测试镜像）

当前源码的内部构建入口为 `tools/build-a733-voice-v179.sh out/NEW_BUILD_RECORD`；
已有构建树再次检查时另选新记录目录，并显式追加 `--incremental`。独立构建目录、
完整暂存源校验、共享SDK恢复和ELF边界检查保留，但**不制作或刷写新的SD镜像**。
LLM、通用ASR与本地TTS补齐后才统一安排用户整机测试。状态和缺口见
[v179](a733/OFFLINE_VOICE_V179.md)。不要拿当前v179工作树重新包装成旧v178镜像；
旧版本应在新目录按对应Git标签恢复，必要时显式设置真实`OPENVELA_WORKSPACE`。

## v178 本地 LLM 候选（2026-10-25）

详见 [MiniCPM5与语音说明](a733/LLM_V178_MINICPM5_AND_VOICE.md)。使用
`tools/build-a733-llm-v178.sh`、`tools/package-a733-llm-v178.sh`；先运行
`test-aipet-llm-v178.sh`、`test-aipet-audio.sh`及真实模型`test-minicpm5.sh ... infer`。
采用独立构建目录、SDK快照、源码归档和镜像名，不覆盖v177。候选只替换镜像分区3内核，
不操作板上UFS或Boot0/BL31；GGUF单独安装。完整烧录会覆盖目标SD，先备份重要数据。

## v177 UART4/STM32语音控制候选（2026-10-23）

板测接线、安全门禁及云ASR/本地NPU边界见
[AGENT_UART4_STM32_V177](a733/AGENT_UART4_STM32_V177.md)。恢复点和v175镜像保留。

```sh
# 协议对照测试需要下位机源码：在新目录clone后锁定提交，不修改原下位机工程。
git clone https://github.com/hxy299/STM32C8T6Mini-Play out/stm32-reference-v177
git -C out/stm32-reference-v177 checkout e83a9cac8a0e4a775a38910bdaf08c68bfdffc8c
export STM32_REFERENCE="$PWD/out/stm32-reference-v177"
bash tools/test-aipet-mcu.sh
python3 tools/test-a733-uart4-mcu.py
python3 tools/test-a733-got-layout.py ../../quickly-openvela
bash tools/build-a733-mcu-v177.sh
# 如需修复后重编，保留失败日志再用：
bash tools/build-a733-mcu-v177.sh --incremental
```

构建包装器记录官方SDK原始状态，临时应用项目overlay/patch，退出时恢复；
校验MCU关键源和既有NPU/UFS源码实际暂存字节。不得直接在SDK留下未归档改动。
新版本首次运行需不存在构建产物与对应SDK快照；重现其他目录时设置
`OPENVELA_WORKSPACE` 和 `A733_BUILD_DIR`，并为证据另选版本目录，勿覆盖封存的记录。
交付打包入口为 `tools/package-a733-mcu-v177.sh`，要求最终回归日志、源hash匹配和SDK恢复，
只替换已封存SD镜像中的openvela内核，不写物理SD/UFS，也不运行UFS清理工具。
具体镜像名、哈希、源码归档和未测项见 [ARTIFACTS](ARTIFACTS.md)。

## v175 UFS 自动挂载候选（2026-10-23整理）

v175完整编译通过，源码冻结与镜像摘要见 [ARTIFACTS](ARTIFACTS.md)，行为/板测见
[UFS_AUTO_V175](a733/UFS_AUTO_V175.md)。在项目根目录用NEW构建目录重现：

```sh
export A733_TENSOR_EXPERIMENTAL=1 A733_UFS_EXPERIMENTAL=1
export A733_UFS_HANDOFF_EXPERIMENTAL=1 A733_UFS_QUIESCE_EXPERIMENTAL=1
export A733_UFS_DMA_NOP_EXPERIMENTAL=1 A733_UFS_READCHECK_EXPERIMENTAL=1
export A733_UFS_BLOCK_RO_EXPERIMENTAL=1 A733_UFS_SCRATCH_EXPERIMENTAL=1
export A733_UFS_DATA_EXPERIMENTAL=1 JOBS=16
export A733_BUILD_DIR="$(cd ../../quickly-openvela && pwd)/cmake_out/cubie-a7z_nsh_v175_reproduce"
python3 tools/audit-a733-staging.py record ../../quickly-openvela out/sdk-before-v175-reproduce.json
bash tools/build-a733-wapi.sh
python3 tools/audit-a733-staging.py check ../../quickly-openvela out/sdk-before-v175-reproduce.json
```

构建运行中另终端可用 `check-a733-tensor-staged.py ../../quickly-openvela --ufs`
与 `check-a733-storage-staged.py ../../quickly-openvela` 保存57关键源/6应用源暂存摘要；
wrapper退出后检查原SDK173路径。实验开关保留受限底层能力，并不让boot调用scratch写入。
只对备份SD镜像文件用 `package-a733-kernel-image.sh` 打包，然后
`verify-a733-image.sh NEW.img KERNEL_SHA256` 复核；不以UFS为烧写目标。

确切v175冻结源为478文件源码包，后续工作树已增加未启用的v176摄像头探测代码；
要字节级复现v175，恢复其完整974文件checkpoint到**新目录**，不要回滚当前工程。
恢复工具同时保留原Git历史、index/working变更和旧开发文档，不操作原仓库Git。

2026-10-22最新：[v173数据卷候选](a733/UFS_DATA_V173.md)增加第九个显式开关
`A733_UFS_DATA_EXPERIMENTAL=1`，构建目录独立；NSH补丁也临时应用/还原，
170路径SDK快照、51关键源与dd hash分别校验，不覆盖任何旧镜像。

2026-10-22补充：[v172八个实验开关、受限写入风险与测试流程](a733/UFS_SCRATCH_V172.md)。
构建必须检查49个关键源、独立CPU备份BSS、167项SDK原件恢复；旧v171镜像不覆盖。

v153新增本地Kaldi前端：`bash tools/build-a733-local-voice-v153.sh`，独立输出与
SDK恢复检查。它不是NPU语音推理镜像；范围见[v153](a733/LOCAL_NPU_VOICE_V153.md)。

本文给出从公开协作仓库复现当前 openvela A733 固件的方法。命令在 Ubuntu
22.04/24.04 或 WSL2 Ubuntu 中执行。Windows PowerShell 只用于 Git 和文件管理，
不要直接代替 Linux 构建环境。

2026-10-19新增：`bash tools/build-a733-local-voice-v152.sh` 在独立目录构建本地
语音前置代码（22.05k WAV转换），不覆盖v151；它不是KWS/ASR/TTS全链路镜像，
当前不提供对应“本地全链路”打包入口。资料、恢复点和验证门槛见
[v152迁移说明](a733/LOCAL_NPU_VOICE_V152.md)。

## 1. 获取工作区

```bash
mkdir -p ~/openvela-contest
cd ~/openvela-contest
repo init \
  -u https://github.com/hxy299/hxy299-a733-share-hub.git \
  -b a733-cubie-a7z-share \
  -m contest2026_274_Dogking.xml
repo sync -c -j8
```

manifest 将比赛仓库检出为 `contest2026_274_Dogking/`，并把板级源码、应用和
必要兼容文件映射到 openvela 工作树。`packages_ai_agent` 等公共仓修改以仓内
patch 保存，构建脚本临时应用并在退出时恢复，避免污染公共仓源码。

## 2. 完整构建

```bash
cd ~/openvela-contest
bash contest2026_274_Dogking/tools/build-a733.sh
```

可选参数和环境变量：

```bash
bash contest2026_274_Dogking/tools/build-a733.sh --incremental
JOBS=12 bash contest2026_274_Dogking/tools/build-a733.sh

OPENVELA_WORKSPACE=/path/to/openvela \
A733_BUILD_DIR=/path/to/openvela/cmake_out/cubie-a7z_nsh \
bash contest2026_274_Dogking/tools/build-a733.sh
```

脚本会检查 openvela-first 架构边界，临时应用公共 Agent、readline 和 ARM64
兼容 patch，完成 Cubie A7Z 构建，核对 SMP/WAPI 配置并输出 SHA-256。无论成功、
失败或中断，都会恢复临时修改的公共仓文件。

主要产物：

```text
cmake_out/cubie-a7z_nsh/nuttx.bin
cmake_out/cubie-a7z_nsh/nuttx
cmake_out/cubie-a7z_nsh/System.map
```

`nuttx.bin` 带 ARM64 Linux Image header，必须通过 U-Boot `booti` 启动。

## 3. 生成可烧写 SD 镜像

本仓不重新分发 Boot0/SCP/BL31/U-Boot。以一份已经验证能启动 Cubie A7Z 的
基础 SD 镜像为输入，只替换 openvela 分区中的内核：

```bash
cd ~/openvela-contest
BASE=/path/to/known-good-a733-base.img
OUT=/path/to/openvela-a733-current-candidate.img
KERNEL=cmake_out/cubie-a7z_nsh/nuttx.bin

bash contest2026_274_Dogking/tools/package-a733-kernel-image.sh \
  "$BASE" "$OUT" "$KERNEL"
```

脚本拒绝覆盖基础镜像或已存在的输出文件，提取 GPT 第 3 分区，通过 `debugfs`
替换 `/boot/openvela/a733/Image`，回读比较内核，再把完整 ext4 分区写入新镜像。

## 4. 独立校验

```bash
KERNEL_SHA=$(sha256sum cmake_out/cubie-a7z_nsh/nuttx.bin | cut -d' ' -f1)
bash contest2026_274_Dogking/tools/verify-a733-image.sh \
  /path/to/openvela-a733-current-candidate.img "$KERNEL_SHA"
```

校验包括 `e2fsck -fn`、回读内嵌内核、`sgdisk -v`、镜像大小和 SHA-256。
依赖通常由 `coreutils`、`e2fsprogs` 和 `gdisk` 提供。

## 5. 发布边界

- `.img`、ELF、模型、A7PM、构建日志和本地恢复 bundle 不进入 Git。
- 可公开分发的最终镜像应上传 GitHub Release，并在 `docs/ARTIFACTS.md` 写明
  来源、大小、SHA-256 和实机验收状态。
- v128 是改动较少的显示/桌宠 hardware-passed 恢复点；v129 已完成 30 秒帧率与表情
  回归；v130 在 v129 副本上加入文件 RGB→NPU→LVGL 检测框候选。三者必须保留为
  不同文件，禁止用较新的候选覆盖已通过恢复点。各版本哈希与准确状态见
  `docs/ARTIFACTS.md`。
- 烧录会覆盖目标 TF 卡。先备份 `/data/models`、Wi-Fi、SSH/FTP、Agent 密钥
  和私人配置。凭据禁止进入 Git 历史或公开镜像。
