# A733 SMP 阶段 1：双 Cortex-A55 候选

这是首个真正启用 SMP 的候选阶段，仅启动 logical CPU0 和 CPU1；二者都是
Cortex-A55，MPIDR 分别为 `0x000` 与 `0x100`。此阶段不启动 A76，也不启用
LLM 并行计算，目标只是验证：

- TF-A PSCI `CPU_ON` 可以启动 logical CPU1；
- A733 Aff1 到 NuttX logical CPU 的双向映射正确；
- CPU1 使用正确的 GICv3 Redistributor frame；
- CPU1 的虚拟定时器、SGI、idle task 和调度器稳定；
- 现有 Wi-Fi、SD、NPU、SSH/FTP 与 LLM 单线程基线不回退。

本阶段配置为：

```text
CONFIG_SMP=y
CONFIG_SMP_NCPUS=2
CONFIG_SMP_DEFAULT_CPUSET=0x3
```

只有 v82 `cat /dev/a733-hw` 的八个 `gicr[]` 行全部显示
`expected=yes` 后，才建议烧写此候选。若不能进入 NSH，立即换回已归档的
v81 或 v82 镜像，不覆盖故障卡上的日志。

