# v188：v185 启动卡死的加固与定位候选

状态：尚未实机验证。不能据本次日志宣称已找到唯一根因或已修复。

后续补记（2026-10-27）：用户已用本镜像进入 NSH；7 次次核启动调用
返回 0、24 个构造完成、NPU 自测通过。记录在
`docs/evidence/voice-v189/v188-board-test.txt`。本次启动关卡通过，不等于
反复冷启动/语音闭环通过，原有打包时状态及报告保持原样。

## 已知现场

用户的 v185 日志到达 `N4`，随后是多核并发、混杂的早期串口输出，
未观察到清晰的 `N8/X1/NSH`。上游 U-Boot 的 FDT/MMC 提示在此前
成功记录里也存在，不能将其直接判为根因。

已只读检查 v185 的实际 ELF，而不是仅凭当前源码推断：它编入了
`N5..N8`、`B0..` 和 `A1` 等标记，但并发输出可能淹没短标记。
构造表包含 24 项，其中包括新增的 ORT、Protobuf 与 libc++ 初始化。
v185 未启用 `BOARD_LATE_INITIALIZE`；`nx_bringup()` 在 idle 路径上
直接执行这些构造函数，idle 栈仅 8192 字节。应用音频 worker 的
256 KiB 栈对此启动阶段无效。此为已确认的初始化上下文风险，
不是已经实测证明的本次卡死根因。

ELF 的 `_image_end=0x459be000`，低于 `0x48000000` 固件保留区；
没有 TLS 孤立段。静态范围检查通过不等于动态栈和 SMP 已通过。

## 候选改动

1. 使用 NuttX 既有 `BOARD_LATE_INITIALIZE` / `AppBringUp`，其栈设为
   262144 字节、优先级 100；全局构造仍严格先于 NSH/应用启动。
   `board_late_initialize()` 不重复初始化外设；原有 NSH 的
   `BOARDIOC_INIT -> board_app_initialize()` 流程保持不变。
2. 仅诊断构建启用 `A733_BOOT_DIAGNOSTICS`。CPU0 逐核打印 SMP
   请求与返回、逐个打印构造函数地址及完成序号。
3. 仅关闭次核汇编启动横幅，保留次核异常输出。PSCI 返回错误向上传递，
   不再被 `up_cpu_start()` 无条件成功掩盖；失败时明确停止，不能在
   不完整 CPU 集合上继续八核调度。
4. 构建显式暂存并恢复 `nx_start.c`、`nx_bringup.c` 等启动源码，
   消除依赖 SDK 中未纳入暂存清单的旧修改；补丁和新增文件均纳入恢复检查。

不修改 NPU 模型、内存池大小、PMIC/DDR/CPU 时钟或 UFS 数据；
摄像头保持关闭，v186/v187 源码与既有通过版本保留。

## 构建与检查

在项目根目录的 WSL 中运行；输出目录必须不存在：

```sh
python3 tools/test-a733-boot-v188.py
bash tools/build-a733-voice-v188.sh out/general-voice-build-v188-r1 out/ort-static-v185-r2
python3 tools/audit-a733-boot-elf.py ../../quickly-openvela/cmake_out/cubie-a7z_nsh_v188_boot/nuttx
python3 tools/package-a733-boot-v188.py out/general-voice-build-v188-r1 ../../quickly-openvela/cmake_out/cubie-a7z_nsh_v188_boot ../openvela-a733-cubie-a7z-sd-v188-boot-candidate.img out/boot-image-v188-r1
```

构建源提交：`a38aa6f385515bc786add2d1f5477442fbd5bacb`。
打包脚本锁定原 v185 整卡摘要；除内核所在 ext4 分区外，逐范围比较新旧
镜像的全部字节摘要，包括模型 FAT、GPT 和其他启动分区，不改写原镜像。

主机函数体回归已验证 PSCI 错误向上传递、启动在首个错误停止，以及构造
顺序/空项跳过；启用 ASan/UBSan。它不模拟 SMP 硬件，也不执行 ORT。
已检查本 SDK 内核线程共享持久的 `g_kthread_group`，不会因临时
AppBringUp 线程正常退出而释放全局 iostream 引用的内核组标准流。

旧 v185 的镜像、ELF、二进制均保留；改动前恢复点：
`out/checkpoints/a733-pre-v188-boot-recovery-20261027`。
镜像与模型仍为私有候选，许可范围不因打包而改变。

## 已生成制品（非实机通过）

完整交叉构建 `out/general-voice-build-v188-r1` 返回 0；280 路径 SDK
恢复检查通过。最终 ELF 反汇编确认构造函数在 `nx_start_task` 中执行，
不再由 idle 路径的 `nx_bringup` 直接调用，AppBringUp 分配 256 KiB 栈；
逐构造进度及按 Aff1 过滤次核横幅均已编入，非仅改配置。

新私有镜像：`../openvela-a733-cubie-a7z-sd-v188-boot-candidate.img`，
2147483648 字节，SHA256：
`a18142fda0ed79896af9a357a190034668089368809f255cac841085cc41c44b`。

kernel 7007952 字节、runtime 92004352 字节；kernel SHA256：
`4597bf7f38a694b0a5f978485ea0a91bc230de55c86ddf95ce200f2a0ff90db4`。
最终 ELF SHA256：
`a90077a9233fd0ed4f6f58a8d97dc5d9ed807992d9a914e58530ad241526cd33`。

镜像内核回读、ext4 检查、GPT 检查通过；继承的第 4 分区非 2048-sector
对齐提醒仍在，无 GPT 错误。内核所在 ext4 分区之外两个完整范围的 SHA256
均与原 v185 一致，包含模型 FAT 和原有引导区。
记录见 `out/boot-image-v188-r1` 及 `docs/evidence/boot-v188`。
以上均不证明本次卡死根因、启动通过或完整语音通过。

本地标签：`a733-v188-boot-candidate-20261027`；
完成版恢复点：`out/checkpoints/a733-v188-boot-candidate-20261027`。

## 实机验收与故障定位

先备份当前 SD 上的模型、配置和录音，再烧录新候选。
从上电开始完整保留串口日志，先验证能进入 NSH；
暂不同时做语音或摄像头压测。

- `M0 v188-boot-candidate`：确认启动的是新内核，不是其他介质上的旧镜像。
- `SMP request cpu=...` 无对应返回：停在该次启动调用。固件若不返回
  SMC，内核无法用普通超时打断；不要无限等候或重复复位外设。
- `SMP return` 非零 / `STOP v188`：保留错误码和 CPU 编号。
- `N5..N8`：内核进入正常任务调度；其先后可与线程日志交错。
- `CTOR enter=地址` 无对应 `CTOR done index`：用**本版** ELF 及
  `audit-a733-boot-elf.py` 的构造表定位，不能套用旧 ELF 的地址。
  序号以十六进制打印；若表仍为 24 项，末项序号为 `0x17`。
- 所有构造结束但没有 NSH：继续按 `A2/X1/PR` 和板级日志缩小范围。

进入 NSH 后先运行：

```text
uname -a
free
ps
npu info
npu selftest
ufs status
```

Host mock/ASan/UBSan、实际交叉链接、镜像校验、启动成功、语音闭环
和延迟达标是不同验收层级。本版不提前宣称其中任何实机结果。
