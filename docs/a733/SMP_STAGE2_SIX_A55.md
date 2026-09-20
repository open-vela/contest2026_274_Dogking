# SMP 阶段 2：六 Cortex-A55（v84 候选）

前置基线是 `a733-smp-2a55-v83-basic-verified`，已完成双 A55 的定时、
SD 块读取、NPU 自检/API、5 GHz WPA2/DHCP 和网关 ping 实机回归。
恢复时使用该标签和 v83 镜像；v81 单核镜像仍保留。

本阶段仅调整配置：`CONFIG_SMP_NCPUS=6`、默认 CPUSET=`0x3f`。
CPU0..5 MPIDR 为 `0x000..0x500`，全部 A55；CPU6/7 A76 仍不启动。
不修改无线/NPU/LLM 算法，不承诺 LLM 加速。扩核验证通过后再启用八核。

## 构建与镜像

构建源提交：`7185d6f`。干净构建及配置核验通过，内核大小 1,709,456 字节。
以 v83 为基础，只替换分区 3 的 Image；ext4 检查通过，GPT 报告无错误，
内核回读 SHA256 与构建产物一致。镜像大小 2,147,483,648 字节。

```text
image: openvela-a733-cubie-a7z-sd-smp-6a55-v84-candidate.img
kernel-sha256: c5a3f336cab4b92864b78ae32dea141ab55a558a5a4a1cb8714bb3631084696b
image-sha256: 8716d5c4e4cc83685b163fbbb45aa3820d14eddba1bef1e2ff3deccd9a3c6f2b
```

这仅表示构建和镜像验收通过，六核实机验收仍待完成。官方 `arch.h` 已恢复，
后续 patch 使用 `--no-backup-if-mismatch` 避免额外留下 `.orig` 文件。

## 六核基础实机回归：通过

用户日志确认 CPU0..5 六个 IDLE，NSH 在 CPU1 执行，掩码 `0x3f`。
sleep 5 为 5.0009 秒；1 MiB SD 读取无报错；NPU selftest/apitest 均为
0、guards=0。706E 5 GHz 在一次重试后扫描到，WPA2/DHCP 成功；
网关 ping 7/7，零丢包，RTT 9..23 ms、平均 13 ms。

由此可以进入八核候选，但不涵盖长期负载、完整 YOLO/LLM 数值测试或
SSH/FTP 性能。早期串口仍有多核交错输出；无线扫描仍需观察。
原始日志保存为 `archives/smp-v84/v84-board-regression.txt`。

实机保存完整启动日志，执行：

```text
ps
cat /dev/a733-hw
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

预期六个 CPU IDLE 条目及 `0x3f` 亲和掩码，计时接近五秒；不能只凭
IDLE 条目宣称各核的全部中断/负载路径均通过。后续八核版需要逐核 MIDR
与亲和执行测试，确认 A76 实际执行计算。若启动卡住或基础驱动回退，回到
v83，不删除故障日志。
