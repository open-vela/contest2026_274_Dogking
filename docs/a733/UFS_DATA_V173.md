# UFS v173：数据卷、官方FAT32和模型文件验收候选

v172用户日志已确认scratch写入/flush/回读/原件还原通过；末尾dd误报已修复，
见 [v172实机证据](../evidence/UFS_V172_RUNTIME_20261022.md)。
本版新增文件系统/模型盘入口，**尚待实机；不能把主机模拟或镜像构建称为上板通过**。

## 1. 范围和风险

仅用于已确认可清空的Kingston CY14-128测试Linux，LUN0，31256576×4096B，固定三段GPT。
新节点 `/dev/ufsdata` 是第三分区的数据视图：物理LBA **679936..31256414**，
30576479个4KiB块，共125241257984B。最后128块（512KiB）不导出给文件系统，
其中包含v172 scratch 31256496..31256511和相邻保护块；后续每次启动的scratch不会
写入模型文件。GPT、前两分区、SD启动、SD `/data` 都不修改。GPT第三分区原Linux
类型GUID保留，不宣称已做GPT类型转换；这是固定视图手动挂载，不是通用分区管理器。

`/dev/ufs0` 一直只读，不新增整盘写入、任意CDB、UNMAP、reset或错误后重试。
数据卷每条写命令最多64KiB，每回调最多1MiB；逐段精确回读，再CACHE10。
异常立即隔离；无异常后自动还原。**格式化会破坏第三分区原测试Linux内容**；
数据卷不是备份，断电也不能保证事务原子性。不要存放唯一一份资料。

使用已有官方NuttX块接口、BCH、FAT32、mkfatfs、mount、cp和cmp；没有另造文件系统。
FAT32单文件必须小于4GiB，现有Qwen 1.117GB模型满足。更大模型需要后续文件系统方案。
没有自动启动格式化/挂载/迁移，更没有从UFS替换现有SD启动链。

## 2. 每次启动先做的资格检查

只烧新的 `openvela-a733-cubie-a7z-sd-v173-ufs-data-candidate.img` 到已备份SD，
**镜像不要写入UFS**。NSH逐段执行，看到错误或quarantine=1就停止，不要继续下一段。
不要把整篇命令盲目粘贴到串口。SD上的旧模型和配置先保留。
“不修改SD”指UFS驱动不写SD；整张镜像烧录仍会覆盖SD对应扇区，烧录前先把当前SD
模型、Wi-Fi/Agent配置等另行备份，不能把旧基线镜像当成当前SD数据的备份。

```text
cat /dev/a733-ufs
echo board > /dev/a733-ufs
echo handoff > /dev/a733-ufs
time "echo quiesce > /dev/a733-ufs"
cat /dev/a733-ufs
time "echo dma-nop > /dev/a733-ufs"
cat /dev/a733-ufs
time "echo readcheck > /dev/a733-ufs"
cat /dev/a733-ufs
echo block-ro > /dev/a733-ufs
time "echo bench-ro=8 > /dev/a733-ufs"
cat /dev/a733-ufs
time "echo scratch-prepare > /dev/a733-ufs"
cat /dev/a733-ufs
time "echo scratch-test=ERASE-TEST-LINUX > /dev/a733-ufs"
cat /dev/a733-ufs
```

应见v173横幅；每级完成/停止/保护区通过，quarantine=0。
scratch必须 `completed=1 restored=1 error=0 writes=3 flushes=4 patterns=2`。
每次启动只允许一轮；EALREADY不是要求继续重复。资格状态只在当前启动有效。
需要永久保存原scratch备份，请在写之前自己导出 `/dev/a733-ufs-backup`；RAM备份重启丢失。

验证上次dd修复：

```text
time "dd if=/dev/ufs0 of=/dev/null bs=4096 skip=31256496 count=16"
cat /dev/a733-ufs
echo data-rw=ERASE-TEST-LINUX > /dev/a733-ufs
cat /dev/a733-ufs
ls -l /dev/ufsdata
```

应见data enabled=1、quarantine=0，stage为尚未格式化/挂载；发布时只做CACHE10，
不自动写文件系统。身份/GPT不匹配、未完成scratch或新入口缺失时不能手动绕过门禁。

## 3. 仅首次创建数据卷：会清空第三分区测试Linux

只有接受第三分区测试Linux被破坏、卷未挂载、上节全部通过时才运行下面一条。
**以后重启禁止再次mkfatfs，否则模型文件会被清空。**

```text
time "mkfatfs -F 32 /dev/ufsdata"
cat /dev/a733-ufs
```

这是快速创建FAT元数据，不是安全擦除或全盘坏块扫描。保守写后校验/flush会增加首次
格式化耗时，等待命令返回；报错则停止，不要重复格式化掩盖错误。data应仍无隔离且
verified-bytes持续累计。它是否能在真实UFS完成正是本轮新验收门槛。

## 4. 首次格式化后 / 以后每次开机：只挂载，不格式化

```text
mkdir /ufs
mount -t vfat /dev/ufsdata /ufs
ls /ufs
mkdir /ufs/models
echo v173-persistence-pass > /ufs/probe.txt
cat /ufs/probe.txt
cp /dev/a733-ufs-backup /ufs/backup-v173.bin
cmp /dev/a733-ufs-backup /ufs/backup-v173.bin
ls -l /ufs/backup-v173.bin
cat /dev/a733-ufs
```

目录已存在时mkdir报EEXIST不需要删除目录。备份文件应65536B，cmp一致、无隔离。
不要对已挂载的 `/dev/ufsdata` 做原始dd写入；原始只读边界检查可放在umount后。

## 5. 模型迁移验证：先复制，保留SD原件

先用ls核实SD模型确实存在，路径若不同只替换模型文件名：

```text
ls -l /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf
time "cp /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf /ufs/models/qwen2.5-1.5b-instruct-q4_k_m.gguf"
time "cmp /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf /ufs/models/qwen2.5-1.5b-instruct-q4_k_m.gguf"
ls -l /ufs/models/qwen2.5-1.5b-instruct-q4_k_m.gguf
cat /dev/a733-ufs
```

只有大小一致、cmp通过、无隔离才从新绝对路径加载。不要删除SD模型，不自动更改Agent
路由或云端配置。生产应用的现有检查点可直接使用新路径：

```text
aipetllm unload
time "aipetllm forwardfast /ufs/models/qwen2.5-1.5b-instruct-q4_k_m.gguf 9707"
aipetllm cacheinfo
aipetllm unload
```

先unload是为了避免RAM缓存让读速比较失真。比较load字段，不把模型compute当存储带宽。
持续速度仍需板测，不能把UFS3.1标称速率当成此单队列保守实现的实测速率。

## 6. 干净卸载和重启持久性

```text
umount /ufs
cat /dev/a733-ufs
time "dd if=/dev/ufsdata of=/dev/null bs=4096 skip=30576463 count=16"
cat /dev/a733-ufs
free
poweroff
```

重新上电，从第2节重新资格检查、启用data-rw，然后仅执行第4节的mkdir/mount/ls，
**不再mkfatfs，不先写probe**，先读：

```text
cat /ufs/probe.txt
ls -l /ufs/backup-v173.bin
ls -l /ufs/models
cat /dev/a733-ufs
```

首轮不要做带电拔卡/强制掉电测试。请保留从横幅到最后cat的完整日志；新门槛是
format→mount→小文件/二进制精确一致→模型cp/cmp→umount→重启后仍可读。
若文件系统失败但scratch通过，回退v172镜像仍可诊断整盘只读；它不能把已格式化的
Linux内容恢复。所有旧镜像、源归档、文档和项目恢复点保留。

## 7. 构建、回归和恢复

沿用 [v172构建](UFS_SCRATCH_V172.md) 的八个显式开关，再增加：

```bash
export A733_UFS_DATA_EXPERIMENTAL=1
export A733_BUILD_DIR="$OPENVELA_WORKSPACE/cmake_out/cubie-a7z_nsh_v173_ufs_data_candidate"
bash tools/build-a733-wapi.sh
```

正常defconfig全部UFS实验开关仍默认关闭。官方SDK仅临时overlay/patch，退出时还原；
新增 `patches/nsh-dd-large-seek.patch` 同样备份/还原。12个真实ARM64对象、51个关键
overlay源及NSH补丁hash分别检查，SDK还原清单170路径。CPU data状态无新DMA，
继续复用已验证read arena；ELF检查与scratch/bench/NOP/栈堆/BL31隔离。

主机命令（不需要板子、不读写物理设备）：

```bash
python3 tools/test-nsh-dd-large-seek.py "$OPENVELA_WORKSPACE"
python3 tools/test-a733-fat-volume.py "$OPENVELA_WORKSPACE"
python3 tools/test-a733-ufs-data.py
python3 tools/test-a733-ufs-scratch.py
python3 tools/test-a733-ufs.py
```

生产data core覆盖4KiB/64KiB/1MiB及卷首/中/末、171个故障；原scratch642故障保留，
驱动37场景，原board/handoff/quiesce/NOP/readcheck/link回归全跑。官方FAT规划/
格式化函数在/tmp稀疏普通文件上验证30576479×4096B、FAT32、备份boot、FSINFO和两份FAT。
这仍不代替真实FAT挂载/持久性验收。

开发前恢复点 `out/checkpoints/a733-pre-ufs-data-v173-20261022/` 保存934文件、3引用。
v172镜像SHA-256 `09f92202f63792afa9ee17f61a1d8235d29b3b377039efe88eddc1b462733eeb`。
新v173制品哈希与构建/源归档/恢复演练结果追加至 [ARTIFACTS](../ARTIFACTS.md)。
