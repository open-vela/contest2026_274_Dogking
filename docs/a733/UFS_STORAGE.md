# A733 / A7Z UFS 数据存储适配（v164 第一阶段，未完成高速存取验收）

2026-10-22最新补充：v171标准只读块读及两遍8MiB/4GiB偏移已由用户实机通过，
见 [实机证据](../evidence/UFS_V171_RUNTIME_20261022.md)。下一阶段
[v172受限scratch候选](UFS_SCRATCH_V172.md)仅针对已识别测试Linux内固定64KiB，
手动显式确认后WRITE/flush/回读/还原，尚待实机写入验收，RAM备份断电丢失。
不开放任意写块API，不格式化、不挂载、不迁移模型。下文旧阶段说明原样保留。

## 用户目标及不变条件

最新v167：用户确认UFS已有测试Linux、无需保留数据，允许之后清空作为模型盘；
当前尚未擦除，先校验真实LUN/容量/介质身份与读写可靠性，SD内容不在清空授权范围；
新增[固件交接只读观察](UFS_HANDOFF_V167.md)，不是已授权的DMA/块设备接管。
不得按旧v164段落尝试格式化、挂载写入或强行attach。

2026-10-21 最新：见 [v166板级取证镜像及测试](UFS_BOARD_V166.md)。
真实供电/域/owner/DMA未验证，v166生产HCI门禁关闭；旧probe/attach命令返回-EACCES，
只有board命令采集CCU/RTC。本页下述命令行为是保留的 **v164历史边界**，
不是v166操作指南；尤其不能套用旧版probe/attach建议。旧v164镜像/记录原样保留。

2026-10-21 后续推进：新增 [v165 M-PHY/UIC生产核心](UFS_LINK_V165.md)，
源码/主机故障注入/ARM64对象通过；尚无板级adapter/caller，未生成可执行cold-start镜像。
它不改变下述v164现有transport的运行边界，不能据此执行冷初始化或宣称UFS可用。

2026-10-21 补充：用户实物丝印为 Kingston `UFS128-CY14`，系列在官方产品资料中确认。
供电/焊盘对照及新版只读取证工具见 [Kingston 器件核对](UFS_KINGSTON_CY14.md)。
具体订货后缀、板号、实际供电和 Linux 枚举仍未确认；不因此解锁冷启动/写入。
以下 v164 固件实现及镜像封存保持原状。

2026-10-20 用户已焊接 128GB UFS 3.1，要求将 LLM 模型及高频访问数据转入 UFS。
这取代历史记录中的“测试板无 UFS”硬件状态；**并不等于芯片已成功枚举**。
新旧功能之后由用户统一板测。本轮不连接开发板、不烧录、不格式化、不修改介质。

- SD 启动、`/data`、Wi-Fi/蓝牙、音频 x5、NPU 和显示的现有行为不改。
- 原镜像、历史开发文档和 v163 候选全部保留；UFS 不进入启动关键路径。
- 不初始化未知分区、RPMB、Boot LUN，不执行 FORMAT/UNMAP/擦除，不自动搬迁模型。
- 不把“UFS 3.1 芯片”当作 A733 已协商到相应 Gear/速率的证据。
- 摄像头 IMX214 与语音的待办继续保留，见 `VOICE_CAMERA_RECOVERY_ROADMAP.md`。

优化目标应分开计时：冷启动模型加载、文件读写吞吐、推理 compute 和端到端延迟。
已有 RAM resident/cache-hit 路径的 compute 不直接受存储替换加速；UFS 首先改善首次加载和
实际需要读盘的场景。不能把热缓存吞吐或 CPU/NPU 计算时间混成 UFS 性能。

## 当前实现边界

新增 `a733_ufs.c`、`a733_ufs_wire.h`，作为板级适配放在标准 NuttX 块设备之下。
`CONFIG_A733_UFS_READONLY` **默认关闭**；实验构建通过
`A733_UFS_EXPERIMENTAL=1` 临时启用，不修改源 defconfig 默认值。

开机仅注册 `/dev/a733-ufs`，不访问 UFS HCI、不编程时钟/复位/供电、不分配 DMA。

| 入口 | 实际功能 | 不代表什么 |
| --- | --- | --- |
| `cat /dev/a733-ufs` | 软件状态、CCU 状态、上次 HCI 快照 | 不探测介质，不是高速/容量验收 |
| `echo probe > /dev/a733-ufs` | 先检查 CCU gate/reset，再读 CAP/VER/HCS/IS | 不启动链路，不证明焊接成功 |
| `echo attach-ro=0 > /dev/a733-ufs` | 明确接管固件已初始化、active、idle 的 LUN0，执行真实 NOP/INQUIRY/READ CAPACITY(16) | **不是冷启动初始化**，也不配置 UFS LUN |
| `/dev/ufs0` | 上述命令全部成功后才注册的只读块设备，READ(16) | 不自动解析分区，不自动挂载，无写接口 |

**最重要的限制：正常 SD 启动很可能没有初始化 UFS。此时 probe 返回 `-EAGAIN`，
或 attach-ro 返回 `-ENOLINK`，属于明确未满足前置条件，不应反复重试或声称存储通过。**
Linux 启动过一次也不保证切回 openvela 后链路仍然保留。不得用硬编码“成功”或
回退 SD 数据掩盖这个限制。

当前传输仅在 HCE 已开启、device-present/UTRL-ready、无 host/device error、
IE/AHIT/doorbell/task-list 均符合空闲条件时接管。不会主动停止其他固件请求，
不会自行退出 Hibern8。单槽、一次一个请求，64KiB 上限；每条命令 3s 墙钟及轮询双重边界。
字节编码避免 C bitfield/packing/endian 隐患；NOP、INQUIRY、READ CAPACITY(16)、READ(16)
之外的命令拒绝，UTRD 没有 host-to-device 数据方向。

DMA 使用独立 1024B 对齐的 69632B 内存，当前 A733 平坦 VA=PA 映射；
记录控制器 capability，但本版仅接受完整落在4GiB以下的分配：CAP64不证明SoC互连DMA窗。
超界直接释放未发布的分配并报错，不重试/截断/侵占NPU池；clean → DSB → doorbell →
确认完成 → invalidate → DSB → 校验 OCS、UPIU type/tag/LUN/status/residual → 复制数据。
超时/坏响应/传输错误隔离到重启，不 reset/retry，不释放仍可能被 DMA 引用的内存。
成功接管也保留内存到重启，不支持卸载或运行中切 LUN。未提交前分配地址不满足才可释放。
块设备 `write=NULL`、`ioctl=NULL`、0444，geometry 标记不可写。

**CPU VA=PA 不证明设备 DMA 未经过 SoC SMMU。当前还没有本板 UFS DMA aperture/bypass
的实机证据，attach-ro 也不会检测/重编 SoC IOMMU。必须先确认设备地址确为 CPU 物理地址、
固件没有并发 owner、无残余请求；仅 HCE/HCS ready 不足以满足这个门禁。当前候选先限于
注册/probe，不建议在任意固件交接状态下直接执行 attach。超时后保存日志并冷断电恢复，
也不把一次暖重启当作设备 DMA 已停止的证据。**

## 仍必须实现的内容（不可跳过）

1. 已收到 Kingston UFS128-CY14 丝印；继续核实订货后缀、VCC/VCCQ 及旧 VCCQ2/NC
   球连接、A7Z 硬件版本、实际电压、焊接及去耦，不能给所有系列套三路电源。
2. 冷启动板级 adapter：正确 PMIC rail、DCXO/refclk、store/MSI/AXI/config clock/reset，
   M-PHY efuse 校准及 SRAM 初始化、DME/UIC link startup、NOP/query/device-init。
   厂商 Linux 明确有专用流程，不能仅打开 HCE 就假定设备工作。
3. 读取 device/geometry/unit descriptors、确认目标 LUN 容量、逻辑块长与器件信息。
4. 固定范围重复读、跨边界/大文件 hash、热重启/冷启动及 fault/timeout 验证；
   补齐真实NuttX下Ctrl+C/任务退出、锁竞争/owner死亡与DMA在途生命周期验证，不能用主机mutex
   shim替代。当前同步内核锁不是经过实机取消/并发验收的传输调度器。
   未通过前不开放写入。
5. 独立 scratch 分区/文件明确授权后的写回读及断电一致性；实现缓存 flush 与安全错误处理。
6. GPT/MBR 边界/CRC/重叠检查后注册只读分区；确认实际文件系统再挂载 `/ufs`。
   当前 SDK 文件系统目录未发现 ext4，不能把 Linux 常见的 ext4 当作 vfat 挂载。
7. 再谈多 PRDT/queue/IRQ、HS Gear/lanes 协商、吞吐、温度与长稳。
   单槽轮询实现首先是正确性入口，不是性能优化完成。
8. 模型复制后校验长度与 SHA-256，再显式切路径；SD 原文件保留到 UFS 长稳通过。

本地 LLM 已支持绝对模型路径：`aipet local model /ufs/models/模型.gguf`，
本轮不需要改路由。**仅在 /ufs 真正挂载且模型校验后才能执行**，切换前按现有接口卸载
resident 模型。NPU/audio 某些入口仍限制 `/data`，它们要单独审计，不能统一字符串替换。
不把 `/ufs` 假挂为 SD 目录，以免跑了 SD 却误报 UFS 性能。FAT32 单文件 4GiB 限制也需考虑。

## Linux 一次性无损取证工具（以后统一板测）

脚本：`tools/collect-a733-ufs-linux.py`，只用 Python 标准库；不下载、不联网、不调用
任何刷机/分区/格式化工具，不读 API key/SSH key/模型内容。

在 **A7Z 官方 Linux**，将单个脚本复制到新测试目录后：

```sh
sudo python3 collect-a733-ufs-linux.py
```

默认仅采集 UFS 相关 DT 原始属性、sysfs LUN 身份/容量、筛选后的 regulator/clock/dmesg，
新版另沿控制器 of_node/供电 phandle 精确关联 regulator 电压读回；不依名字或 min/max 猜测。
不读取原始块数据。输出新 `a733-ufs-linux-随机串/` 与同名 `.tar.gz`，带 SHA256SUMS；
每次都是新结果，保留旧包。无 UFS 时明确输出 `(none; not accepted)`。
“无写入”指不直接写原始块设备；生成报告会新建普通文件。如果输出目录位于UFS文件系统，
报告文件自然会占用UFS空间，不声称整个系统没有任何磁盘写活动。
该报告仅取证，不自动把 Linux 板测升级为 openvela 通过。

只有清单确认 `/dev/sda` 是 A733 UFS whole LUN 后，才可选择：

```sh
sudo python3 collect-a733-ufs-linux.py --benchmark /dev/sda
```

工具仍独立核验 sysfs 路径必须是 `4520000.ufs` 且 driver 为 `sunxi-ufs-pltfm`，
major/minor 必须匹配。拒绝分区、symlink、USB/SATA/NVMe/SD 和任意路径。
仅 O_RDONLY|O_DIRECT，从 32MiB 偏移读 64MiB 并 hash，绝不写盘，不全局 drop_caches。
所报吞吐包含 SHA 开销；每次 read 前检查 30s 预算，但 Linux 阻塞 read 本身仍受内核超时控制，
不能保证损坏设备上的单个 syscall 30s 内返回。基准是可选的，默认不执行。

## openvela 手动检查（新候选以后测，不要求现在烧录）

```text
free
npu info
ls -l /dev/a733-ufs
cat /dev/a733-ufs
echo probe > /dev/a733-ufs
cat /dev/a733-ufs
```

记录返回码及完整状态。如果 gate 不开/链路未初始化，到此停止；不要改 PMIC、复位、
原始寄存器或格式化。已确认固件交接前置条件后才测试 `attach-ro=0`；失败保留日志，
`quarantine=1` 时不再提交命令。成功才会出现 `/dev/ufs0`，还不能直接假设可挂载。
负例：未知命令、`attach-ro=8` 应拒绝；UFS 不存在/未初始化时 NSH/音频/Wi-Fi 应保持正常。

## 离线回归及构建

```sh
python3 tools/test-a733-ufs.py
python3 tools/test-a733-ufs-link.py
python3 tools/test-ufs-linux-collector.py
python3 tools/test-project-checkpoint.py
python3 tools/compile-a733-ufs.py \
  ../../quickly-openvela/cmake_out/cubie-a7z_nsh_v163_frontend_audit_r2_candidate \
  out/new-ufs-arm-object-check

A733_TENSOR_EXPERIMENTAL=1 A733_UFS_EXPERIMENTAL=1 \
A733_BUILD_DIR=/绝对路径/quickly-openvela/cmake_out/全新候选目录 \
JOBS=8 bash tools/build-a733-wapi.sh
```

对象检查使用现有 ARM64 compile_commands，强制包含当前 overlay 的外设声明，不写 SDK；
首个对象检查只因板级声明读取了旧 SDK include 而失败，修正检查工具后 UFS 最大静态栈224B、
board64B 通过，失败日志保留 `out/ufs-v164-arm-objects/`。
主机 MMIO/模拟器测试不等于电气、DMA 一致性或器件声称的速度验收。

## 参考来源及许可边界

官方产品资料列 A7Z 为 2-lane UFS 3.0 可选，并不能据此担保焊接的任意 UFS 3.1 芯片：
[Radxa A7Z](https://radxa.com/products/cubie/a7z/)、
[官方下载/硬件资料](https://docs.radxa.com/en/cubie/a7z/download)。

本地参考位于仓库同级，只读研究；未把 Linux 生命周期/源码表格直接复制到 Apache RTOS 驱动：

| 源文件 | SHA-256 | 关键事实 |
| --- | --- | --- |
| bsp-product-aiot-stable/drivers/ufs/sunxi-ufs-platform.c | `0de8056e6c693656ec246ec7979b1981d16cf1f43ab05797e50fe85fe67d9dddb` | 独立 PHY/RMMI/efuse/SRAM/connection 流程，HS G4 上限配置不是实测结果 |
| bsp-product-aiot-stable/drivers/ufs/ufshci-sunxi.h | `964fb3d6e2b6829fac420fa33c57c4df00154cd5f34c329a6f9f2ab9af95c019` | vendor CFG0x200、CLK_GATE0x204、PPU_ACTIVE0x32c；hibernation 供电有总线挂起风险 |
| bsp-product-aiot-stable/configs/linux-5.15/sun60iw2p1.dtsi | `fa7f4d8080119c7cdaec8b25061a286e54d3c823b3865b3399fff9c7c3cb430f` | HCI0x04520000、SPI165（GIC ID197）、专用 clocks/resets，compatible allwinner,sunxi-ufs-v0 |
| allwinner-device-device-a733-v1.4.6/configs/cubie_a7z/linux-5.15/board.dts | `0189e9cd071a4b4f7b987804ae294da4fc7e9c8c9179f3d09ae4bf39d8fb4c89` | vcc=dldo6，vccq/vccq2=dcdc8；regulator 范围500000..3400000不是实际设定电压 |

标准 UFSHCI/UPIU/SCSI ABI 与位域另交叉查看同级 Linux `drivers/scsi/ufs/ufshci.h/ufs.h`、
U-Boot `drivers/ufs/ufs.c`；本项目以独立字节编码和测试实现，不引入 GPL 驱动代码。
当前 openvela `nuttx/drivers` 没有 UFS host 驱动可开一个配置即复用。

## 恢复及验收状态

UFS 改动前完整源码 checkpoint：`out/checkpoints/a733-pre-ufs-20261020/`，
864 文件、13932575B，包含 Git 可达历史/dirty patch；6 项外部参考记录 SHA 而不复制镜像。
恢复工具只允许新目录，不 reset 原目录。它不是异盘备份；镜像/模型/SD `/data` 必须另保留。
v162 实机通过范围及 v163 未测状态不变。

已生成独立 `../openvela-a733-cubie-a7z-sd-v164-ufs-readonly-candidate.img`（2GiB），
SHA-256 `34ba7129e04f713981edd81760e71edf2feff1d51d1640fe1ec43743f7493d93`。
最终Image/ELF、完整构建、SDK恢复和提取内核验证通过，不等于启动或存储可用。
暂不要求用户烧录；完整烧录会覆盖SD卡 `/data`，必须另备份。原始v162/v163文件不覆盖。

本轮状态：只读传输源码/主机模拟/ARM64对象已验证；完整构建、封存记录见
`../evidence/UFS_V164_20261020.md`。**cold-start、真实容量/读写/高速/长稳均未通过，
不得把本版本作为 UFS 全功能可用版本。**
