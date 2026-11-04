# v175：既有 UFS 数据卷开机挂载与 Agent 模型自动选择

2026-10-22。以用户实机通过的 v174 为基线。v175 是新的自动挂载软件候选，
必须另做冷启动验收；不把 v174 的手动挂载成功改写成 v175 自动挂载通过。
旧镜像、源码、开发文档和恢复点全部保留。摄像头型号已确认是 **Radxa 13M214 / IMX214**。

## 已通过的 v174 基线

- 1117320736 字节模型复制并完整 SHA256 验证成功：
  `6a1a2eb6d15622bf3c96857206351ba97e1af16c30d7a74ee38970e434e9407e`。
- 连续读取 128 MiB 用时 1894 ms，约 67.58 MiB/s；不是 UFS 3.1 标称总线极限。
- 从 UFS 冷加载模型 15.9906 s，计算 0.6212 s；此前 SD 加载约 175 s。
- 28 层 CRC 与已通过输出一致；final `17a04f7d`，logits `7fdfee67`，argmax `6233`。
- 重启后完整 compare `result=0 verified=1`；长时间断电/异常断电耐久性未验收。

原始记录分别在 `docs/evidence/UFS_V174_COPY_READ_USER_20261022.txt`、
`UFS_V174_MODEL_LOAD_USER_20261022.txt` 和 `UFS_V174_REBOOT_COMPARE_USER_20261022.txt`。
通过文件是 `/ufs/models/qwen2.5-1.5b-instruct-q4_k_m.v174.gguf`；
旧同名无版本文件为 0 字节，本版不删除、不覆盖它。

## 开机行为

`a733services` 在独立任务中启动 `ufs --boot`，不占用 NSH 输入；Wi-Fi、SSH、Agent
继续各自启动，不等待 UFS。缺少 `ufs.auto` 记录时默认启用；损坏的记录按关闭处理。

一次性执行：`board → handoff → quiesce → dma-nop → readcheck → block-ro → data-existing`。
每一步失败即停止，不自动重试、复位、格式化、迁移或执行 scratch WRITE。
`data-existing` 只接受已限定 Kingston 身份/GPT 范围：先读 FAT32 主/备份启动扇区并
逐字节比较，再验证 FSINFO 与几何边界，最后完成原有 CACHE10 门禁，才发布可写数据视图。
只挂载 `/dev/ufsdata` 到 `/ufs`；不接受其他来源的 EBUSY 为成功，不改 SD 的 `/data`。

现有底层仍依赖引导固件留下有效、空闲的链路。**独立 PMIC/PHY 冷初始化尚未实现**；
若完全冷启动时固件未初始化 UFS，本版报告失败并回退，不盲写 PMIC 或反复复位。
不是只读文件系统：官方 FAT 挂载仍使用经过验证的 RW 路径；`/dev/ufs0` 整盘保持只读。
逐 64 KiB 写后回读与 CACHE10 未关闭，FAT 文件仍小于 4 GiB。

## Agent 选择规则

`aipet local` 的原有本地模型配置入口保持可用，模型选择支持 `auto`、`sd` 或绝对路径。
自动模式每次加载时检查 UFS 是否由本服务成功挂载、模型大小及 GGUF 头：

1. `/ufs/models/qwen2.5-1.5b-instruct-q4_k_m.v174.gguf`；
2. `/ufs/models/qwen2.5-1.5b-instruct-q4_k_m.gguf`；
3. 原 `/data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf`。

前两项已知模型要求恰为 1117320736 字节；0 字节旧文件不会被选中。已存的旧 SD 默认
路径也解释为自动模式，不修改配置文件本身。`sd` 强制 SD；其他显式绝对路径保持用户
选择。显式 UFS 路径不可用时回退 SD，不进行自动迁移或覆盖。
GGUF 头/大小验证不等于每次开机做 1.1 GB SHA256；全摘要用 `ufs compare` 显式检查。
本版不把任意未来摄像头/语音模型路径强行改到 UFS，只修改已经实现的 Agent 本地 LLM 加载。

云/本地路由配置不变。首次请求早于 UFS 挂载完成时可选择 SD，已经驻留的模型不会偷偷
切换；要验证 UFS 冷加载，请先等 `ufs status` ready，再卸载驻留模型后加载。

## 一轮板测（不得重新 mkfatfs）

先备份当前 SD 配置/凭据/模型，刷新的 SD 镜像，不是 UFS。首次启动等待后执行：

```sh
ufs status
ls -l /ufs/models
cat /dev/a733-ufs
services status
aipet local status
```

`ufs status` 应显示成功挂载，`cat` 应显示 `existing-FAT=1`；旧 scratch 计数不应因开机
自动增加。手动重复 `ufs start` 应保持幂等。记录完整日志；然后执行项目原有本地推理
验收，确认选择路径为带 `.v174.gguf` 的 UFS 模型，核对层 CRC、加载时间与计算时间。

```sh
aipet local model auto
aipet local status
aipet local unload
time "aipetllm forwardfast /ufs/models/qwen2.5-1.5b-instruct-q4_k_m.v174.gguf 9707"
aipetllm unload
aipet local ask "请简短介绍你自己"
```

`forwardfast` 是既有单token算子对照，`local ask` 才经过 Agent 本地调用入口；二者不是
语音识别。不把文件输入 `npuref` 当作 ASR 验收。需要强制回退时用 `aipet local model sd`；
测试结束可恢复 `aipet local model auto`。不要并发加载两个1.1GB实例。

```sh
ufs autostart status
ufs autostart off
```

以上关闭仅影响下次开机，不在线强制卸载模型卷。需要重新启用：

```sh
ufs autostart on
```

明确停止所有使用该卷的任务后，可执行 `ufs unmount`；忙时必须失败且保持挂载状态。
随后 `ufs start` 只重挂载已经限定的卷，不重跑初始化。外部直接 `umount /ufs` 后
ready 检查应失效。不要手动把另一个 FAT 挂在 `/ufs` 冒充本卷；这不是恶意重映射防护。

至少补测：正常完全断电冷启动、暖重启、关闭自动挂载时 Agent SD 回退；初始化失败
时 NSH/Wi-Fi 可用；并发打开模型时卸载拒绝；长稳读取和异常断电后的文件完整性。

## 软件验证与恢复

主机 ASan/UBSan 覆盖真实启动状态机的七处初始化失败、mount/umount 失败、幂等、并发
启动、后台创建失败和挂载失效；模型选择矩阵覆盖自动/SD/自定义/无效 UFS/越界路径。
FAT 验证器对照 SDK 官方 mkfatfs 实际输出；数据层保留旧故障注入并增加 BPB/备份/
FSINFO 拒绝路径；真实 ARM64 编译和整机链接另列构建记录，不等于硬件通过。

构建包装器临时暂存项目源后恢复 SDK，并记录 57 关键源、6 应用源和 SDK 173 路径。
镜像/源码/ELF/配置/hash/checkpoint 以 `docs/ARTIFACTS.md` 最新节和对应 out capsule 为准。
若新自动流程不通过，刷回 v174 并沿用旧手动挂载流程；不格式化已通过的 UFS 数据卷。
