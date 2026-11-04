# UFS v172：固定窗口写入、flush、回读及还原候选

v171标准只读块接口已有用户实机通过。本版尚待实机写入验收，不是通用可写模型盘。
保留所有旧镜像/文档/恢复点，不格式化、不自动挂载、不迁移模型、不修改SD或 `/data`。
`/dev/ufs0` 仍没有write/ioctl，geometry仍只读。

## 范围、备份和失败边界

只匹配LUN0完整28字节身份 `KINGSTONCY14-128        0002`、31256576块×4096B、
与v171一致的三个GPT分区。固定窗口由第三分区末端减32向16块对齐：
**LBA31256496..31256511，共64KiB**，相邻保护块31256495与31256512。
这是用户已许可可清空的**测试Linux分区3内区域，可能有数据，不保证未使用**。
无任意LBA/LUN/长度/CDB、FORMAT、UNMAP或分区修改入口。

`scratch-prepare`只读：重复读取原64KiB和两个相邻块，复核头中尾/GPT entries CRC。
原件保存于CPU-only BSS，通过 `/dev/a733-ufs-backup` 只读导出。
**备份仅在RAM，重启/断电丢失。** 本固件不自动向SD保存备份；需要持久备份请在写入前
自行导出到电脑。若旧Linux有价值，先备份整盘，或只做prepare，不执行写入确认。

确认后先重新检查媒体与备份，先在未改写媒体时验证一次flush，再做两轮不同模式的WRITE(16)→SYNCHRONIZE CACHE(10)
→精确回读/相邻块核对，最后写回原件→flush→精确回读，并复核相邻块及元数据。
只有全部成功才completed=1；正常3次WRITE、4次flush（含写前资格验证）、patterns=2、restored=1。

超时、OCS/UPIU/SCSI/映射/保护区/回读异常立即隔离；**不重试、不reset、不强制清
doorbell、不释放DMA、不在错误后继续自动还原**，避免对未知在途事务二次写入。
备份仍可RAM只读导出。失败/断电可能破坏该Linux，不能保证异常后自动恢复。
flush成功也不是已经验证了突然断电持久化。

## 一轮上板命令（NSH）

只烧新的v172候选到已备份SD，**不要烧UFS**。逐段运行，不要盲目整篇粘贴。
任一步负值、隔离、保护区异常均停止后续。先确认v172横幅：

```text
cat /dev/a733-ufs
echo board > /dev/a733-ufs
echo handoff > /dev/a733-ufs
time "echo quiesce > /dev/a733-ufs"
cat /dev/a733-ufs
```

quiesce completed=1、quarantine=0、last=0后：

```text
time "echo dma-nop > /dev/a733-ufs"
cat /dev/a733-ufs
```

NOP completed=1、stopped=1、guards=1、quarantine=0后：

```text
time "echo readcheck > /dev/a733-ufs"
cat /dev/a733-ufs
```

readcheck completed/identity/samples=1/1/3、三对CRC相等、GPT通过、身份容量匹配后：

```text
echo block-ro > /dev/a733-ufs
time "echo bench-ro=8 > /dev/a733-ufs"
cat /dev/a733-ufs
```

bench completed=1、passes=2、bytes=16777216、两遍CRC相等后：

```text
time "echo scratch-prepare > /dev/a733-ufs"
cat /dev/a733-ufs
ls -l /dev/a733-ufs-backup
```

应见prepared=1、attempted=0、备份bytes=65536、original-crc32。
字符设备的ls长度不是有效备份长度。不要用cat把二进制原件直接输出到串口。
如需持久备份，可自行选择目的地复制（例如自行保存SD后FTP下载）；这是用户显式
选择，不是固件自动落盘。只想无损验证，到这里停止。

**接受上述64KiB测试Linux数据在失败/断电时可能丢失，才执行：**

```text
time "echo scratch-test=ERASE-TEST-LINUX > /dev/a733-ufs"
cat /dev/a733-ufs
time "dd if=/dev/ufs0 of=/dev/null bs=4096 skip=31256496 count=16"
cat /dev/a733-ufs
free
```

应见scratch completed=1、restored=1、quarantine=0、error=0、writes=3、flushes=4、
patterns=2、stopped=1、guards=1。每次启动仅允许一轮；重复返回EALREADY，不应重试。
请回传从横幅到最终free的完整日志。新写入通过前不要格式化、mount或迁移模型。

## 构建、源码与恢复

正常defconfig保持scratch关闭。候选需八个显式开关：

```bash
export OPENVELA_WORKSPACE=/path/to/openvela  # 改为包含nuttx/apps/vendor的SDK目录
export A733_TENSOR_EXPERIMENTAL=1 A733_UFS_EXPERIMENTAL=1
export A733_UFS_HANDOFF_EXPERIMENTAL=1 A733_UFS_QUIESCE_EXPERIMENTAL=1
export A733_UFS_DMA_NOP_EXPERIMENTAL=1 A733_UFS_READCHECK_EXPERIMENTAL=1
export A733_UFS_BLOCK_RO_EXPERIMENTAL=1 A733_UFS_SCRATCH_EXPERIMENTAL=1
export A733_BUILD_DIR="$OPENVELA_WORKSPACE/cmake_out/cubie-a7z_nsh_v172_ufs_scratch_candidate"
bash tools/build-a733-wapi.sh
```

源码 `a733_ufs_scratch.c/.h` 与私有 `a733_ufs_write_wire.h`；原READ编码器不开放写命令。
ABI核对本地厂商Linux `include/scsi/scsi_proto.h`（WRITE16=0x8a、CACHE10=0x35）、
`drivers/scsi/ufs/ufshcd.c`、U-Boot `drivers/ufs/ufs.h`（HOST_TO_DEVICE=0x02000000，
UPIU WRITE=0x20）。CACHE10依据Linux `drivers/scsi/sd.c:sd_sync_cache` 和U-Boot
`drivers/ufs/scsi/scsi.c:scsi_setup_sync_cache` 的实际调用，非仅凭0x91宏存在就假设支持。
无IMMED，LBA/count=0，无数据PRDT。不移植Linux忽略某些flush错误/重试的行为。
沿用同一mutex/READ arena/真实AT映射/IE0/固定HCI快照/cache/保护区/停止队列。
备份与工作缓冲区都CPU-only BSS，ELF门禁检查与DMA/栈/堆/BL31分离。

`python3 tools/test-a733-ufs-scratch.py`运行真实生产core的ASan/UBSan模拟回归；
另跑原UFS各层/音频回归、11个真实ARM64对象、49关键源暂存、整包ELF与镜像回读校验。
主机模拟不是实机写入证明。

v171只读实机通过回退镜像SHA-256：
`baa130bf61d45705bb3cacd13c07e95ec5716f2154b9593705d73ee2973473ac`。
开发前完整快照 `out/checkpoints/a733-pre-ufs-scratch-v172-20261022/`：925文件、3外部引用。
恢复只指向新的空目录，不覆盖用户工作树或SDK。所有旧开发文档保留。

## 本轮候选制品

镜像 `openvela-a733-cubie-a7z-sd-v172-ufs-scratch-candidate.img`，2GiB，SHA-256
`09f92202f63792afa9ee17f61a1d8235d29b3b377039efe88eddc1b462733eeb`。
内核SHA-256 `841704d8e301f7aca48128316b927caefc022d6ab4d9374a2b5f5c466a3a16f6`。
完整构建/布局/源对照/镜像回读/167路径SDK恢复已通过；完整恢复演练见本地
`out/v172-final-integrity.json`。新v172尚待板测，不能据此格式化或直接迁移模型。
