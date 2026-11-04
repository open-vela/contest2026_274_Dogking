# IMX214 v194：整套 1080p 模式写入与回读实验

v193 已真实读取 0214 并成功恢复控制，见
[原始板测](../evidence/camera-v193-board-20261028/README.md)。旧候选镜像和独立封存保持不改。
本版将 **ID、整套模式配置、14 项关键回读、standby 确认和复位恢复** 合并成一个有界命令，
不再逐寄存器安排板测。**它仍不是 CSI 出图版；不写 STREAM_ON、不启动 DMA。**

## 本版实现

- `camera control mode-experiment` 只在本次有效 handoff 的 prepare 会话内运行。
  与 `id-experiment` 二选一，每次启动最多一次；不能先运行 ID 自动 park 后再接模式实验。
- 外部 A7CM 固定 660B/153 操作，SHA256
  `a7b2102ba05c51de1a8a7602152505f4feedbeb3ec9facdda70710f9639bb2af`。
  先复制到独占缓冲并用 SDK 实际 SHA256 验证，再释放复位/访问传感器。
  配置损坏、缺失或元数据错误不发起模式传输，已准备的控制可显式 park。
- 配套 U-Boot 脚本从根分区把配置有界加载到实际 ELF 指定的 `.data.camera_mode`，
  最大读取 660B，核对大小后提交另一份 704B 的一次性记录；原电源交接 ABI 不改。
  不使用 CLI 布尔值制造电气测量或“严格资格”，运行时 PMIC 总线仍不访问。
- 同一独占会话重新实读 0214；只允许已有 standby，执行配置并回读 14 项字段与 standby。
  模式执行器复制完整配置后执行，回调不能改写后续操作成开流。
- 独立 trial 读写接口要求本次成功 ID；其自身拒绝 `0x0100=1`、软复位和低地址写入，
  不给旧严格 start API 建立资格或身份 lease。
- 配置全过程 5s、单次事务 30ms（含 STOP 保留），有限轮询防止计时器冻结无限循环。
  正常结束后拉低 RESET/STBY、恢复引脚/时钟；仲裁、STOP/超时或所有权不明时隔离并保留资源。
  第一错误、模式错误、清理错误分别报告，不自动重试。

名义 24MHz/固件保留供电仍是已声明假设；v193 的可通信结果不是示波器/电压表测量。
模式成功仅证明寄存器配置/回读，不证明 MIPI 包、帧、RAW 位对齐、色序或画质。

## 配置来源与许可分离

参考原文件为本地 BSP `drivers/vin/modules/sensor/imx214.c`，SHA256
`e05a71fe4843c81d354ab9fa30bbd809796c21d557a87af54b550ba4e4a23832`。
外部配置保持 GPL-2.0-or-later 来源，不把寄存器表静态编入 Apache 驱动。
镜像根分区携带 `imx214-1080p.a7cm`、完整 `imx214-reference.c` 和
`SOURCE-LICENSE.txt`；生成/打包器锁定数据和对应源码摘要并逐字节回读。
当前是私有开发候选，不作整个组合固件已经完成公开发行许可审核的声明。

## 本版板测命令（需配套 v194 镜像）

断电接好 J5/FPC，禁止热插拔。烧录前备份 SD 私有配置/录音/新增模型，不写 UFS。
冷启动连续发送 `sss` 进入 U-Boot：

```text
sunxi_card0_probe
mmc dev 0
mmc rescan
mmc part
ext4load mmc 0:3 0x4fc00000 /boot/openvela/a733/camera-v194.scr && source 0x4fc00000
```

若设备/分区未识别，先停下，不格式化或写引导区域。脚本与内核必须配套。
进入 NSH 后只需：

```text
camera control prepare
camera control mode-experiment
camera control status
```

期望 `chip-id=0214 match=1`、`writes=153 verified=14/14 error=0`，
最终 `owned=0 configured=0 cleanup=0`。这版屏幕没有摄像头画面是预期，不是取帧已失败。
失败保存完整状态；不要循环 prepare 或通过手填 ready 字节绕过检查。

## 软件验证与复现

组合回归使用真实 SHA2/TWI/模式/控制节点，模拟硬件而非调用 Linux 摄像头：
5971 用例覆盖全部 5298 个回调错误、每个配置字节损坏、元数据、NACK、错误 ID、
模式回读错误、仲裁和 STOP 故障；之前 ID/复位保持、解析器和文件前处理回归保留。
目标对象最大单函数栈：模式 2240B、控制节点 336B、冷控制 272B、CLI 1840B。
这些不代替完整链接、镜像核验或真实模式写入验收，最终构建结果另记。

```bash
python3 tools/check-a733-camera-v194.py \
  --build ../../quickly-openvela/cmake_out/cubie-a7z_nsh_v192_camera \
  --output out/camera-check-v194-new
bash tools/build-a733-camera-v194.sh out/camera-build-v194-new out/ort-static-v185-r2
python3 tools/build-a733-camera-handoff-v192.py \
  ../../quickly-openvela/cmake_out/cubie-a7z_nsh_v192_camera \
  out/camera-handoff-v194-new --release 194 --mode-directory out/imx214-mode-v187-r1
python3 tools/package-a733-boot-v188.py out/camera-build-v194-new \
  ../../quickly-openvela/cmake_out/cubie-a7z_nsh_v192_camera \
  ../openvela-a733-cubie-a7z-sd-v194-camera-mode-candidate.img \
  out/camera-package-v194-new --base-version 191 --purpose camera-mode \
  --camera-handoff out/camera-handoff-v194-new
```

SDK 可变 cache 仍沿用 v192 目录名，但旧镜像/封存文件不修改。
私有模式配置若缺失，从锁定摘要的 BSP 用 `tools/export-a733-imx214-mode.py` 重新导出到新 out 目录，
保留对应源码和许可说明。所有输出目录必须新建。

## CSI 接收端已核对的关键事实与剩余工作

厂商 `sun60iw2_vin_cfg.h` 指定 CSI IOMMU master **1**、ISP **2**；
不能复用 VIP2 NPU 的页表/IOVA，也不能直接把任意 malloc 指针填入 DMA 地址寄存器。
首帧 1080p RAW10-in-LE16 候选缓冲仍约 4.18MB，须独立分配/映射、缓存维护、保护区检查，
并在释放前证明 DMA 完全停机。

V1.11/J5 对应 MCSIB。厂商板级 `vinc0_csi_sel=1`、`vinc0_mipi_sel=1`；
逻辑 VIN0 **不是**物理 MIPI0/CSI0。本地参考地址：

| 单元 | 参考地址 |
| --- | --- |
| MIPI1 PHY / port | 0x05810200 / 0x05811400 |
| CSI parser1 | 0x05821000 |
| CSI CCU / top | 0x05800000 / 0x05800800 |
| DMA0 | 0x05830000（路由须单独配置） |

CSI 系统钟 parent0 是 `pll-video2-4x`，与 MCLK1 parent0 的 `sys24M` 完全不同；
不得照搬已过板的 MCLK 配置去打开 CSI。PHY 校准、通道路由、独立 CSI IOMMU、
DMA/IRQ/有界停机仍需实现与板测。本版不注册假视频节点、不宣称 RAW/YOLO/SPI 实时显示完成。

## 最终候选制品（2026-10-28）

固件源码提交 `afd8e59e3c4162c7469bfd37e34b7311e35bad10`；
独立发布标签 `a733-v194-camera-mode-candidate-20261028`。
实际使用 `out/camera-build-v194-r1`，外层退出 0、完整 SDK 链接通过，
285 项暂存逐字节恢复；24 构造函数，文件 7041568B、运行时 92041216B，内存布局检查通过。
`out/camera-check-v194-r2` 的源/目标对象/日志摘要与最终 ELF 对齐；
最终证据为 `../evidence/camera-v194-final/summary.json`，不是实机报告。

- 新镜像：仓库上一层 `openvela-a733-cubie-a7z-sd-v194-camera-mode-candidate.img`，
  2147483648B，SHA256=`76c73bd6c79c2b5d9c6f63bb675f27501e935087523be26382913f0edc6f05a9`。
  同目录 `.img.sha256` 可校验。
- 内核 SHA256=`452ec209917cfd5989a8afe40d12b50f457b2d07ba4681a40cd6a587c10bce79`，
  CRC32=`4de89c0c`；ELF SHA256=`62e1183defe642504db845a51bbc9e0a3e94ef965e08c03de9daff478a212e08`。
- 配套 `camera-v194.scr` 为 1357B，
  SHA256=`859b3c694f8e66ec1b415827d61ce04bbf3718d92f10742c357b1152cc7b4f6e`。
- `out/v194-release-20261028` 独立封存 ELF/bin/map/config、cmd/scr/manifest、
  外部模式文件、对应 GPL 源码和许可说明；不依赖后续会覆盖的 SDK build cache。
- v191 基线摘要 `0de22bb8f79f34fc5438b2dec1830701156f855195ffca36125d63083e38b265`；
  只替换镜像内 root3，前后非 root3 区间逐段摘要一致，模型 FAT 不变，GPT/文件系统检查通过。
- v193 已通过镜像 SHA256 仍为 `c590fe77b30f12e47ac90b900aa8c055349a88577d3c68e8f75a5d9a9f21aac3`，
  独立封存内核仍为 `707b84dbd46943e5c0bc0a0692f5ee2e4293ebb71b2f104d9d2b8b76af31ef4f`。

所有文件系统操作只针对本地候选文件，未写物理磁盘。语音 v190 仍等待联合实测；
UFS 残留引导清理继续暂停。本次没有替用户执行刷卡、摄像头开流、格式化或模型迁移。

恢复点使用本地 `out/checkpoints/a733-v194-camera-mode-candidate-20261028`。
可用 `tools/checkpoint-a733-project.py verify <checkpoint> --check-references` 重新检查，
并用 `restore <checkpoint> <新的空路径> --with-git` 恢复，不对现有仓库 reset/checkout。
镜像和 out 封存属于外部摘要引用，需一并保留；source/bundle 不把大镜像塞进 Git 历史。

## 追加：一次真实板测通过（2026-10-28）

[完整串口与通过边界](../evidence/camera-v194-board-20261028/README.md)。
本次实际加载脚本 1357B、Image 7041568B/CRC=`4de89c0c`、模式文件 660B。
prepare=13ms，模式实验=102ms，ID=`0214 match=1`、writes=153、reads=17、
verified=14/14、blob-error=0、mode error=0；最终 state=2、owned=0、
error=cleanup=command-error=0、bus-quarantined=0，后续 status 一致。
发布时的未测报告不改写；本次证据单独增加。
这是模式寄存器控制通过，不扩展为连续/冷启动压力测试、物理测量或 CSI 出图。

通过记录标签为 `a733-v194-mode-board-pass-20261028`，不移动旧候选标签；
本轮不改固件代码或镜像。新增归档使用
`out/checkpoints/a733-v194-mode-board-pass-20261028`，
在 `out/restore-check-v194-board-pass-r1` 新目录验证恢复，原工作区不重置。
