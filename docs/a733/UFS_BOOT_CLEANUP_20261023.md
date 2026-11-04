# 旧 UFS 启动链：定位与可恢复的定点禁用

2026-10-23。用户授权清理旧 UFS U-Boot，保留模型及原有开发过程。
本次只准备工具与离线验证，**尚未对开发板执行禁用，未生成新 RTOS 镜像**。

## 已确认的证据

- 用户最新日志两次正常从 SD 启动，BOOT0 `{4721ad08}`，U-Boot 为
  `2018.07-17-boot-aw2501-g4c7078cfdf7-dirty (Apr 29 2026)`。
- 设置 `ufs autostart on` 后，重启显示 `attempted=1 ready=1 error=0`，
  `stage=mounted-existing-FAT`。这是一次正常重启自动挂载通过，非长期压力测试通过。
- 模型 `/ufs/models/qwen2.5-1.5b-instruct-q4_k_m.v174.gguf` 为 1117320736 字节。
- 异常启动日志则进入 UFS 旧 BOOT0 `{9f6fab4f-dirty}` 和 2025 年的 U-Boot，
  `No MMC device available` 后扫描 UFS 分区并 `data abort`；未进入 openvela。

用户在通过固件中只读导出 LUN0 前 2 MiB，文件来自
`E:/models/ufs-boot-prefix-backup.bin`，另保存在本地
`out/ufs-boot-cleanup-20261023/ufs-boot-prefix-backup.bin`。
SHA256：`a50eea041526a85c63a96420a9940e34485dba1f02fdf41185217d57615e43cd`。
普通源码 Git 不包含该设备二进制；原 E 盘文件未修改。

| 项目 | 实盘位置 | 验证 |
| --- | --- | --- |
| 主 GPT | LBA1 / 0x1000 | CRC32 9eb7d026 |
| GPT 分区项 | LBA2..5 / 0x2000..0x5fff | CRC32 c76c09df，128×128字节 |
| BOOT0 主副本 | LBA32 / 0x20000 | 245760字节，校验和42bb8eb8 |
| BOOT0 备副本 | LBA258 / 0x102000 | 245760字节，校验和8e20abfa |
| 分区1 | LBA32768..65535 | 不修改 |
| 分区2 | LBA65536..679935 | 不修改 |
| 模型所在分区3 | LBA679936..31256542 | 不修改 |

所有 LBA 均为设备原生 **4096字节**，不是512字节扇区。
实盘主 BOOT0 在128KiB，不可照搬厂商 `ufs_main.h` 的16×512字节旧偏移：
该位置正处本设备 GPT 分区项！备副本偏移恰好与2064×512一致，不代表主副本也一致。
两份 BOOT0 内均找到 `9f6fab4f` 字符串，完整头校验有效；没有理由整盘擦除。

## 处理策略

不是删除某个文件，也不擦除 BOOT0/ATF/U-Boot 有效载荷。只清除两个 BOOT0 头中
`offset+4..offset+11` 的8字节 `eGON.BT0`，让旧链路失去有效引导标记。
写传输为两个原生4KiB块的读改写，除此之外字节不变。总共只改变16字节；
恢复时用原备份还原这两块即可，原校验和和有效载荷均保留。

这是候选措施；实际 BootROM 回退行为仍须冷启动板测。它不会解决 SD 接触/供电/
枚举问题，也不会消除正常 SD U-Boot 自身初始化 UFS 时打印的异常事件警告。
当前 UFS 驱动依赖 SD 引导固件留下有效链路，不应删除 SD 上的 U-Boot 或移除其 UFS 初始化。

## 当前可执行入口：Linux 工具

`tools/a733-ufs-boot-guard.py` 默认仅检查普通备份，不打开物理设备。
当前 openvela v175 的 `/dev/ufs0` 有意保持整盘只读，因此不能在 NSH 用 dd 写整盘。
本次没有偷偷开放全盘写权限，也没有提供未经验证的 U-Boot `phywrite` 命令。

电脑离线检查：

```sh
python3 tools/a733-ufs-boot-guard.py /path/to/ufs-boot-prefix-backup.bin
python3 tools/test-a733-ufs-boot-guard.py /path/to/ufs-boot-prefix-backup.bin
```

物理禁用只能在**从另一张/另一份 SD Linux 启动**、已识别目标节点后进行。
不得覆盖现有 openvela SD 或模型数据来强行满足这个前置条件。
工具、原始备份及 journal 目录须放在 SD 的持久文件系统上。
先 `lsblk -o NAME,MODEL,SIZE,LOG-SEC,MOUNTPOINTS` 核实，再用实际设备替换下例
`/dev/sdX`；不要把占位符误当确定的设备名。UFS及其所有分区须卸载、不得启用其swap。

```sh
sudo python3 a733-ufs-boot-guard.py ufs-boot-prefix-backup.bin \
  --action disable --device /dev/sdX --journal-parent /path/on/SD \
  --ack DISABLE-OLD-UFS-BOOT
```

工具独立核验 root、Linux、非符号链接的全LUN节点、A733 `4520000.ufs` 与
`sunxi-ufs-pltfm`、LUN0、KINGSTON/CY14-128/0002、容量、4096B块、GPT、
完全一致的2MiB备份；不接受任意其他设备。拒绝已挂载、swap、只读或已被声明的块设备。
目标在独占打开后再次核对 rdev 和重复读，备份与恢复日志必须在UFS之外的持久存储。

写前新建随机恢复目录，保存 original-prefix / before-prefix 并fsync文件和目录。
先禁用备副本，再禁用主副本。每块写入前持久化日志，fsync后全2MiB回读逐字节验证，
只允许两处标记变化；任何不一致均停止，不自动重试或回滚。
已成功禁用可重复执行，不再写；完整单副本禁用可继续，但半写/其他变化必须人工恢复。

恢复工具使用同一原备份，将 `--action disable` 改成 `--action restore`，其他参数不变。
恢复按主、备顺序；仍须全部身份/独占/备份验证。不能用一份已修改的前缀冒充原始备份。

## 状态与下一步

- 真实备份解析、主GPT/两份BOOT0校验：通过。
- 真实备份驱动的15项离线测试通过：GPT/原始备份拒绝、4种完整主备状态、
  其他字节变化拒绝、半写标记拒绝、仅两块写入且16字节差异、逐字节恢复、
  幂等、单副本完成后继续、短写/坏回读不重试、并发变化/日志失败时不写、短读拒绝。
- Linux 实盘身份预检/持久化/禁用/恢复：未上板执行。
- BootROM 禁用后行为、SD多次冷启动、UFS模型完整性复验：未执行。
- 若不能提供临时 SD Linux 环境，需另做 openvela 固件的受限维护入口；不以关闭整盘
  只读保护代替。当前通过镜像与模型盘不因此改变。
