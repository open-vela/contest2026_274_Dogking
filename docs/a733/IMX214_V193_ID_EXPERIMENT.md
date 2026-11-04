# IMX214 v193：有界、一次性的 ID 识别实验

## 已生成的测试镜像（2026-10-28）

- 镜像：`../openvela-a733-cubie-a7z-sd-v193-camera-id-candidate.img`，2147483648 字节（2 GiB）。
- SHA256：`c590fe77b30f12e47ac90b900aa8c055349a88577d3c68e8f75a5d9a9f21aac3`，相邻 `.img.sha256` 为校验文件。
- 内核 7036768 字节，SHA256：`707b84dbd46943e5c0bc0a0692f5ee2e4293ebb71b2f104d9d2b8b76af31ef4f`。
- ELF 9578408 字节，SHA256：`d7236bd643eaaa560db918a06ed7fca59cf9a480063af18f107a838309fdadf6`。
- 配置 103482 字节，SHA256：`9f7d205b3512946ed2965b2627f17132e6aaac7012a65ee726dbf393803ed315`。
- 脚本 1129 字节，SHA256：`73ca52d5934b5d0824a9fdd4a3e5ff4eaf0e6fdd1eff8418dfeb764476e76923`。
- 内核 CRC32 `6ddb4047`；RAM 记录链接地址 `408abc80`、本次加载地址 `4072bc80`，仅供溯源，不能跨版手填。

最终 BSP 源码对应 `c7ff173`（功能 `795077c`，配置安全说明 `9aacc1d`），打包工具
提交 `1669515`。SDK 子包装器 r3 完整链接/布局检查通过、285 路径恢复一致。
外层 shell 因运行中修改包装器而额外退出 1，不能虚报为 0；
[原始说明](../evidence/camera-v193-build-attempts/r3-wrapper-note.md)保留。
最终包装器三种路由另经 mock 子命令验证，不把它说成第二次完整固件构建。

最终源码/对象/ELF/镜像独立关联检查通过；运行区 92033024 字节，
`_image_end=459c5000`，仍为 24 个全局构造函数，256 KiB 初始化栈和启动调用图检查通过。
实际控制核心/节点最大 AArch64 栈帧 224B，CLI 1840B，均小于 4096B 上限。
86 项组合 ID 故障用例、旧 82 项 reset-held 回归、17+17 条脚本命令模型、
60 条打包配置拒绝路径通过。最终主机证据为 host-r5；完整链接/镜像证据见
`docs/evidence/camera-v193-final`。这些不是新的传感器 ID 或视频实机验收。

镜像内核和两份脚本回读逐字节一致；ext4/GPT 检查、根分区以外摘要与 v191 基线一致，
模型 FAT 不改。旧 v192 镜像及封存内核 SHA256 再次核对一致。未刷写物理 SD/UFS。
封存目录为 `out/v193-release-20261028`；本地标签/检查点名
`a733-v193-camera-id-candidate-20261028`。归档仍使用 Windows Python，外部引用路径为 Windows 绝对路径。

**需要烧录上板测试。烧录前另存 SD 上的私有配置、录音和新增模型，核对目标是 SD，
不要写 UFS。不得把软件检查或镜像生成叫作摄像头识别/出图成功。**

## 前一阶段与本版边界

v192 用户一次板测通过启动、六字节 RAM 交接、RESET 保持的控制准备、
名义 24MHz/1MHz 切换与 park。原始串口和结论见
[v192 板测记录](../evidence/camera-v192-board-20261028/README.md)。
v192 镜像、封存 ELF 和恢复检查点不修改。SDK 的可变增量缓存可重用，不是封存产物。

用户说明 FPC 接口无法直接测电压/波形。不要求带电探测 FPC 触点，也不提供伪造
“已测量”的 CLI 标志。本版改用**明确声明假设的探索性 ID 实验**，不是把名义时钟
强行写成 measured_mclk_hz=24000000，也不是解除旧严格提供层门禁。

本次 U-Boot 重新读取 AXP8191 六个字节，仍假设固件保留供电；PE 电源域、模块
供电和真实 MCLK 未由仪器测量。固定配置来源仍是 V1.11/J5、厂商 DT/CCU 参考。
ID 返回 0214 只能证明当时存在可工作的传感器控制通信，不能量出电压或时钟裕量。

## 操作契约

`camera control id-experiment` 只能接在本次 Image 的成功 `prepare` 后：

1. 已取得整个 PE bank、TWI3、MCLK1 所有权；当前名义时钟须为 24MHz。
2. 回读 APB1、pinmux、低电平 RESET/STBY、MCLK、TWI gate/reset、100kHz 配置及空闲状态。
3. PE8/PE9 拉高并回读，等待 30ms。控制电平/等待事实参考本地厂商 IMX214 PWR_ON
   及 STBY_OFF 时序；不复制 GPL 模式表，不调用其无条件成功的 detect。
4. 只访问七位地址 0x10、16 位子地址 0x0016，重复 START，读两字节、末字节 NACK、STOP。
   大端组合须严格等于 0x0214。不扫描、不写模式、不 STREAM_ON、不访问 CSI/DPHY/ISP/DMA。
5. 成功、ID 不符或正常完成 STOP 的地址 NACK：自动保持复位、停时钟并恢复资源。
6. 总线状态不明、仲裁丢失、超时/STOP 失败：不再 reset/gate/STOP；仅尝试拉低本方控制线，
   保留所有权和隔离状态，必须重启。清理失败独立报告，不虚报已恢复。

每次 Image 启动只能尝试一次；没有自动重试，也没有任意地址/时钟/电压参数。
prepare、原来的时钟切换仍保持复位；只有显式 id-experiment 短暂释放控制线。
成功不设置 `identity_verified`、`board_power_qualified` 或实测 MCLK，不能调用模式/视频 API。

## 测试命令（需本版配套镜像，不能在 v192 上使用新命令）

先断电接好摄像头和 FPC，V1.11 使用 J5，不是 J3 PCIe。禁止热插拔。
串口 autoboot 前连续发送 `sss`，在 U-Boot 提示符执行：

```text
mmc dev 0
mmc rescan
mmc part
ext4ls mmc 0:3 /boot/openvela/a733
ext4load mmc 0:3 0x4fc00000 /boot/openvela/a733/camera-v193.scr && source 0x4fc00000
```

如果 MMC/GPT 或加载失败先停止，不重新分区、不 saveenv、不修改 PMIC。
脚本与 Image 必须配套；每版地址从最终 ELF 算出，禁止照抄旧版 ready 地址。

```text
camera selftest
camera control status
camera control prepare
camera control id-experiment
camera control status
camera board
```

期望首行 `camera-control-v193`，识别结果 `chip-id=0214 match=1`，
最终 `owned=0`、`configured=0`、`cleanup=0`。报告仍明确电气未测量、没有严格资格/视频帧。
不要在 id-experiment 前 park；它会结束 prepare 会话。失败先保留完整日志，别循环测试。

## 软件验证、构建与未完成项

源代码已加入实际控制节点/CLI、冷控制核心和 TWI3 核心；不增加任何 boot-time 硬件访问。
ASan/UBSan 对组合路径逐回调注入故障，包含写入已经生效后返回错误、NACK、错 ID、
冻结计时器、锁冲突、仲裁、STOP 失败和二次操作拒绝。旧 82 项 reset-held 回归保留。
目标对象编译及栈检查与最终链接/镜像验收分开记录，不把模拟结果说成读到真实 ID。

```bash
python3 tools/check-a733-camera-v193.py \
  --build ../../quickly-openvela/cmake_out/cubie-a7z_nsh_v192_camera \
  --output out/camera-check-v193-new
bash tools/build-a733-camera-v193.sh out/camera-build-v193-new out/ort-static-v185-r2
python3 tools/build-a733-camera-handoff-v192.py \
  ../../quickly-openvela/cmake_out/cubie-a7z_nsh_v192_camera \
  out/camera-handoff-v193-new --release 193
python3 tools/package-a733-boot-v188.py out/camera-build-v193-new \
  ../../quickly-openvela/cmake_out/cubie-a7z_nsh_v192_camera \
  ../openvela-a733-cubie-a7z-sd-v193-camera-id-candidate.img \
  out/camera-package-v193-new --base-version 191 --purpose camera-id \
  --camera-handoff out/camera-handoff-v193-new
```

沿用 v192 交接 ABI，但脚本文件名、日志及镜像为 v193，生成器独立校验最终 ELF/Image。
重用 v191 精确摘要的磁盘布局基线，只替换镜像文件里的根分区；不写物理 SD/UFS，
不迁移模型，不覆盖任何旧镜像。所有输出目录必须新建。
图像模式、CSI/DPHY 接收、帧 DMA/缓存、ISP 和实时摄像头 YOLO 仍未完成。

本轮中间构建 r1 在 CMake 生成阶段由 agent 主动中断以补正 Kconfig 的安全说明，
没有完成固件；SDK 285 路径恢复。中断留下 cache/对象但缺 build.ninja，r2 增量因此
退出，SDK 同样恢复。新增显式、限定同一 Ninja/源目录 cache 的重新生成路径，
不删除构建缓存、不恢复旧源码冒充新功能；最终成功构建须另记，失败日志保留。

上述中间状态原样保留；最终 r3 子构建与独立发布检查已完成，见本页开头。

## 追加：一次真实 ID 板测通过（2026-10-28）

用户本次完整日志已另存 `../evidence/camera-v193-board-20261028/serial.txt`，
SHA256=`6e4333505424e62dd6e1dd995ca48411c03b913c539ece95838fcab50aa86e87`。
脚本载入的 Image CRC 为 `6ddb4047`，PMIC 字节为 `03/00/ff/17/c1/0d`；
NSH 的一次 `camera control id-experiment` 返回真实 ID=`0214`、match=1、
elapsed=31ms、error=0、cleanup=0、command-error=0、owned=0、configured=0、
consumed=1、bus-quarantined=0。恢复点标签为 `a733-v193-id-board-pass-20261028`，
指向原 v193 发布提交 `14e96fd987a93c30397bc8c9a06d338dc444db08`，不移动旧候选标签。
此前“待板测”描述为当时历史状态，不删除。通过范围仅是这一次控制/ID实验，
不扩展为电气测量、重复冷启动稳定、模式配置或 CSI/图像成功。
