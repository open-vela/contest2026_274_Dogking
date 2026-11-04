# UFS v168 空闲队列停止候选（未完成UFS存储）

## 范围与依据

v167用户实机确认HCI可读、链路就绪，IE=0x30e15、UTRL/UTMR runstop均1、
两组doorbell为0，见[原始实机证据](../evidence/UFS_V167_RUNTIME_20261021.md)。
本版只关闭该固件留下的中断使能并停止空闲队列，不把“观察成功”升级成DMA所有权。

本地厂商`u-boot-2018-product-aiot-stable/drivers/ufs/ufs.c`的
`ufshcd_enable_run_stop_reg`和`ufs.h`确认0x60/0x80的队列使能语义；
Linux5.15`drivers/scsi/ufs/ufshci.h`提供相同寄存器定义。
**没有照搬Linux的`ufshcd_hba_stop`**：它会写HCE复位控制器，属于当前禁止动作。
vendor源码是行为参考，不冒充已验证的openvela生命周期或对本板所有版本的担保。

## 默认关闭、显式执行

新增独立`a733_ufs_quiesce.c/.h`和`CONFIG_A733_UFS_QUIESCE`，default n，
依赖HANDOFF_PROBE；构建候选需另外设置`A733_UFS_QUIESCE_EXPERIMENTAL=1`。
开机注册、cat、board、handoff均不执行quiesce，也不写UFS寄存器。

唯一写入口`quiesce`，一次启动只允许尝试一次：

1. mutex串行化，拒绝已有RTOS owner/块设备/隔离状态；重新采样CCU/RTC。
2. 复用v167窄board profile和双遍HCI观察，必须CAP=11870f1f、VER=300、HCE=1、
   活跃链路、IS/AHIT/IAC=0、两组doorbell=0，队列base合法且TM base仍0。
   仅接受实际观察的IE=30e15、两个runstop=1，或完全已停的三个0。
3. 按顺序只写：IE(0x24)=0、UTRLRSR(0x60)=0、UTMRLRSR(0x80)=0。
   每个写之前和确认之后都重新做双遍快照；原base及其他未拥有字段必须不变。
4. 每次MMIO前重新检查始终可访问的CCU gate/reset，写前/后DSB。
   写回读最多200次，独立20ms计时门限，每次未完成延迟100us。
   计时倒退会按无符号差值越界拒绝；计时器停止仍有poll次数界限。
   单次MMIO总线挂死不能被软件超时救回。
5. 写回调即使返回错误也可能已经影响硬件：从第一次尝试写起，任何错误都隔离至重启。
   不回滚/重启队列、不清IS/doorbell、不重试。重复命令不再MMIO，原结果保留。
6. 成功只说明三项控制稳定读回0。原固件base不改/不读指针/不释放，
   不分配DMA、不做cache维护、不注册/dev/ufs0、不发NOP/SCSI。

仍依赖**本次U-Boot成功枚举并保持供电、无其他固件请求生产者**的实验前提；
不是完整PMIC/power-domain provider，不证明SMMU/DMA地址窗或descriptor lifetime。
原`a733_ufs_board_hci_allowed()`仍false，probe/attach-ro继续拒绝，不能force绕过。
全局report的`quarantine`及`quiesce-v168`记录部分改变；失败后只保存日志并冷断电。

## 实机步骤（仅下一次烧录候选后）

镜像：`openvela-a733-cubie-a7z-sd-v168-ufs-quiesce-candidate.img`，2147483648B。
SHA256：`370545656b446c036e35e5121fb2b6153f3179d6d385cc66a6b04093766674e2`。
完整构建、38项暂存校验、159项SDK恢复、Image提取和分区3以外保留检查通过。
这是image-produced/build-passed，不是v168实机通过。

保留v167通过镜像和恢复点。本版镜像只烧SD，烧前备份SD配置/模型，不能烧UFS。
当前UFS测试Linux以后可清空，但本版不清空，也不改变分区/文件系统。

**本次启动必须仍看到U-Boot成功枚举UFS及容量；否则不要运行quiesce。**
进入NSH按顺序执行，quiesce只一次：
若quiesce返回失败或紧接的cat没有completed=1，在此停止；不要执行后续handoff。

```text
echo board > /dev/a733-ufs
echo handoff > /dev/a733-ufs
cat /dev/a733-ufs
time "echo quiesce > /dev/a733-ufs"
cat /dev/a733-ufs
echo handoff > /dev/a733-ufs
cat /dev/a733-ufs
free
dmesg
```

预期：banner v168；`quiesce-v168 completed=1 quarantine=0 writes=3`，
`quiesce-after ie=0 utrl-rsr=0 utmr-rsr=0`，原base不变。
第一次cat中的handoff四行仍是先前手动采样，cat不主动刷新HCI；
先看quiesce-after新结果，成功后再执行一次handoff刷新独立快照。
再次handoff应stable=1、pending=0、needs-quiesce=0、IE/runstop均0。
介质commands/read-bytes和DMA resident仍为0，registered/owns仍0；不是存储挂载成功。
只提交完整启动和上述日志，不要求上传UFS Linux数据或原始扇区。

任何负数、FAULT/卡死或quarantine=1：不运行attach、不重复quiesce，不写寄存器恢复，
保存最后输出后冷断电；必要时恢复旧SD镜像。不要使用格式化/fsck/挂载写入命令。

## 软件复现与后续

```sh
python3 tools/test-a733-ufs-quiesce.py
python3 tools/test-a733-ufs.py
python3 tools/test-a733-ufs-handoff.py
python3 tools/test-a733-ufs-link.py
python3 tools/test-a733-ufs-board.py
A733_TENSOR_EXPERIMENTAL=1 A733_UFS_EXPERIMENTAL=1 \
A733_UFS_HANDOFF_EXPERIMENTAL=1 A733_UFS_QUIESCE_EXPERIMENTAL=1 \
A733_BUILD_DIR="$NEW_BUILD" JOBS=8 bash tools/build-a733-wapi.sh
```

NEW_BUILD必须是SDK/cmake_out下新的专用目录；SDK通过项目脚本临时暂存并恢复。
主机测试460个读写故障点、14类前置拒绝、冻结计时器/延迟ACK/原base变化/报告边界通过；
集成19场景含实验开关关闭和写后门控消失隔离通过。7个ARM64对象编译通过。
完整构建/镜像/恢复证据见`../evidence/UFS_QUIESCE_V168_20261021.md`，不当作实机成功。

下一关口是可证明的DMA隔离/地址映射/所有权，再做NOP及只读身份容量，
随后才有只读块设备、分区识别、有限写回读、flush、受控建模型盘和/ufs迁移。
不把runstop读回0单独当作允许复用内存的fence。
