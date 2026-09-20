# A733 SMP 阶段 0：只读 CPU 拓扑诊断

本阶段保持 `CONFIG_SMP_NCPUS=1`，不唤醒任何次级 CPU。目标是在启用 SMP
之前，通过芯片寄存器验证逻辑 CPU、MPIDR 与 GICv3 Redistributor frame 的
一一对应关系，防止错误的 PSCI 目标或 GICR frame 令系统无法启动。

官方 sun60iw2 Linux 5.15 DTS 给出的拓扑为：

- logical CPU 0..5：Cortex-A55，MPIDR 分别为 `0x000`..`0x500`；
- logical CPU 6..7：Cortex-A76，MPIDR 分别为 `0x600`、`0x700`；
- MPIDR 的逻辑序号位于 Aff1，Aff0 恒为 0；
- GICR 基址为 `0x03460000`，frame 步长为 `0x20000`。

候选镜像启动后执行：

```sh
cat /dev/a733-hw
```

预期结果：

- `cpu-current` 显示 CPU0、Cortex-A55；
- `gicr[0]` 到 `gicr[7]` 的 affinity 依次对应 `0x000` 到 `0x700`；
- 每一行的 `expected=yes`；
- 只有最后一个 frame 的 `last=1`。

此阶段通过以后，才实现 A733 专用的 `arm64_get_mpid()`、
`arm64_get_cpuid()`/`up_cpu_index()` 映射并开始逐级唤醒测试。

