# v191：V1.11/J5 摄像头资格检查镜像

## 状态与边界

2026-10-27：用户暂无条件测试 v190，保留待测，不标记为通过。
v190 的标签、检查点、不可变 ELF/bin/config 和 SD 镜像均不覆盖：

- 标签：`a733-v190-offline-voice-candidate-20261027`。
- 源码：`59808e18c3749fd0fc2e5a28e86e7d02a3acb61d`。
- 交付记录：`85d3279318e24fd88acf46bccfd56579ed986ae8`。
- 镜像：`../openvela-a733-cubie-a7z-sd-v190-offline-voice-candidate.img`。
- SHA256：`6082caf2fd059d06d1057abad1cf2bb726dbb4ab776f85c8a4f320c0b00f1ccb`。
- 检查点：`out/checkpoints/a733-v190-offline-voice-candidate-20261027`。
- 不可变产物：`out/v190-release-20261027`。不要用后续 SDK 输出覆盖它。

v191 是第一张完整链接、启用摄像头诊断应用的**资格检查候选镜像**，
不是可拍照镜像；不能宣称已实现 IMX214 取帧、ISP、实时 YOLO 或摄像头预览。
v186/v187 的测试以前仅证明对象编译与模拟测试，不曾证明完整镜像和摄像头硬件通过。

现有严格传感器 ID、TWI3、模式和 CSI 帧规划核心保留，但没有真实板级提供层。
当前不能从 Linux 的 OV13850 电源配置或新版 CAM2 overlay 推导本板 IMX214 的电气资格。
本版不通过新增命令行布尔参数绕开 power/measured-MCLK/ownership 门槛。

## 已确认的硬件路线

目标是用户照片及丝印确认的 Cubie A7Z V1.11，单 CSI 插座 J5。
J3 是 PCIe，不是摄像头；不使用新版 CAM2 的 TWI4/PE10/PE11 路线。

- J5：31 pin、0.3 mm；官方要求异面 FPC。断电插拔，检查方向、锁扣和接触面。
- 控制：TWI3 PE3/SCL、PE4/SDA；MCLK1 PE5；STBY PE8；RESET PE9。
- 数据：MCSIB 四通道，候选 CSI1/MIPI1。
- 模块 28/29 脚是 3.3V，30/31 脚是 5V；不能当成裸 IMX214 芯片供电。
- ID 核心只接受 7-bit 地址 0x10 的寄存器 0x0016 返回 0x0214，不能照搬旧 BSP 无条件成功的 detect。

锁定资料和来源哈希见 `IMX214_V176_QUALIFICATION.md`、
`IMX214_V186_BRINGUP.md`、`IMX214_V187_SENSOR_MODE.md`。
官方接口说明：<https://docs.radxa.com/en/cubie/a7z/accessories/camera-13m-214>。

## 本版命令与验收含义

烧写前保留 SD 卡上的模型和配置；烧写整卡会覆盖目标卡。
本地制作镜像不写物理盘、不改 UFS、不删旧版本。

```text
camera selftest
camera board
cat /dev/a733-camera
dmesg
```

`camera selftest` 使用合成像素，不访问文件、设备、传感器或 NPU：
检查 RGB letterbox/几何映射、错序结果拒绝、四种 NV12 色彩定义、
四种 Bayer 定义下的白场转换及短帧拒绝。PASS 仅代表 CPU 预处理软件运行成功，
白场不用于确认真实传感器的 Bayer 顺序；主机另有非对称颜色图案回归。

`camera board`/`cat` 只读取 12 个既有映射的 CCU/PIO 寄存器，
不访问未开时钟的 CSI/ISP/TWI 设备寄存器，不做 PMIC/时钟/引脚/复位写入。
初始化只注册只读节点，不在开机时取样，不自动启动摄像头。

新增 `handoff-config: matched=N/6` 解释六项配置观察；即使 6/6，
仍会明确报告电源、实测 MCLK、传感器 ID、总线及 DMA 所有权尚未证明。
`complete=1`/命令返回成功只表示快照读取成功，不表示摄像头可用。
两次读取可能因固件配置而不同，应保留完整原始输出，不用成功计数替代证据。

已有文件工具继续保留：

```text
camera prepare nv12-601-limited 1920 1080 1920 /data/frame.nv12 /data/camera-frame-001
```

这是精确一帧文件的 CPU 转换，不是摄像头采集命令；输出目录必须不存在。
输入 NV12 的色域、范围及 stride 必须来自真实提供层，不能看文件后缀猜测。
成功后输出 RGB 640×640×3（1228800 字节），可以交给现有 `npu yolo-ui`，
但静态文件识别不算摄像头实时链路验收。

## 可复现构建与打包

WSL，从仓库根目录执行；新建专用 SDK 构建目录，不复用 v190：

```sh
bash tools/build-a733-camera-v191.sh out/camera-build-v191-r1 out/ort-static-v185-r2
python3 tools/package-a733-boot-v188.py out/camera-build-v191-r1 \
  ../../quickly-openvela/cmake_out/cubie-a7z_nsh_v191_camera \
  ../openvela-a733-cubie-a7z-sd-v191-camera-qualification-candidate.img \
  out/camera-image-v191-r1 --base-version 190 --purpose camera-qualification
```

打包器应核验 v190 基础镜像哈希、SDK 全部暂存源文件逐字节恢复、
摄像头及引导配置、实际内核标记、引导调用图、内核回读和非内核分区不变。
保留 v190 的语音应用/资产不等于 v190 已板测；MiniCPM5 大模型仍需用户自备。
不会在镜像内加入来源/许可不完整的 GPL 派生模式文件。

```sh
python3 tools/record-a733-camera-v191.py out/camera-check-v191-r1 \
  out/camera-build-v191-r1 out/v191-release-20261027 \
  out/camera-image-v191-r1 docs/evidence/camera-v191-final --source-commit ffb4781
```

记录器逐项检查源文件、对象、主机日志、SDK 恢复、镜像和 ELF 摘要，
并检查 ARM64 运行区不越过 BL31 边界以及构造函数数量未变。
打包配置另有 23 个拒绝路径测试；不允许用 Python `-O` 跳过打包断言。
早期启动标记仍可能显示 `M0 v188-boot-candidate`，该引导修复没有重命名；
以 `camera-selftest-v191` 和 `A733 IMX214 v191` 判断摄像头应用版本。

## 实际交付记录（尚未板测）

- 固件源码提交：`ffb47817f6efc1623baaf5236d47975dc0cefdb1`。
- 打包工具提交：`00832126622f923c84760b02f7ec7829b2674efd`。
- 本地发布标签：`a733-v191-camera-qualification-candidate-20261027`。
- 镜像：`../openvela-a733-cubie-a7z-sd-v191-camera-qualification-candidate.img`。
- 镜像大小：2147483648 字节，附 `.img.sha256` 校验文件。
- 镜像 SHA256：`0de22bb8f79f34fc5438b2dec1830701156f855195ffca36125d63083e38b265`。
- 内核：7024336 字节；SHA256：`ec6ecbafe14259159d7d74dea54c9de048f393014434faee9d6354f95a286447`。
- ELF SHA256：`e76555b830d47d24a97d5676b746fe88df1e65f0eb4bd6533333ccba4acfa18d`。
- 配置 SHA256：`7254f38b2e12db8c821fd79520586164379d94b31dc14596af9e8138866e0581`。
- 静态运行区 92020736 字节，`_image_end=0x459c2000`，构造函数仍为 24 个。
- 完整构建日志：`out/camera-build-v191-r1/build.log`，SHA256：
  `260326646ad696a147b7151900348c5c685f6d2f2864362ccf8034070a65e597`。

九组主机/模拟回归、AArch64 对象及完整链接、281 个 SDK 暂存路径恢复、
23 个打包拒绝路径、引导调用图/ELF 布局及镜像内核回读/分区摘要检查通过。
证据见 `docs/evidence/camera-v191-host` 与 `docs/evidence/camera-v191-final`。
这些结论**均不是摄像头电气、取帧或整机启动的实机验收**。

内核以外的镜像区域及原模型 FAT 摘要与 v190 相同；未写物理盘、未改 UFS，
未覆盖或删除旧镜像。旧 UFS 引导残留等问题继续保留，不归因于摄像头也未借此修复。
最终 ELF/bin/map/config 独立封存在 `out/v191-release-20261027`。
源码归档、Git history bundle 与外部制品摘要由以下检查点命令保存；恢复只允许新目录。

```sh
python3 tools/checkpoint-a733-project.py verify out/checkpoints/a733-v191-camera-qualification-candidate-20261027 --check-references
python3 tools/checkpoint-a733-project.py restore out/checkpoints/a733-v191-camera-qualification-candidate-20261027 out/restore-check-v191-r1 --with-git
```

## 下一道实机门槛

1. 先证明本镜像正常启动及自测、12 项快照可重复返回。
2. 为 V1.11 建立真实的电源、PE 电平、MCLK 输出/实测与控制引脚极性提供层，
   分清固件保留状态与冷启动，不复用其他传感器的 PMIC 电压。
3. 独占 TWI3 后严格读 ID，再在 standby 执行已校验模式和关键寄存器读回。
4. 证明 CSI 主设备自己的地址转换、缓存维护、DMA 所有权和有限停止；
   不能拿 NPU 的 IOVA 当 CSI 地址，也不能先启动传感器再分配接收缓冲。
5. 先测试一帧 RAW10、帧计数/长度/边界哨兵，再验证 Bayer/颜色与文件对照，
   最后接 YOLO 与同序号画面/框。没有 ISP/AWB/AE 时必须明确其限制。

后续可沿用 `tools/test-linux-imx214.sh` 采集官方 Linux 参考数据。
当前没有板子，以上硬件步骤不能被软件模拟测试替代。

## 后续用户板测补记（2026-10-27）

以上是构建封存时的原始结论，不回写成“当时已板测”。之后用户上传日志确认：
一次启动进入 NSH，`camera selftest` 的 CPU 合成测试通过，`camera board`
完成 12 次只读寄存器采集；PE3/4/5/8/9 均为 mux15，TWI3 BGR 与 MCLK1 为零。
这与该版只做资格检查的范围一致，不是传感器 ID、连续启动稳定性或取帧验收。
随后收集到的 U-Boot PMIC 配置读数与下一步控制测试见
[v192 说明](IMX214_V192_CONTROL_TEST.md)。v191 封存证据与通过边界不改写。
