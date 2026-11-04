# UFS v174：大块存取、离线进度与恢复检查

本版针对 v173 约1.1GB模型用普通 `cp` 长时间无反馈的问题。旧镜像和所有开发文档保留。
**这是大块文件I/O优化候选，不是已经实测的UFS3.1满速版本；板测前不承诺速度。**
软件/构建/恢复证据见 [v174记录](../evidence/UFS_BULK_V174_20261022.md)。

## 变化和边界

- 新增 `ufs` 正规应用：256KiB缓冲，后台执行，串口可查询进度或请求停止，不要求联网。
- `ufs copy` 边读SD边计算源流SHA-256，关闭并同步目的文件后重新完整读取目的文件校验。
  不多读一遍SD；只有完成且 `verified=1 result=0` 才允许作为已校验模型使用。
- 只创建**新名字**，已有文件返回EEXIST；不覆盖、不删除、不自动改路由。
- `ufs compare` 用256KiB缓冲作全文件逐字比较，大小不同立即失败。普通NSH `cmp` 为128B，
  不再推荐用于GB模型；普通NSH `cp` 仍是原1KiB接口，请按本页用新命令。
- `ufs readbench` 默认32MiB、最大256MiB，只计文件读耗时，不做CRC/SHA。
- 新增 `/dev/ufsdata-ro` **原始只读块视图**，无write/ioctl，不需要scratch或介质flush。
  注意本SDK的NSH mount没有-r参数，FAT bind强制请求可写介质；该视图目前只供原始块读，
  **不能直接只读挂载官方FAT**。不虚报只读文件系统已支持。RO/RW视图每次启动二选一。
- 继续保留v173每段最多64KiB写后精确回读、每次块写回调CACHE10。官方FAT目前没有在
  文件fsync处调用BIOC_FLUSH，不能为了快而直接去掉介质flush。本版没有改PHY/HS Gear。
- 15分钟软期限、零进度写EIO、有界EINTR处理。期限/stop在I/O调用之间检查，**不能强行
  打断卡在内核的调用**，不会杀死持有DMA/FAT锁的任务。串口status不是硬件超时恢复器。
- FAT32单文件<4GiB、固定Kingston/GPT身份门禁、尾部512KiB保留和SD启动链不变。
  不自动格式化、挂载、重试、复位、删除SD模型或迁移配置。

## 先检查旧文件：沿用通过的数据卷挂载流程

烧录只针对**已备份SD**，不要写UFS。整卡烧录会覆盖SD内容，当前SD模型和配置先另存。
新镜像名称 `openvela-a733-cubie-a7z-sd-v174-ufs-bulk-candidate.img`。
先用下面流程挂载现有卷，不格式化。每一阶段报错或quarantine=1就停止，不要继续盲贴。
由于官方FAT限制，这不是只读挂载；如需严格无介质写，仅进行block-ro/raw诊断，不进文件系统。

```text
cat /dev/a733-ufs
echo board > /dev/a733-ufs
echo handoff > /dev/a733-ufs
time "echo quiesce > /dev/a733-ufs"
time "echo dma-nop > /dev/a733-ufs"
time "echo readcheck > /dev/a733-ufs"
cat /dev/a733-ufs
echo block-ro > /dev/a733-ufs
time "echo bench-ro=8 > /dev/a733-ufs"
time "echo scratch-prepare > /dev/a733-ufs"
time "echo scratch-test=ERASE-TEST-LINUX > /dev/a733-ufs"
cat /dev/a733-ufs
```

只有scratch `completed=1 restored=1 error=0`、无隔离才继续。scratch位于文件卷排除的
尾部，仍需显式确认测试Linux可丢弃。已有FAT，**绝对不再执行mkfatfs**：

```text
echo data-rw=ERASE-TEST-LINUX > /dev/a733-ufs
mkdir /ufs
mount -t vfat /dev/ufsdata /ufs
cat /ufs/probe.txt
ls -l /ufs/models
ls -l /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf
```

mkdir已存在的报错无须删除目录。预期 v174 横幅、data enabled=1、无隔离。
旧模型应1117320736B；**目录项存在不证明文件完整**，即使大小相同也需比较：

```text
ufs compare /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf /ufs/models/qwen2.5-1.5b-instruct-q4_k_m.gguf
ufs status
```

命令立即返回，任务会继续输出串口进度；等 `active=0 phase=complete result=0 verified=1`。
大小不符会直接报错；内容不符打印首个不同字节位置。失败不要加载旧文件，也不要格式化。
需要停止时，在同一串口输入 `ufs stop`，等active=0；当前I/O尚未完成时会继续显示active=1。

读取性能可以先测这个已有文件的前32MiB，不要求它是完整模型：

```text
ufs readbench /ufs/models/qwen2.5-1.5b-instruct-q4_k_m.gguf 32
ufs status
```

等active=0后，再用128MiB复测。输出KiB/s是文件读吞吐，可能包含缓存，不能当模型推理速率。
做长期/冷读测试要干净卸载/重启，并记录完整日志。若不需要新复制，测完：

```text
umount /ufs
poweroff
```

## 需要重新复制时：同一次RW挂载即可

上一节已挂载且无隔离可直接继续。若已经关机，重新执行上节挂载流程，不重做mkfatfs。
若另外做过 `echo data-ro` 原始只读块诊断，必须重启选择RW，不能同时注册两个别名。
若models目录不存在才mkdir。使用**新文件名**，旧文件保留：

```text
ufs copy /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf /ufs/models/qwen2.5-1.5b-instruct-q4_k_m.v174.gguf
ufs status
```

无需 `time` 或 `&`，任务自己输出进度和总耗时；time只会计启动后台任务的时间。
同一时间只允许一个任务。阶段依次opening/copy-256KiB/sync-close/verify-SHA256/complete。
若之前已创建同名文件，返回EEXIST；换新的 `.v174b.gguf`，不自动覆盖/断点续传。
停止/失败时残片留下且verified=0，不要加载；下一次使用新的名字。
**确认SHA256、active=0、result=0、verified=1后**，再测试模型冷加载：

```text
ls -l /ufs/models/qwen2.5-1.5b-instruct-q4_k_m.v174.gguf
ufs readbench /ufs/models/qwen2.5-1.5b-instruct-q4_k_m.v174.gguf 128
ufs status
```

等readbench完成，接着执行：

```text
aipetllm unload
time "aipetllm forwardfast /ufs/models/qwen2.5-1.5b-instruct-q4_k_m.v174.gguf 9707"
aipetllm cacheinfo
aipetllm unload
cat /dev/a733-ufs
umount /ufs
poweroff
```

只比较load字段，compute不代表读盘速率。SD原件和Agent路由保持不变。
下一次重新上电可用上面的挂载流程再次compare，确认干净卸载后的持久性。
若mount失败，保留错误/完整状态，不重复格式化；读回小文件成功也不能证明整卷完全无损。

## 构建与恢复

沿用v173九个显式实验开关，新增应用由 `A733_UFS_DATA_EXPERIMENTAL=1` 同时启用：

```bash
export A733_TENSOR_EXPERIMENTAL=1 A733_UFS_EXPERIMENTAL=1
export A733_UFS_HANDOFF_EXPERIMENTAL=1 A733_UFS_QUIESCE_EXPERIMENTAL=1
export A733_UFS_DMA_NOP_EXPERIMENTAL=1 A733_UFS_READCHECK_EXPERIMENTAL=1
export A733_UFS_BLOCK_RO_EXPERIMENTAL=1 A733_UFS_SCRATCH_EXPERIMENTAL=1
export A733_UFS_DATA_EXPERIMENTAL=1
export A733_BUILD_DIR="$OPENVELA_WORKSPACE/cmake_out/cubie-a7z_nsh_v174_ufs_bulk_candidate"
bash tools/build-a733-wapi.sh
```

正常默认配置仍关闭。源码 `app/a733storage`、芯片只读视图、包装器均归项目持有；官方SDK
临时staging后还原。NuttX块层/FAT/mbedTLS是原官方实现，不另造文件系统或摘要算法。
严格只读FAT需要后续完善NuttX mountflags传递与FAT写操作拒绝，不能用虚假的可写geometry
绕过门禁；本版优先完成批量复制/校验链路，不扩大成文件系统改造。
`tools/test-a733-storage.py <SDK>` 和 `tools/test-a733-ufs-data.py` 使用真实生产代码
进行主机故障测试，`tools/compile-a733-storage.py` / `compile-a733-ufs.py`检查真实ARM64。
软件验证不替代板测；当前没有新增实机高速/压力/断电一致性证明。

开发前保存945文件/3外部引用恢复点 `out/checkpoints/a733-pre-ufs-bulk-v174-20261022/`。
v173旧镜像SHA256：`7e606b28ee9f169992447a3c50f6b904667a52436846a54c39d60d77c6648fbf`。
v174制品和最终恢复记录见 [ARTIFACTS](../ARTIFACTS.md)。
