# SMP 阶段 3：六 A55 + 两 A76（v85 候选）

前置恢复点为 `a733-smp-6a55-v84-basic-verified` 与 v84 镜像。
本阶段 CPU 数改为 8、默认 CPUSET=`0xff`；CPU0..5 为 A55，CPU6/7 为
A76。继续使用 TF-A PSCI CPU_ON，不直接修改 CPU 电源/PLL/PMIC。

新增 `aipetllm cpucheck` 仅做逐核亲和迁移、MPIDR/MIDR 读取和相同的
10 万步整数校验，结束后恢复任务原有亲和掩码。它不会改变 LLM 并行策略，
也不是生成速度基准。CPU6/7 必须实际执行此命令并读到 MIDR part=`d0b`，
才能确认 A76 执行检查通过；不能只看八个 IDLE。

实机命令：

```text
ps
aipetllm cpucheck
time "sleep 5"
dd if=/dev/mmcsd0 of=/dev/null bs=65536 count=16
echo selftest > /dev/npu0
echo apitest > /dev/npu0
cat /dev/npu0
wifi connect <SSID>
ifconfig
ping -c 20 <router-IP>
ps
dmesg
```

逐核 checkpoint 全部为 0，六行 A55、两行 A76，其他基础回归正常后，
才进入 A76 优先的 LLM 工作线程、权重内存缓存与持久 KV cache 优化。
当前不承诺频率、实际 tok/s 或八核长期稳定性。失败时保留完整日志并回退
v84；不得覆盖任何既有镜像。

## 构建与镜像存档

构建源提交：`92c2c35`。恢复后的 WSL 构建成功，生成配置已确认
`CONFIG_SMP_NCPUS=8`、`CONFIG_SMP_DEFAULT_CPUSET=0xff`；
`System.map` 包含 `aipetllm_cpu_checkpoint`。临时应用的官方 `arch.h`
补丁已恢复，未留下 `.orig` 文件。

- 镜像：`A733-A7Z-ALL-Files/openvela-a733-cubie-a7z-sd-smp-8cpu-a76-v85-candidate.img`
- 大小：2147483648 字节（2 GiB）。
- 内核：1709456 字节，SHA256 `8de14f44999f2f002bd55264b63bedce4608488e72b01fcc3e635af1b52372b9`。
- 镜像 SHA256：`2022121c014b4ebde2c4c0475943fa1f1b02a5bceba3d2579fa4a4e465ee7a69`。
- ext4 只读检查通过，内核回读一致，GPT 检查报告无问题。基础镜像
  分区 4 尾部非 2048 扇区对齐提示仍保留，不属于本次新增错误。
- 候选恢复标签：`a733-smp-8cpu-a76-v85-candidate`。
- 仓库恢复包：`A733-A7Z-ALL-Files/archives/smp-v85/a733-smp-v85.bundle`。
- 恢复构建日志：`A733-A7Z-ALL-Files/archives/smp-v85/v85-resume-build.log`。

此记录仅证明构建和镜像校验通过；八核启动、A76 身份和基础功能仍需
执行上面的实机回归，不能将候选版本描述为实机验证完成。

## v85 实机基本验证通过

用户回传结果：八个 IDLE 均存在，NSH 在 CPU1 执行。
`aipetllm cpucheck` 逐核迁移成功，全部 `checkpoint=0`，整数校验均为
`81e62f98`，结束后原有亲和掩码 `0xff` 恢复。

| 逻辑核心 | MPIDR | MIDR | 实测类型 |
| --- | --- | --- | --- |
| 0..5 | 81000000、81000100、81000200、81000300、81000400、81000500 | 412fd050 | Cortex-A55 / part d05 |
| 6 | 81000600 | 414fd0b1 | Cortex-A76 / part d0b |
| 7 | 81000700 | 414fd0b1 | Cortex-A76 / part d0b |

`time "sleep 5"` 为 5.0009 秒。用户同时报告 Wi-Fi 和 ping 正常，未提供
本次网络的详细统计；不能据此宣称长时间压力测试通过。本次未提供 SD
大块读取和 NPU 回归结果，仍需保留这些测试项。

早期启动文字明显交错。现象与多个核心直接输出串口的情况一致，但本次
未独立定位所有输出路径；不能将其描述为已修复，也不能仅凭该现象判定
SMP 失败。进入 NSH 后输出及逐核验证正常。

恢复标签：`a733-smp-8cpu-a76-v85-basic-verified`。
独立恢复包：`A733-A7Z-ALL-Files/archives/smp-v85/a733-smp-v85-basic-verified.bundle`。
候选镜像名称及哈希保持不变。下一步优先权重内存缓存和 A76 计算路径，
随后矩阵行并行、持久 KV cache 和 NEON；当前 LLM 尚未改为并行执行，
未测得新的 tok/s。
