# UFS v169：专用缓冲区的一次 NOP DMA 试验

## 目标和明确边界

v168一次实机启动与空闲队列停止已通过；本版只验证一条NOP OUT/NOP IN。
NOP不是SCSI TEST UNIT READY，也不是读扇区，不访问旧Linux文件，不注册块设备。
原`a733_ufs_board_hci_allowed()`仍false，probe/attach-ro保持拒绝。
没有把CPU VA=PA或文档拓扑当作已经通过的真实设备DMA。

当前状态及产物哈希见`../evidence/UFS_DMA_NOP_V169_20261022.md`。
源码/主机/对象/构建通过不能代替v169实机NOP结果。

本轮候选：`A733-A7Z-ALL-Files/openvela-a733-cubie-a7z-sd-v169-ufs-nop-candidate.img`，2GiB。
SHA-256：`d2e997667efd729f8672ce1258412f77cc84949b30d01ee8ae17fae1a1232d7d`。
v168通过版、修改前恢复点及本版完整项目恢复包均独立保留。

## 依据和假设

- 本地厂商U-Boot `drivers/ufs/ufs.c`把utrdl/ucdl的直接地址写入队列；
  用户实际U-Boot能完成容量读取，但这只证明它当时的固件地址可用。
- 厂商BSP `configs/linux-5.15/sun60iw2p1.dtsi`的UFS节点没有iommus绑定；
  IOMMU master表未列UFS。仅“没有绑定”本身不能证明硬件不翻译地址。
- A733 User Manual V0.92：PDF页425/印刷页405第10章列出两组IOMMU主设备，
  PDF页427/印刷页407第11.2节描述UFS独立AXI DMA接口；原手册表目页码与正文不完全一致，
  已查看实际页图。手册不提供本板当前DMA寄存器路由测量值。
- 本工程`a733_boot.c`是flat DRAM映射，chip.h限定stage1在BL31之前；
  对同一专用缓冲区再执行真实AT S1E1R/S1E1W逐页核对，不能只强制指针转换。

以上共同支持一个**窄的direct-AXI候选假设**；只有设备响应才能验证这次试验的实际路由。
不修改IOMMU、不以读取未上电的IOMMU寄存器来“补证明”，不宣称IOMMU硬隔离已建立。
仍要求本次U-Boot成功枚举并保持供电、没有其他固件请求生产者。

## 实现契约

1. 新`CONFIG_A733_UFS_DMA_NOP` default n，依赖QUIESCE；只允许手动`dma-nop`。
   开机/注册/cat不触碰HCI。每次启动最多一次尝试，失败不能force或重试。
2. 私有静态BSS arena=77824B：69632B payload两侧各4096B保护区，4096B对齐。
   不向heap/NPU借内存，不使用/读取原固件描述符内存；永远不free或复用到重启。
   编译后检查符号类型/大小/对齐及位于BSS、stage1、heap/stack/BL31之前。
   这是内存所有权隔离和越界检测，**不是硬件DMA防火墙**；坏地址仍可能破坏其他RAM。
3. 依赖同次启动成功quiesce；再做窄board profile及双遍HCI快照，
   IE/AHIT/IAC/IS/doorbell/两组runstop均0，HCE/HCS/版本/CAP匹配，
   base与本次quiesce-after一致，未知状态零写拒绝。
4. 逐页检查19页当前EL1读/写翻译，必须连续且VA=PA、低32位、处于0x40200000..0x48000000。
   AT/PAR访问在临界区内，保存并恢复原PAR_EL1；不修改页表。
5. 只构造一个slot0 NOP，tag=0、无PRDT/数据阶段，保持slot/tag关联。
   以新缓冲区的OCS=15哨兵和原先全零的响应区防止假完成，不依赖任意tag。
   准备保护图案后clean整个arena，DSB。
6. 写面仅为自己的UTRL base两字、UTRLRSR=1、doorbell bit0=1，成功后UTRLRSR=0。
   写前/后屏障、逐访问gate检查；每个关键阶段重读HCI，原UTMR/IE保持0。
   不写HCE、不清IS、不写doorbell-clear、不复位、不更改PMIC/PLL/PHY。
7. NOP等待独立1000ms/10000次上限；runstop确认独立20ms/200次上限。
   未拥有的doorbell位或额外IS事件立即拒绝；单次MMIO挂死仍无法靠软件超时救回。
8. 完成后invalidate整个arena，校验OCS、NOP IN类型/零LUN/tag/response/status、无EHS/数据段，
   保护页不变、请求/其余描述符/未使用payload全保持原样；允许改变范围只有OCS及32B响应。
   响应不正确、缓存/保护区异常或超时均隔离，不继续执行清理写入、不释放内存。
9. 成功后停止自己的队列并稳定复核；保留自己的base及arena到重启，不还原固件base。
   完成中断状态bit0保留（通常IS=1），IE仍0，不为得到“全0”报告清除它。
   `completed=1 stopped=1 guards=1`只代表NOP DMA通过，不解锁媒体命令。

`dma: resident`仍描述被禁用旧transport的动态缓冲区；新内存看`nop-arena`/`nop-dma`。
handoff/quiesce报告是各自最后显式执行的历史快照，cat不主动采样。
NOP成功后的base应改为本版私有arena地址，不能再要求等于固件bfeb4c00。

## 实机测试

镜像只烧SD，烧录前备份SD配置/模型；不要烧UFS。旧v168通过镜像及恢复点保留。
本次启动U-Boot必须仍成功枚举UFS，供电异常/未枚举时不执行以下主动命令。

```text
echo board > /dev/a733-ufs
echo handoff > /dev/a733-ufs
cat /dev/a733-ufs
time "echo quiesce > /dev/a733-ufs"
cat /dev/a733-ufs
```

只在quiesce显示completed=1、quarantine=0时继续：

```text
time "echo dma-nop > /dev/a733-ufs"
cat /dev/a733-ufs
```

预期`dma-nop-v169 completed=1 stopped=1 quarantine=0 pages=19 writes=5`，
`nop-dma guards=1 submitted=1 published=1`，响应以`20 00 00 00`开始。
只有成功才继续只读观察：

```text
echo handoff > /dev/a733-ufs
cat /dev/a733-ufs
free
dmesg
```

正常IE/runstop仍0，doorbell=0，HCE=1、HCS=10f、IS通常1，UTRL base为私有物理地址。
媒体commands/read-bytes、registered仍0。启动heap减少约专属arena及状态大小是静态保留，
不把free无动态变化当作没有预留RAM的证据。

任何负数/FAULT/卡死/quarantine=1，停止后续主动命令、保存日志、冷断电，必要时恢复v168。
不要重复dma-nop、attach、写IS/门铃清理、格式化或挂载。用户允许以后清空测试Linux，
本次不清空，也不动SD/其他磁盘。

## 复现

```sh
python3 tools/test-a733-ufs-nop.py
python3 tools/test-a733-ufs.py
python3 tools/test-a733-ufs-link.py
A733_TENSOR_EXPERIMENTAL=1 A733_UFS_EXPERIMENTAL=1 \
A733_UFS_HANDOFF_EXPERIMENTAL=1 A733_UFS_QUIESCE_EXPERIMENTAL=1 \
A733_UFS_DMA_NOP_EXPERIMENTAL=1 A733_BUILD_DIR="$NEW_BUILD" \
JOBS=8 bash tools/build-a733-wapi.sh
```

专用NEW_BUILD须在SDK/cmake_out下；暂存期间运行critical checker --ufs核对44项输入。
构建备份位于持久的out/sdk-staging；清理失败不得删除备份或直接重复打补丁。
先用tools/audit-a733-staging.py记录并核对SDK前后状态；正常构建退出后应逐项一致。
不要同时对同一个SDK运行两个暂存构建。
SDK自身无有效Git元数据的问题沿用v167记录，不虚构SDK HEAD；复现依赖同一SDK快照。
后续顺序：实机NOP → 同样隔离条件下只读身份/容量/有限读取 → 只读块/分区 →
写回读/flush与错误隔离 → 用户已授权的UFS模型盘创建和/ufs迁移。
