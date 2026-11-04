# IMX214 v192：复位保持的控制与时钟测试

2026-10-28 追加：用户已完成一次脚本启动、prepare、24MHz/1MHz 配置切换和 park，
控制面通过。原始日志及有限结论见 [板测记录](../evidence/camera-v192-board-20261028/README.md)。
下方原始发布时“未板测”的记录作为历史保留；不是实测电压/MCLK、ID 或取帧通过。
如果 U-Boot 报 `No partition table`，先 `mmc dev 0`、`mmc rescan`、`mmc part`
确认 GPT，再运行 ext4load；不要因这一错误重新分区或清空 UFS。

本版推进 V1.11/J5 控制面，不是摄像头出图版本。v190 语音候选和 v191 镜像保留；v190 实机验收仍延期。不要把主机测试、PMIC 配置值或时钟寄存器计划当作摄像头硬件验收。

## 已生成的候选镜像（2026-10-28）

已从暂停点增量完成构建，`wrapper-status=0`，285 个 SDK 暂存路径逐字节恢复。
源码仍为 `e499648`；恢复后修正脚本日志引号（`02c03ec`），并把 ELF 解析改为
一次读入内存，避免 WSL 跨盘小读取阻塞（`a0ce2af`），不改固件或交接协议。
生成/打包工具提交记录为 `10985fc2d865e1292319ea929fc74eeeb51804f1`。

- 镜像：`../openvela-a733-cubie-a7z-sd-v192-camera-control-candidate.img`
- 大小：2147483648 字节（2 GiB），相邻 `.img.sha256` 为校验文件。
- 镜像 SHA256：`c359a2d23e3a86f6fdd7d3e5d702bd740a6d8375aadb8329a346daf704234d79`
- 内核：7028576 字节，SHA256：`467037ac97584a72b46dee5f2f36f7b76032f8a8cc2ebd5a64ebbda49b190c86`
- ELF SHA256：`73d85fa12c3df5653e29c6cd07c661da6c6bb97f33064acfcb9a9f4b9eba8e90`
- 配置 SHA256：`9f7d205b3512946ed2965b2627f17132e6aaac7012a65ee726dbf393803ed315`
- 运行区：92024832 字节，`_image_end=0x459c3000`；全局构造函数仍为 24 个。
- 交接记录链接地址 `0x408a9c80`，本次源加载地址 `0x40729c80`；由 ELF 计算，
  **不是可跨版本手填的固定地址**。
- 启动脚本 SHA256：`fff0ef25d43bf41d3f1cf7543fb45759091247263aa116a098007077ee53b0c9`

内核、`.cmd` 与 `.scr` 从镜像根分区回读逐字节一致；两次 ext4 检查、GPT 检查、
根分区以外摘要与 v191 一致。旧 v191 镜像不改，模型 FAT 不改；没有写物理盘/UFS。
最终源码/对象/ELF/Image 一一对应校验通过，256 KiB 初始化线程及启动调用图检查通过。
生成器在打包时从最终 ELF/Image 独立重算，拒绝混用旧脚本。r5 主机/交叉对象结果
见 `docs/evidence/camera-v192-host-r5`；完整构建与镜像证据见
`docs/evidence/camera-v192-final`。这些都不是实机启动、传感器 ID 或图像验收。

ELF/bin/map/config 和两份脚本/manifest 单独保存在 `out/v192-release-20261028`。
本地候选标签：`a733-v192-camera-control-candidate-20261028`。
新的检查点使用相同标签名存于 `out/checkpoints/`，不覆盖昨日未完成的归档。
`a733-v192-source-resumed-20261028` 已完成 1426 文件源码/Git 归档及新目录恢复。
本机归档使用 Windows Python，外部引用路径是 Windows 绝对路径；使用 Windows
Python 执行 `checkpoint-a733-project.py verify ... --check-references` 及新目录恢复。

**这仍是 RESET-HELD 控制测试镜像：没有释放 RESET、读 ID、CSI/ISP/DMA 或摄像头帧。**
先执行下方实机步骤，保存完整串口结果；不要把配置匹配当作取帧通过。

## 历史暂停点：用户需要关机（2026-10-27，已于次日恢复）

本轮源码提交 `e499648`，脚本/打包/最终主机回归提交 `05cbdaf`。
完整构建 `out/camera-build-v192-r1` 在 2663/3019 项后收到用户要求的中断，
日志明确是 `interrupted by user`，wrapper 状态为 2，不是完整构建成功。
`sdk-restored.json` 已确认 **SDK_RESTORED_BYTE_EXACT / 285 paths**。
未生成 v192 最终 ELF/可烧录镜像，不能运行最终打包/发布证据脚本冒充通过。

最终主机回归在 `docs/evidence/camera-v192-host-r3`：82 个核心模拟用例、
真实控制 fops 回归、17 条脚本命令模型路径及 AArch64 对象检查通过；
另有 43 条配置拒绝路径通过。v191 检查点 1373 文件与外部引用已复核。
这些不是硬件或 U-Boot 脚本实机验收。所有旧版文档和镜像保留。

关机前停止了本次 Ninja，退出钩子正常恢复 SDK；没有杀死其他任务、删除构建目录
或修改开发板、SD/UFS。`cmake_out/cubie-a7z_nsh_v192_camera` 的已有对象留存。
下次从 WSL 的本仓库根目录继续（新的报告目录，原 r1 日志不覆盖）：

```bash
bash tools/build-a733-camera-v192.sh \
  out/camera-build-v192-r2 out/ort-static-v185-r2 --incremental
```

增量构建仍需重新暂存并最终恢复 SDK；完成后才生成交接脚本、新镜像、校验、
独立封存与最终发布标签。如中间构建目录丢失，应指定新的版本构建目录重新构建，
不要删除封存的 v190/v191 产物。大制品和编译对象不写进源码 Git。

## 已获得的真实板端证据

用户在 v191 启动到 NSH，`camera selftest` 通过（纯 CPU 合成图像）；`camera board` 完成 12 次只读寄存器采集。TWI3 gate/reset 和 MCLK1 初始为 0，PE3/4/5/8/9 为 mux15（io_disabled），没有传感器 ID 或图像帧证据。

用户在 2026-04-29 构建的 U-Boot 2018.07（17-boot-aw2501-g4c7078cfdf7-dirty）执行只读命令，AXP8191 寄存器为：

| 寄存器 | 值 | 有限结论 |
|---|---|---|
| 0x0e / 0x0f | 03 / 00 | ID / 版本匹配此驱动参考 |
| 0x10 / 0x12 | ff / 17 | DCDC1 已使能，配置值为 3.3V |
| 0x20 / 0x2a | c1 / 0d | BLDO1 已使能，配置值为 1.8V |

0xff 在 0x10 是合法使能位图，不应一概判作总线失败。这些是 U-Boot 时刻的配置读数，不是电压表测量，也不是 NuttX 启动后的再次读取。模块 5V、MCLK 实测、传感器 ID、CSI 接收均未验证。

U-Boot 现场还确认：仅有 Bus 0 `sunxi_r_i2c0`，PMIC 地址 0x36 来自对应本地 U-Boot 配置；支持 `i2c read` 到 RAM。现场环境为 `kernel_addr_r=0x40080000`、`fdt_addr_r=0x4fa00000`、`scriptaddr=0x4fc00000`。内核链接地址 0x40200000，不能把链接地址直接用作加载前补丁地址。

## 为什么采用同一次启动的 RAM 交接

NuttX 不直接夺取 SCP 的 R_TWI0 电源总线，不绑定未经本板验证、没有合格超时保证的 PMIC SMC。六个字节由 U-Boot **这一次启动重新读取**，写入当前 Image 文件覆盖的 `.data` 记录。脚本通过 ELF 符号计算源加载地址，先核对完整 Image 长度和 CRC，再读取六个固定寄存器，最后设置 ready 标记。

没有 `saveenv`、PMIC 电压写入、磁盘写入、扫描或任意地址参数。普通启动中记录未就绪，`prepare` 返回 `ENODATA`，硬件访问为零；记录仅可消费一次，重新实验必须重新加载并启动 Image。CRC 是误操作检查，不是密码学认证或安全启动。

**明确假设：U-Boot 到 NuttX 期间固件保留了这些供电设置。** 本层会报告 `firmware-retention-ASSUMED` 和电压未测量，不会伪造 PMIC 再采样、生成旧的传感器传输资格 token 或解锁 ID 读取。后续仍需实测 PE 电源域与 MCLK、确认模块供电。

## 控制范围与失败行为

`prepare` 在检查供电记录、初始资源未被占用之后，取得 TWI3/MCLK1/整个 PE bank 的互斥所有权；先将 PE8/PE9 数据锁存为低再设为 GPIO 输出，配置 PE3/4 为 TWI3、PE5 为 MCLK1。TWI3 使用固定 100kHz，仅作软复位/空闲配置，不访问传感器 DATA/START；MCLK1 为 sys24M/1 名义 24MHz。

RESET/STBY 始终为低，禁止释放复位；不访问 CSI/DPHY/ISP/DMA。时钟校准命令只在固定 1MHz 与名义 24MHz 两档之间切换，仍保持复位。所有普通等待有时间和次数双界限；失败只清理一次。清理失败则保留所有权和故障状态，不自动重试。驱动不能硬性抢占一个已经陷入硬件异常的 MMIO 指令，此版不声称能在任意 SoC 总线故障下保证返回。

后续引入任何 PE GPIO 用户前，必须加入相同 bank 所有权/读改写仲裁，不得各自无锁 RMW。现有独立只读 `camera board` 可继续使用，不会使能控制测试。

## 实机步骤（使用上述已生成的配套镜像）

本版交付的是完整 SD 镜像，不是单个内核补丁。自行烧录前先备份当前 SD 的
`/data`、私有配置、录音及新增模型，核对目标确实是 SD 卡；不要烧写 UFS。
本地打包/检查工具不会自动写入任何物理介质，也不迁移现有私有配置。
摄像头与 FPC 先在断电时接好，使用 V1.11 的 J5，不要热插拔或误接 J3 PCIe。
测量时先共地并确认仪器支持该信号电平，不要向 PE 信号脚注入 5V。

正常启动先运行：

```text
camera selftest
camera board
camera control status
```

共享 CPU 自测和只读板级报告仍保留 v191 组件标记，不等于刷错镜像；
新版独有的 `camera control status` 首行应为 `camera-control-v192`。

主动测试须冷启动，在 autoboot 前持续发送 `sss`（该厂商源码检查连续三个 s/S；空格不是有效中断键）。在 `=>` 下只运行：

```text
ext4load mmc 0:3 0x4fc00000 /boot/openvela/a733/camera-v192.scr
source 0x4fc00000
```

仅使用镜像内与本版 ELF/Image 匹配的脚本，不挪用旧 Image 或旧 RAM 地址。脚本任何错误都停在 U-Boot，不执行 `booti`；不要通过手工填写 ready/PMIC 字节绕过检查。若现场加载地址或唯一 I2C bus 与上述不同，停止并重新核对，不套用此脚本。

进入 NSH 后依次执行，每步保存完整输出：

`prepare` 之前的 `status` 显示尚未消费的本地快照，因此 ready/bytes 为零、
consumed=0 是预期现象，不代表脚本交接一定失败。首次 `prepare` 才消费并检查
记录；之后可见六个字节及结果。没有有效交接时必须失败且不进行 MMIO。

```text
camera control status
camera control prepare
camera control status
camera control calibrate
camera control status
camera control normal-clock
camera control status
camera control park
camera control status
camera board
```

用逻辑分析仪量 PE5/MCLK：`calibrate` 应是名义 1MHz，24MHz 采样率可作初筛；恢复后名义 24MHz 不能用 24MHz 采样率准确测量，需更高速设备。确认 PE9/RESET 保持低。摄像头暂时不应出图或读 ID。任何步骤失败后先保存状态，不循环 prepare；故障清理失败要断电恢复，不能盲目反复操作。

## 可复现验证与构建

```bash
python3 tools/check-a733-camera-v192.py \
  --build ../../quickly-openvela/cmake_out/cubie-a7z_nsh_v191_camera \
  --output out/camera-check-v192-new
bash tools/build-a733-camera-v192.sh out/camera-build-v192-new out/ort-static-v185-r2
python3 tools/build-a733-camera-handoff-v192.py \
  ../../quickly-openvela/cmake_out/cubie-a7z_nsh_v192_camera \
  out/camera-handoff-v192-new
```

生成交接脚本后，打包至新的文件（不烧写物理介质）：

```bash
python3 tools/package-a733-boot-v188.py \
  out/camera-build-v192-new \
  ../../quickly-openvela/cmake_out/cubie-a7z_nsh_v192_camera \
  ../openvela-a733-cubie-a7z-sd-v192-camera-control-candidate.img \
  out/camera-package-v192-new \
  --base-version 191 --purpose camera-control \
  --camera-handoff out/camera-handoff-v192-new
```

打包器重新从最终 ELF/Image 生成脚本并逐字节比较，而不是只相信外部 manifest；
回读 Image 和两份脚本，检查 ext4、GPT 及根分区以外的摘要，v191 源镜像不改。

每个输出目录必须全新，旧镜像、构建与 Git 历史不覆盖。新控制功能由 `CONFIG_A733_CAMERA_CONTROL_TEST` 独立 opt-in，SDK 默认关闭；节点注册不作硬件初始化。完整链接、打包、SDK 逐字节恢复和镜像校验结果在本版完成后另记，不预先标成通过。

## 后续仍需完成

实测供电/时钟并确认控制面 → 合格的复位释放/ID 传输提供者 → IMX214 模式配置 → DPHY/CSI 接收与缓存一致性 → 一帧 RAW 证据 → ISP/软件处理 → NPU YOLO → SPI 展示。目前不得把 reset-held 测试层改名为视频驱动或注册假的 `/dev/video0`。
