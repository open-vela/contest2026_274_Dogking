# v83 双 A55 SMP 构建与验证记录

## 状态与边界

v81 单核 LLM 基线已保存到 `a733-pre-smp-llm-v81` 标签与 Git bundle。
v82 是单核、只读拓扑诊断镜像；v83 是首个通过配置核验的双 A55 SMP
候选，逻辑 CPU0/1 的 MPIDR 分别为 `0x000/0x100`。v83 尚未实机验收，
没有启动 A76，也没有启用 LLM 多线程、权重缓存、持久 KV cache 或 NEON。

内核 SHA256：

```text
b4c0b2b8b9dc374fe9f186178a2e4c359f3ca44db696e4683f23bfb53a4434e3
```

镜像：`openvela-a733-cubie-a7z-sd-smp-2a55-v83-candidate.img`，大小
2,147,483,648 字节；嵌入内核 1,709,392 字节。镜像 SHA256：

```text
4fb8753729d08af72f4c9cb060408b65880119dde0f9642d798f2358fabafbbd
```

镜像内核回读 SHA256 与构建产物一致，ext4 检查通过，GPT 检查报告
`No problems found`。分区 4 的末尾非 2048 扇区对齐提示继承自基线分区表，
本次只替换分区 3 的内核，没有修改数据分区布局。

最终配置已核验：

```text
CONFIG_ARCH_HAVE_MULTICPU=y
CONFIG_SMP=y
CONFIG_SMP_NCPUS=2
CONFIG_SMP_DEFAULT_CPUSET=0x3
```

## 本次发现并修复的错误

1. 构建脚本保留了旧 CMake 缓存，尽管 defconfig 已启用 SMP，生成配置仍是
   单核。普通构建现在只清理固定的板级生成目录，并强制检查上述配置；
   `--incremental` 不能用于切换单核/多核配置。
2. 启用 `ARCH_HAVE_MULTICPU` 后，通用 ARM64 `arch.h` 默认使用 Aff0 计算
   `up_cpu_index()`。A733 必须使用 Aff1；仅在后面包含的 `chip.h` 定义同名
   宏会产生重定义，而且不能保证所有翻译单元取到正确映射。
3. 平台宏改为 `A733_MPID_TO_CPU`，CPU_ON 目标仍为 `cpu << 8`。通用头文件
   的最小 A733 条件补丁保存在 `patches/nuttx-arm64-a733-aff1-cpuid.patch`。
   构建期间临时应用，退出时恢复官方文件，不永久改动官方工作区。
4. 补丁 hunk 行数元数据错误已修正，并通过 `git apply --numstat` 语法检查。

## 实机测试顺序

先烧写 v82，运行：

```text
cat /dev/a733-hw
```

只有八个 `gicr[]` 都显示 `expected=yes`，且最后一个 frame 的 `last=1`，
才继续测试 v83。先保存完整串口启动日志，再运行：

```text
ps
cat /dev/a733-hw
dmesg
time "sleep 5"
dd if=/dev/mmcsd0 of=/dev/null bs=65536 count=16
free
```

检查是否出现 CPU1 IDLE、启动是否卡在 PSCI/第二核初始化、定时器是否正常，
并回归 Wi-Fi 与已有 NPU 功能。不要先运行约 474 秒的 LLM forward1 来判断
SMP 成败；LLM 仍是单线程，速度提升不是本阶段验收项。

无法进入 NSH 或发生异常时，保留故障串口日志，换回 v81/v82。不要覆盖基线
镜像。后续依次验证六 A55、全部八核及 A76，再推进矩阵并行与 LLM 优化。
