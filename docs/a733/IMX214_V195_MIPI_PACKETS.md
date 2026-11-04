# IMX214 v195：MCSIB 真实包接收候选

v194 已通过一次真实 ID、153 次模式写入和 14 项回读，保留原镜像/原始日志。
本版在同一控制会话中接入固定 J5/MIPI1 的 Combo PHY 与协议端口，尝试短时开流，
收集真实包状态，然后停流/复位/恢复。**这是包级实验，不是 RAW 取帧、画质或 YOLO 验收。**
没有 DDR 目标、parser capture、DMA、IOMMU 或 ISP 写入，不伪造 `/dev/video0`。

## 行为与边界

- 正常启动只注册接口；没有配套同次 U-Boot 交接时不访问硬件。
- `prepare` 后三选一：旧 ID、旧 standby 模式、新 `link-experiment`；一次启动只允许一轮。
- 新命令先验证外置固定 SHA256 模式、真实 ID 和 14 项回读，再配置 MCSIB 接收端。
  原 standby 试验写接口继续拒绝 STREAM_ON；独立包源入口在开流前重新检查实际 RX 寄存器。
- 接收模块只使用已经配置的 PLL_REF/PERI0：本候选要求观察到的工厂配置
  `f8675f00/ff126310`，选择 CSI parent2、M=2，名义 240MHz；不是物理测频。
  不同根配置返回 ERANGE，打印原始值，不调共享 PLL，不修改 MBUS/ISP/UFS。
- 全局 CSI 时钟原先必须关闭、复位未释放。模块/其他 PHY/port/子模块不得已有活动用户。
  新增代码使用既有控制 mutex；未来 CSI 客户端必须进入同一所有权协议。
- A733 厂商 `VIN_GRP_ID_MIPI` 采用 `mipi->id/2`，MIPI1 使用 Combo gate0（bit8），
  不是直接使用 bit9。固定 MIPI1 PHY=`05810200`、port=`05811400`、lane IDs=4,5,6,7。
- 设备树同时要求 PK10–PK19=`mcsib`（mux2）、10mA（驱动编码0）。按 HW type4/type10
  的初始 bank 偏移 0x80 和步长 0x80 定位 PK；只改这十个引脚的 mux/drive 字段，
  检查其原先为输入/禁用，保留邻近 PK 引脚，和接收配置一起核对及逆序恢复。
- PHY 软件校准/settle 使用本地厂商默认参考，非示波器验证。串行相机 mode 的已过板结果
  不替代 DPHY 数据接收证据。电源保留/24MHz MCLK 仍是明确假设，不创建“严格测量资格”。
- 清除并确认 W1C 状态为零后才允许传感器 ON。记录 500ms 内新的 RAW10/VC0 包头、
  FS/FE 状态与所有错误。成功要求 RAW10 包头和 FS/FE 均至少一次、无 CRC/ECC/同步错误。
  这些是锁存状态观察次数，不是帧数/FPS；协议状态也不能证明完整像素内容。
- 接收端 1.5s 总预算和轮询上限；每次 MMIO 回调 10ms、每次传感器事务有界。
  即使没有新包或时钟计时异常也返回错误，不无限循环。
- 部分 ON 失败仍尝试一次有界 OFF/回读；TWI 已隔离则不再发总线命令，由外层拉低复位。
  停流结果、接收错误、接收清理错误和原控制清理错误分开报告；第一错误保留。
  停接收端并逆序恢复已触碰字段；清理失败保留所有权，必须重启，不重复尝试。
- 只读物理 MMIO 的总线异常不能靠软件 timeout 化解；依赖现有 AArch64 故障输出及
  保留的 v194 恢复镜像。此候选仍需要真实板测，不称硬件成功。

## 实机测试

备份 SD 私有文件再烧录独立 v195 镜像；FPC/J5 断电连接，不热插拔，不写 UFS。
进入可信 U-Boot 后：

```text
sunxi_card0_probe
mmc dev 0
mmc rescan
mmc part
ext4load mmc 0:3 0x4fc00000 /boot/openvela/a733/camera-v195.scr && source 0x4fc00000
```

NSH 中只执行：

```text
camera control prepare
camera control link-experiment
camera control status
```

不要先运行 `id-experiment` 或 `mode-experiment`，它们会用完本次会话并 park。
期望模式回读 14/14；`raw-headers`、`fs-observations`、`fe-observations` 大于零；
`errors=0`、source ON/OFF/cleanup=0、receiver owned=0、control owned=0。
无相机画面是预期。失败也发完整输出，勿循环重试或手动改寄存器。
如 factory PLL 配置不支持，先看报告，不让用户猜数值写 PLL。

## 来源记录

只参考本地厂商行为/寄存器事实；不把 GPL Linux 函数/头文件直接编入 Apache overlay。
本候选沿用外置 GPL 模式文件及对应完整源文件/许可证，未作整套固件公开发行许可结论。
本地 `../bsp-product-aiot-stable` 文件 SHA256：

| 文件 | SHA256 |
| --- | --- |
| drivers/vin/vin-mipi/combo_csi/combo_csi_reg.c | `16cf3c5e09a4613ebbebd6c5efc0182dbf54ff3b37dfbb88e42d629bd45ca07b` |
| drivers/vin/vin-mipi/combo_csi/combo_csi_reg_i.h | `33cd56ebac15fcb1b9457b221d983f004576a0ebf0766224f4b256dbafdf282d` |
| drivers/vin/vin.c | `83f99b35cea93115d393e9bd132df06d31b9eb09d4f17671ef5200a2f5e21daf` |
| drivers/clk/sunxi-ng/ccu-sun60iw2.c | `d3c2546e376caf878975ae799005df4e5b1d64767e128bfc2c06e21b748c5bbf` |
| configs/linux-6.1/sun60iw2p1.dtsi | `fd4de05567e7dc760e11b42942d90b42962e4d5f2d0a6910d8622ede8b8c711d` |
| drivers/pinctrl/pinctrl-sun60iw2.c | `ab05ddccecd152edf3fafd238ade22299aebe3448646f02f0551edef0c6e1ec7` |
| drivers/pinctrl/pinctrl-sunxi.c | `33071ea9c8121ad75e5810e38dcdb2ef4955f92f0fead4e1a5089e84cb25e134` |
| drivers/pinctrl/pinctrl-sunxi.h | `64dce7ca6fa48e692ff8c318f4c5d23fd4e6b7515ddc8a83fe510f84e9efa528` |

## 构建与检查

仍使用独立摄像头配置/可变 SDK v192 cache，所有新输出目录必须不存在。
脚本仅临时部署项目 overlay，完成或失败都恢复 SDK 并核对原文件。

```bash
python3 tools/check-a733-camera-v195.py \
  --build ../../quickly-openvela/cmake_out/cubie-a7z_nsh_v192_camera \
  --output out/camera-check-v195-new
bash tools/build-a733-camera-v195.sh out/camera-build-v195-new out/ort-static-v185-r2
python3 tools/build-a733-camera-handoff-v192.py \
  ../../quickly-openvela/cmake_out/cubie-a7z_nsh_v192_camera \
  out/camera-handoff-v195-new --release 195 --mode-directory out/imx214-mode-v187-r1
python3 tools/package-a733-boot-v188.py out/camera-build-v195-new \
  ../../quickly-openvela/cmake_out/cubie-a7z_nsh_v192_camera \
  ../openvela-a733-cubie-a7z-sd-v195-mipi-packets-candidate.img \
  out/camera-package-v195-new --base-version 191 --purpose camera-packets \
  --camera-handoff out/camera-handoff-v195-new
```

首轮主机组合/ARM 对象回归通过；接收核心最大单函数栈 128B。
原模式/ID/像素/脚本/封装回归保留。所有 callback-after-effect 失败均检查错误传播、
逆序恢复或失败后保留所有权，另测无数据、旧状态、CRC、计时器不推进、错误 PLL、
已有用户和活跃配置变化。真实包、FPC、CSI/RAW/YOLO 尚未验收；最终制品另记。

## 后续顺序

先用本版证明 MIPI 有新数据；再接 CSI1 parser 路由与独立可追踪的物理 DMA/IOMMU，
写入受保护帧缓冲、检查尺寸/内容/保护区与停机后缓存，导出 RAW 后才做 RGB/YOLO/SPI。
不以通用内存指针或 NPU 私有页表冒充 CSI 的 DMA 地址。不改 UFS 引导区。

## 中间构建记录

`out/camera-build-v195-r1` 在 CMake 生成时报新接收源文件未复制到 SDK，退出 1；
尚未链接/打包。285 项 SDK 恢复逐字节通过。不是板测失败，不删除日志或称它成功。
修正显式暂存/备份/恢复文件清单，新增对全部 A733 CMake 源文件的静态清单检查，
并注入删除接收 `.c`/`.h` 两种错误确认会拒绝。后续构建与最终报告使用新目录。

`out/camera-check-v195-r3` 在 CMake 重新生成期间读到临时编译数据库，找不到既有
UFS 编译条目，提前退出；没有生成合格 summary，不用于最终证据。
对象检查须等 CMake 编译数据库完整生成后再运行；完整构建后再做制品关联，
不放宽目标编译命令来源。
后续源码审查补齐 PK10–PK19 pinctrl，r2 构建只作为修正暂存清单的中间构建，
即使链接成功也不发布为最终候选；最终必须重新构建并重新关联包含 pinctrl 的源码。

修正后的组合测试通过，接收核心 2247 用例包含全部 2237 次 callback-after-effect
故障，另加 PK 引脚冲突拒绝/邻接字段保留/恢复。最终目标对象与镜像仍须另行核验。

最终 host-r4 在 CMake 编译数据库生成完成后运行，原有回归、40 项源文件清单检查、
新包路径组合、2247 接收用例、v195 启动脚本 23 路径均通过。
目标最大单函数栈为接收核心 144B、控制接口 608B、冷控制 288B、模式 2240B、CLI 1840B。
最终完整链接与镜像还要用 source/object/ELF/脚本摘要独立关联，不以对象检查替代构建。
本轮没有测试强杀/异步终止控制任务；短时实验期间不要杀任务或断电，
意外终止后按所有权未知处理并重启，不宣称此候选已具备产品级异常恢复/持续运行验收。

## 最终候选（2026-10-28）

固件源码 `43b0d10eafc4c0fd34dca293af12f7f9b5a17f75`；本地发布标签
`a733-v195-mipi-packets-candidate-20261028`。完整构建 `out/camera-build-v195-r3`
退出 0，287 项 SDK 恢复逐字节匹配。24 构造函数/内存布局检查通过；
最终 host-r5、目标对象和源码与最终 ELF/脚本/镜像关联通过，证据见
`../evidence/camera-v195-final/summary.json`，这不是实机验收。

- 仓库上一层镜像 `openvela-a733-cubie-a7z-sd-v195-mipi-packets-candidate.img`，
  2147483648B，SHA256=`3bf513770bb4a42d7aa12bdc4f97a31f7a43426fd31d19df9b96ebce02571ff3`。
  同目录保留 `.img.sha256`，Windows 整镜像复算一致。
- 内核 7045664B，SHA256=`5719f1789699e4abbabb9f8c011e83327f2b2c64b381929aece458c00c8fbaf1`，
  CRC32=`21bc4102`。ELF 9590576B，SHA256=`893a03ef66eff984b7ee0e9cde0f16dfc0cb645442121f327fa64440d823858c`。
- 配置 103482B，SHA256=`9f7d205b3512946ed2965b2627f17132e6aaac7012a65ee726dbf393803ed315`。
  运行时 footprint=92045312B，未越过保留/堆栈边界。
- `camera-v195.scr` 1357B，
  SHA256=`e23e9b8555d6175019e9c138afd67f164656fe2dd9649000fdd9e5e7be4e0dfd`。
- `out/v195-release-20261028` 封存 ELF/bin/map/config、cmd/scr/manifest、外置模式、
  对应 GPL 源码和许可。后续 SDK cache 变化不影响这份封存。
- 仍使用固定 v191 磁盘基线。GPT、ext4、嵌入内核和五项摄像头文件逐字节核对通过，
  root3 以外区间摘要完全匹配原基线，模型 FAT 不变；没有写物理 SD/UFS。

首轮 `out/camera-package-v195-r1` 已生成镜像并完成根文件回读，但在分段摘要读取时
WSL 返回 `OSError: [Errno 12] Cannot allocate memory`，未形成成功 summary。
保留该日志，不把它称为打包成功。将 Python 哈希读取块改为 64KiB，新增显式
`--verify-existing-image`：必须使用新的证据目录、只读现有候选、不重打包/覆盖。
核验工具重新对最终 ELF 生成脚本，重新从镜像提取内核/五项资产、比较全部摘要/分区，
成功证据为 `out/camera-package-v195-r2`。host-r5 把补强工具纳入来源摘要。

继续核验已有候选时可复用上述打包命令，改用一个新的 evidence 目录并加
`--verify-existing-image`；该模式仍要求既有基线摘要、构建/配置/ELF、所有资产及
镜像分区检查通过，不用于跳过失败项。不存在的镜像或已有证据目录会拒绝。

最终 checkpoint/bundle 为 `out/checkpoints/a733-v195-mipi-packets-candidate-20261028-final`，
此前不带 final 的工作树/暂存区归档保留，最终归档另建，包含发布提交与标签。
恢复校验在新目录 `out/restore-check-v195-final-r1`，不重置原仓库。
镜像/封存为外部摘要引用，须一并保存。旧 v194 镜像与历史文档、失败记录不删除，
UFS 引导清理仍暂停，语音联合板测状态不变。
