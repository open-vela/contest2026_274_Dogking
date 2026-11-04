# UFS v171：标准只读块设备与连续读取候选

v170 已有一次用户实机身份/容量、头中尾精确重复读取及 GPT CRC/基本范围通过。
本轮合并标准 NuttX 块接口、较大传输、连续读取对照，减少上板轮次。
v171 是新候选，不是实机通过的可写模型盘；旧 Linux、SD、原 `/data` 不动。

## 已实现与未实现

`block-ro` 仅在本次启动 quiesce→NOP→readcheck 完整成功后，复核当前静态映射、
保护区和停止 HCI，再用官方 `register_blockdriver` 注册 `/dev/ufs0`，mode=0444。
geometry 报告真实块数/块长及 `geo_writeenabled=false`，没有 write/ioctl/pass-through
回调，不自动注册分区、不 mount、不格式化。原 `attach-ro` 门禁仍关闭。

读取继续用已取得资格的独立静态 arena，不从堆重新分配 DMA，不复用 NOP arena。
READ(16) 每条最多 65,536B，每个块回调最多 1MiB、固定命令预算、独立时间/轮询上限。
每次回调重新检查 19 页 AT S1E1R/W 身份映射、保护区和当前 HCI；每条命令检查
OCS/UPIU/请求/PRDT/非预期改写并停止自有队列。互斥串行化诊断与块读取；
失败后不 reset、不超时重试、不释放 DMA，隔离后后续块读拒绝访问硬件。
参数/越界错误在触碰硬件前拒绝；发生中途错误时调用方缓冲区可能已有前缀，不能使用它。

`bench-ro` 默认每遍 8MiB、两遍；可显式选择 1..16MiB，每次启动只能尝试一次。
64KiB CPU 缓冲区是独立 BSS，不是新 DMA arena，不上 NSH 栈。
首个 64KiB 结果的前 16KiB CRC 必须与 readcheck 头样本一致，再比较两遍全量 CRC。
最终 bytes 必须等于 MiB×1,048,576×2，不能只看 CRC 恰好相等。
报告总耗时包含 cache、校验、CPU CRC 及控制开销，不是裸 UFS 标称带宽。
全局约 30 秒预算在每个已完成读取后检查；若仅 CPU 批次预算耗尽，HCI 已停止，
不假装为在途 DMA 超时。真正传输/响应异常仍隔离。

本工程当前配置未启用 ext4，NuttX `fs/` 中也没有找到现成 ext4 实现。
旧 Linux 分区不能直接按 vfat 挂载。可写块设备、flush/sense、scratch 写回读与断电一致性、
最终文件系统/模型迁移均未实施。独立 PMIC/M-PHY 冷初始化、多冷启动与长稳仍未通过。

## 一轮板测

只将独立新镜像烧到已备份 SD；不要烧 UFS。开机先确认 v171 横幅：

```text
cat /dev/a733-ufs
echo board > /dev/a733-ufs
echo handoff > /dev/a733-ufs
time "echo quiesce > /dev/a733-ufs"
cat /dev/a733-ufs
```

仅当 quiesce `completed=1 quarantine=0 last=0`：

```text
time "echo dma-nop > /dev/a733-ufs"
cat /dev/a733-ufs
```

仅当 NOP `completed=1 stopped=1 guards=1 quarantine=0 last=0`：

```text
time "echo readcheck > /dev/a733-ufs"
cat /dev/a733-ufs
```

仅当 readcheck `completed=1 identity=1 samples=3 stopped=1 guards=1 quarantine=0`、
三对 CRC 相等，真实身份/容量符合这颗 Kingston UFS：

```text
echo block-ro > /dev/a733-ufs
cat /dev/a733-ufs
ls -l /dev/ufs0
```

应见 `registered=1`、`block-v171: published=1`、节点只读。
原 `owns`/`dma`/`media.commands` 等部分字段来自未开放的旧 transport；
新的实际传输统计看 `reader-v171`、`readcheck-io` 和 `bench-ro-v171`，不是旧字段是否为零。
以下严格只读 UFS，输出是 `/dev/null`，不要颠倒 if/of：

```text
time "dd if=/dev/ufs0 of=/dev/null bs=65536 count=128"
cat /dev/a733-ufs
time "echo bench-ro=8 > /dev/a733-ufs"
cat /dev/a733-ufs
```

标准 dd 应完整读取 8,388,608B，无读错误；VFS 可能拆分回调，读取调用数不固定。
bench 应 `completed=1 error=0 passes=2 bytes=16777216`，两 CRC 相等，
`head16-crc` 等于本次 readcheck-sample[0] CRC，MBR 通常 aa55；保护区/停止/隔离仍正常。
耗时为 0ms 或字节数不全时，不计算带宽。

只有确认仍是 4096B 扇区、31,256,576 块，且上述都成功，才补 4GiB 偏移只读测试：

```text
time "dd if=/dev/ufs0 of=/dev/null bs=4096 skip=1048576 count=16"
echo handoff > /dev/a733-ufs
cat /dev/a733-ufs
free
dmesg
```

最后一个 dd 应读 65,536B，检验标准接口的 64-bit seek；不是写盘测试。
任何异常立即停止并回传完整启动/命令日志，不重发 NOP/readcheck/bench、不换 LUN、
不复位/格式化、不挂载旧 Linux、不迁移唯一模型副本。块节点保留到重启，无卸载/换盘入口。

## 构建与恢复

参照 v170 的构建/镜像封装方法，额外显式启用 `A733_UFS_BLOCK_RO_EXPERIMENTAL=1`；
它要求 `A733_UFS_READCHECK_EXPERIMENTAL=1`，七个实验开关全部显式设置。
使用新的 `A733_BUILD_DIR` 和输出镜像，不能覆盖 v170。
对应 Kconfig `CONFIG_A733_UFS_BLOCK_RO` 默认 n，运行入口不放在启动关键路径。
构建器持久备份 SDK，退出后必须核对 164 项原始输入恢复。
本轮完整构建日志 `out/build-v171-ufs-block.log`；最终交付/恢复清单见软件证据。
