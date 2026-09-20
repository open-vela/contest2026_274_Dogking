# v82 / v83 首次实机结果

## v82：拓扑验证通过

用户串口结果确认 CPU0 MIDR=`412fd050`，为 Cortex-A55；MPIDR=
`81000000`，逻辑索引 0。八个 GICR_TYPER 的 affinity 依次为
`0000,0100,0200,0300,0400,0500,0600,0700`，全部 `expected=yes`，
仅 frame 7 的 Last 位为 1。这验证了本板 Aff1 CPU 编号与 GICR 布局。

## v83：双 A55 启动、跨核执行通过

用户实机 `ps` 显示 CPU0 IDLE、CPU1 IDLE；`nsh_main` 的当前 CPU 为
`001`，亲和掩码为 `0x3`。因此 CPU1 不仅被登记，还已实际执行 NSH。
启动初始化日志主要记录于 CPU0；结合 NSH 在 CPU1 的结果，说明至少发生了
跨核任务执行。尚不能据此宣称所有 SGI 路径或长期调度稳定性均通过。

procfs、温度节点、TRNG、NPU 设备、SDMMC、GPT/FAT、Wi-Fi 固件与
网络设备初始化完成。日志未提供 Wi-Fi 关联/DHCP/吞吐、NPU 执行或 SD
持续读写测试，因此这些功能的 SMP 回归仍待验证。

## 观察到的问题与限制

- 第二核早期启动输出与 CPU0 的无锁调试输出逐字符交错，出现乱码；之后
  NSH 和 dmesg 可读。不能把这一现象单独认定为内存损坏或启动失败。
- 外部 UVC 探测返回 `-110`，DWC3 与端口读数为零。本日志不足以确定是否
  插入摄像头，也不足以把该问题归因于 SMP；摄像头链路仍未验收。
- Wi-Fi 上传等等待耗时比旧单核日志增加。需测量 `sleep` 的实际时间、
  计时 tick 行为与跨核延迟，不能先认定为无线吞吐回退。
- 当前只有 CPU0/1 两颗 A55；CPU6/7 A76 尚未启动。

## 扩核前的必要回归

### 第二次实机回归结果

用户提交的完整日志确认：`time "sleep 5"` 返回 5.0009 秒；1 MiB SD
块读取没有报错；NPU `selftest=0 apitest=0 guards=0`，提交/完成计数为
1/1。5 GHz 热点 706E 在重试扫描后发现，WPA2 和 DHCP 成功，获得
`10.195.194.22/24`，网关 `10.195.194.239`。网关 ping 10/10 成功，
RTT 9..25 ms，平均 13.5 ms。双 A55 基础回归通过，可以开始六 A55 候选。

该结论不包含长期负载、SSH/FTP 吞吐、YOLO 或 LLM 数值回归；扫描曾两次
未发现目标，第三次发现，因此无线扫描稳定性仍需观察，不能宣称所有功能
已完全验收。原始日志归档在 `archives/smp-v83/v83-board-regression.txt`。

```text
time "sleep 5"
dd if=/dev/mmcsd0 of=/dev/null bs=65536 count=16
free
wifi connect <SSID>
ifconfig
ping -c 20 <router-IP>
echo selftest > /dev/npu0
echo apitest > /dev/npu0
cat /dev/npu0
ps
dmesg
```

保存完整输出，并说明 sleep 的外部实际耗时是否接近五秒。完成基础回归后，
再生成六 A55 候选；通过后加入两颗 A76。不要修改已保存的 v81/v82/v83 镜像。
