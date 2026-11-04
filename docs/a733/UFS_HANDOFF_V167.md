# UFS v167：模型盘准备与固件交接观察

## 最新授权补充（2026-10-21）

用户进一步明确：现有Linux仅用于测试、无需要保留的数据，**之后可以清空UFS用于模型文件**。
因此“永久保留UFS上Linux”不再是产品要求。本文下方初始保护边界保留为开发历史，
以本补充为准：当前v167仍只读，未执行清空；将来先验证实际LUN/容量/目标身份、
排除SD/其他盘，并完成可靠读写与缓存flush后，再实施受控UFS分区/格式化/模型复制。
该授权不包括清空SD启动、SD配置/模型或其他介质。没有把用户的将来许可转换成
本轮未验证驱动上的立即写入动作；不会用现有Linux分区作为必须保存的理由拖延后续模型盘方案。

## 初始约定（已由上述授权补充替代，保留历史）

2026-10-21。用户确认 UFS 已装有以前测试用的 Linux：将其视为**必须保留的数据盘**，
不是可清空的实验介质。所有后续步骤禁止默认格式化、重新配置 LUN、擦除、分区、挂载写入或
迁移文件；也不修改 UFS 上的 Linux 引导内容。本版没有驱动介质写入口。

## 已取得的真实证据与尚未取得的证据

用户 v166 完整日志保存为 `../evidence/UFS_V166_USER_BOOT_20261021.txt`：

- U-Boot 报 AXP8191 VER_A、UFS G4/two lanes/FAST/rate2，块长4096、31256576块，
  对应128026935296B（128.0269GB/119.234375GiB）。这不是吞吐测试或 openvela 容量验收。
- U-Boot 输出校准值0x98620000/0x4d1d9753；不能把日志常量当运行时efuse provider。
- openvela到NSH、两次CCU/RTC采样一致，`stable=1 reads=26`；
  后续用户 `echo -n board` 复测外层与内层last均为0。
- `echo board` 的外层-22来自命令和换行分开write：实际采样成功后换行被误判无效。
  本版修复它，不把“无错误返回”伪装成新的硬件能力。
- 启动中真实PMIC识别改善了此前“型号未知”的事实，但**没有芯片端电压测量**。
- 这次日志的芯片/MAC信息与更早历史日志不同；证据只归属本次用户测试，
  不假定其他板/固件、冷启动和暖重启状态都完全一致。
- 没有 openvela HCE/HCS/队列/中断/doorbell/DMA映射快照，也没有读数据哈希或分区验收。

原v166封存/manifest不回写“通过”，新证据另存。加载字节数和版本标记与候选一致，
不冒充板上重新读回内核SHA。完整启动只有一次原始日志；没有两次完整冷启动验收。

## 为什么不立即把旧 attach 开关改成 true

本地厂商 U-Boot `drivers/ufs/ufs.c` 的 operational 流程会设置 IE、UTRL/UTMR base和runstop。
`sunxi-ufs-platform.c` 的 host_exit反而会关闭PHY/core/reset/clock。
因此“U-Boot读到了容量”不证明交给openvela时队列已停止、DMA可重绑定；
也不能根据源码存在exit就断言实际SD启动一定调用了它。
本次openvela看到相关门控仍开，下一步必须观察真实HCI交接状态，不猜测清理顺序。
禁止强停/清doorbell或直接换base，禁止重置现有Linux数据盘来绕开未知状态。

## 本版实现与安全契约

新增 `a733_ufs_handoff.c/.h`，独立、无写回调、无DMA分配或命令发送：

1. 默认关闭的 `CONFIG_A733_UFS_HANDOFF_PROBE`；只有专门候选通过
   `A733_UFS_HANDOFF_EXPERIMENTAL=1`开启，并要求原UFS实验开关。
2. `handoff` 是显式实验命令，开机注册和 `cat` 均不触发HCI读取。
3. 先重新两遍采样13个CCU/RTC项，严格匹配用户这次v166观察到的窄profile：
   PLL_REF=f8675f00、PERI0=ff126310、AXI=81000000、CFG=80000018、
   必要gate/reset（含MSI-lite1两路reset释放）、RTC 26MHz selector及UFS输出使能。
   未知profile在HCI前拒绝。
4. **这是继承固件已经上电并完成枚举的实验假设，不是电压/电源域/固件身份验证。**
   只有本次启动的U-Boot再次成功枚举UFS且无供电异常时，才按测试步骤执行。
   `gate-mask=7f`不能单独授权；没有force/user-token或读取失败后的时钟/PMIC回退。
5. 固定16项HCI寄存器，两遍最多32次读取；每次读取前重新检查始终可访问的CCU门控。
   只看 CAP/VER/AHIT/IS/IE/HCS/HCE/IAC、UTRL及UTMR base/doorbell/runstop。
   不读取错误层寄存器、不清IS、不写任何控制寄存器；改变时-EAGAIN，不自动重试。
   一次MMIO总线访问若挂死不能靠软件超时救回，profile不是任意板通用的电气担保。
6. 真实字段必须读回，不从U-Boot日志或已知128GB常量补造；
   `pending`、`needs-quiesce`只描述状态，不清队列或宣称 owner 已交接。
7. 原生产transport授权函数仍始终false，probe/attach-ro仍拒绝。
   手动HCI观察是单独窄入口，不授予DMA、块设备、SCSI或文件系统权限。
   board-v166中的 `hci-access=blocked` 指原通用资格/transport路径；
   新的明确例外以`handoff-v167`和`handoff-policy`说明，不代表已通过PMIC资格。
8. 修复单独CR/LF写入：消耗但不覆盖最后实际命令结果，不重复执行。
   支持NSH的完整命令+独立换行，不声称支持跨任意字节拆分的命令流。

这版没有冷启动初始化、停止固件队列、修改中断、分配DMA或发送NOP/SCSI。
不会注册 `/dev/ufs0`、读取分区、自动挂载/修复现有Linux文件系统。
原Linux系统的存在不等于它是vfat，也不能把SD `/data`假称UFS。

## 实机测试

使用独立 `openvela-a733-cubie-a7z-sd-v167-ufs-handoff-candidate.img`，2147483648B。
镜像SHA256：`3eedd949df5764ea0244a24eea1f96441c7b28e244489af040fae890e7e4a3c4`。
内核SHA256：`d40547912e98ab197ee984a5c0f070bc207d1eaf63be3742a950d9b823eebfe6`。
修订版构建、布局、SDK恢复、镜像提取及分区3以外逐字节保留检查通过；仍未实机运行v167。
**整卡镜像只烧到SD，烧录前备份SD的/data、模型、配置。绝不能烧到已有Linux的UFS。**
制作镜像未写任何物理设备；本工程不会代替用户选择/刷写磁盘。
保留v166和以前通过版本，不覆盖它们。

先冷断电启动并保存完整日志。确认本次U-Boot仍输出真实UFS容量/成功枚举；
若没有、出现初始化失败或供电异常，**不要执行handoff**，只发启动日志。
正常情况下进入NSH：

```text
free
cat /dev/a733-ufs
echo board > /dev/a733-ufs
cat /dev/a733-ufs
time "echo handoff > /dev/a733-ufs"
cat /dev/a733-ufs
free
dmesg
```

只执行一次handoff，不补运行旧版attach/format/mount/写入命令。
预期外层banner为v167，board sampler仍打印board-v166，这是刻意保持已通过模块版本。
`last-snapshot`仍是被阻止的旧transport字段，不是新采样结果；
看新增 `handoff-v167`、`hci`、`utrl`、`utmr`四行。

- 正常两遍读完：`stable=1 reads=32`，但这不是存储通过。
- `pending=1`或`needs-quiesce=1`：真实固件仍留有状态，**不是授权强停或清零**。
- `last=-18/handoff-profile-required`：窄profile不符，HCI读取应为0；保存日志，不force。
- `last=-11`：board/HCI在变化或门控消失，不自动重试。
- `last=-67/-5`：controller状态未满足或有错误；本版不清错/重启它。
- 任何卡死/FAULT：保留最后输出，冷断电后恢复旧SD镜像，不反复手动试寄存器。

本轮只需日志，不要求复制/上传UFS原始扇区或Linux文件；避免泄露既有系统数据。
后续进入数据读阶段前，先列出准确LUN/容量/只读范围，再解析GPT/文件系统，
严禁在未知格式上运行写入式fsck或自动创建分区。

## 软件验证、构建和恢复

```sh
python3 tools/test-a733-ufs-handoff.py
python3 tools/test-a733-ufs.py
python3 tools/test-a733-ufs-link.py
python3 tools/test-a733-ufs-board.py
A733_TENSOR_EXPERIMENTAL=1 A733_UFS_EXPERIMENTAL=1 \
A733_UFS_HANDOFF_EXPERIMENTAL=1 A733_BUILD_DIR="$NEW_BUILD" \
JOBS=8 bash tools/build-a733-wapi.sh
# 暂存期间从另一终端核对：
python3 tools/check-a733-tensor-staged.py "$SDK" --ufs
```

NEW_BUILD必须为SDK/cmake_out内新目录。默认普通构建仍关闭新入口。
原生产observer的14个预MMIO拒绝、80个读取/变化故障点与17个集成场景通过，
有/无实验开关均检查；旧3934个link故障点和board测试保留。
真实ARM64对象UFS224B、handoff192B、board208B、UIC112B、板初始化64B；
不把对象/主机检查当实机成功。完整候选另做Image布局/SDK恢复/镜像提取/hash/归档验证。
修改前恢复点 `out/checkpoints/a733-pre-ufs-handoff-v167-20261021/`：886文件、3项外部引用。
原始Linux开发目录只读，NPU/Agent/音频/LVGL与旧hwdiag安全修复保持不变。
