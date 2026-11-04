# A733 摄像头、YOLO 与 LVGL 全链路计划

## 2026-10-28：v198 同输入单帧识别展示候选

[v198 功能与板测命令](a733/IMX214_V198_SAME_INPUT_VISION.md)：在 v197 待测单帧候选上，
准备同一 RAW 的 YOLO 输入、LVGL 模型底图、输入 CRC/序号/整批绑定，并补查 DMA 迟到错误。
主机和 ARM 编译检查通过，硬件待测；不宣称实时摄像头/ISP/V4L2 或新识别准确率已完成。
既有 NPU 后端及模型不变，下方历史计划和已完成/未完成边界保留。

## 2026-10-27：v187 传感器模式与启停源码

在v186基础上新增来源固定的私有1080p配置导出、模式校验/执行/回读、受ID保护的
TWI3写事务、接收端armed门禁、显式停流/空闲确认和CSI RAW10帧容量规划。
9套软件回归与AArch64对象检查见 [v187阶段记录](a733/IMX214_V187_SENSOR_MODE.md)。
仍没有物理提供者绑定、真实CSI/DMA/V4L2取帧或新镜像；语音v185与旧恢复点保持原样。

## 2026-10-27：v186 源码推进，实拍尚未完成

语音 v185 候选保留，摄像头改按用户确认的 V1.11/J5 单口推进。新增受门禁保护的
TWI3 控制协议核心、被动 CCU/PIO 诊断、RAW10/NV12/RGB 前处理、letterbox/帧号框映射
及 `camera` 文件工具，完成主机 sanitizer 和真实 AArch64 对象检查。
没有绑定合格实机 sensor 提供者、启动 CSI/DMA 或生成新实拍镜像；完整缺口、复现命令
与恢复点见 [v186 摄像头阶段](a733/IMX214_V186_BRINGUP.md)。以下历史记录全部保留。

## 2026-10-23：v175封存后尝试v176探测限定

型号已更正为13M214。新增独立编写的严格IMX214只读探测核心、ASan/UBSan故障测试、
真实ARM64编译和Linux一次性对照工具，详见 [v176门禁/缺口](a733/IMX214_V176_QUALIFICATION.md)。
发现单口V1.11原理图与当前CAM2 overlay的TWI/MCLK/复位引脚不同，未确认实际板型与
电源/时钟，因而不启动CSI/VIN/ISP或注册伪视频节点；尚无实时取流。新核心未链接进v175。
先保留v175可刷UFS候选和完整恢复点，再取得硬件实证继续。下方历史记录全部保留。

## 2026-10-20 用户推进与恢复约定

语音基础继续收敛后推进 IMX214 全链路，用户之后统一进行新旧板测；
全部旧代码/镜像/文档和恢复入口必须保留。当前 v163 是语音前端诊断候选，
不是已完成的 ASR，也尚无 IMX214 驱动。后续默认关闭实验摄像头，不改已有音频基线。
最新依赖顺序、1080p 参考模式修正、统一板测账本及不可覆盖 checkpoint 工具见
[语音/视觉与恢复约定](a733/VOICE_CAMERA_RECOVERY_ROADMAP.md)。以下历史记录保留。

## 目标和当前边界

最终主数据流调整为：

```text
Radxa Camera 13M 214（Sony IMX214，MIPI CSI-2）
  -> A733 VIN / CSI / ISP
  -> openvela V4L2 /dev/video0（目标接口）
  -> 帧捕获与 RGB640 预处理
  -> /dev/npu0 + A733_NPUIOC_RUN_A7PM
  -> YOLOv8 整数检测结果
  -> LVGL 预览、检测框和性能信息
  -> ST7735 128x160
```

USB UVC 路线保留为可选兼容输入，但不再是近期主线。原因是 A733 厂商 Linux BSP
已经包含 IMX214 sensor 驱动和 VIN/CSI/ISP 基础设施，而当前 openvela 的外部
USB2/xHCI2 仍未完成 UVC 枚举、PROBE/COMMIT 和等时传输。复用官方已有 MIPI
寄存器表、供电时序及 CSI/VIN 资料，比继续从 USB Host/UVC 底层向上补齐风险更低。

## 已锁定摄像头型号与移植边界

- 模组：Radxa Camera 13M 214；
- Sensor：Sony IMX214，最高 4208×3120、RAW10、24 MHz MCLK、4-lane CSI-2；
- Linux 参考：A733 BSP 的 `drivers/vin/modules/sensor/imx214.c` 和上游
  `drivers/media/i2c/imx214.c`；
- 不能直接把 Linux 驱动编进 openvela：只抽取寄存器表、供电/复位时序、I2C
  地址、CSI lane/format 和曝光增益规则，再接入 openvela/NuttX video lower-half；
- 产品层最终仍只依赖标准 `/dev/video0`/V4L2 风格接口，不直接访问 sensor MMIO。

MIPI 门禁按以下顺序推进，任何一步失败都不启动后续 DMA：

1. 只上电、24 MHz MCLK、reset/pwdn 时序；
2. I2C 读取并严格核对 IMX214 chip ID；
3. 已核对模式表（优先官方验证的 1080p 路线）的 RAW10 与 CSI-2 lane/settle 验证；
4. VIN 单帧 DMA，验证 stride、格式和 cache coherency；
5. ISP/颜色转换和固定缓冲池；
6. 注册 `/dev/video0`，再接现有 RGB640 → NPU → LVGL 队列。

Linux 板级 DTS 可能仍默认绑定其他 sensor（例如 OV13850），因此不能仅凭节点存在
就宣称 IMX214 已适配；上板前还必须核实模组 I2C 地址、reset/pwdn 极性、电源轨、
lane 顺序和 AF actuator。该方案本轮只保存，不与 I2S 音频改动混合实现。

当前已经具备两端：VIP2 YOLOv8 动态输入/检测结果 ABI，以及 hardware-passed 的
LVGL/ST7735 显示。中间的 IMX214 MIPI 路线目前尚未移植 sensor/CSI/VIN/ISP，也
没有注册 `/dev/video0`；保留的 USB 路线仍只有 `/dev/a733-uvc` 控制器诊断，没有
完成 UVC 枚举、PROBE/COMMIT 和等时传输。因此现阶段不能声称实时摄像头链路已经
可用，也不能把诊断节点的文本当作图像输入。

## 固定的模型契约

```text
package: /data/npu/yolov8n-pcq-v3.a7pm
package bytes: 8017024
package SHA-256: 3c2891ef77ce9a44dd02cd32bdd7db5ec92d14185014e6de8db5803a6cbecce2
input: 640 x 640 x 3, RGB, 1228800 bytes
device: /dev/npu0
ioctl: A733_NPUIOC_RUN_A7PM
result: class_id, score_milli, x1/y1/x2/y2_centi
```

运行时必须验证输入大小和包哈希/版本。检测框坐标为模型 640×640 空间的百分之一
像素整数，UI 层再按 letterbox 比例映射到 128×160；应用层不依赖内核浮点 ABI。

## 分阶段门禁

### 阶段 1：显示性能（v129）

运行 `display fps 10` 和 `display fps 30`，记录真实全屏 flush 帧率、吞吐、`dmesg`
和 `free`。随后用 `display demo 30` 回归七种表情。该数据决定实时预览是每帧全屏
刷新，还是低频预览加高频框层更新。

2026-09-27 已通过：10/30 秒分别为 `29.564/29.563 fps`，持续有效吞吐
`1182 KiB/s`。因此框层可以随推理结果更新；后续摄像头预览仍应采用“只保留最新帧”
策略，禁止积压。

### 阶段 2：文件输入到检测框

先不依赖摄像头，使用已知 1228800 字节 RGB 测试帧调用原子 NPU ABI，并把返回的
检测框通过有界消息交给唯一的 LVGL 所有者。验收要求：dog 输入显示至少一个检测，
black 输入显示零检测；NPU 命令、Agent 和 UI 不能直接并发操作 LVGL。

v131 已把该软件路径完整装入最终镜像。v130 因构建包装器漏暂存 NPU 应用而只带有
显示端，现仅作失败证据保留。共享协议为 32 字节定长消息，队列深度 8；每帧严格为
一条帧头加最多七条检测框，发送和接收均为非阻塞。UI 未启动、队列满或结果晚到时
不得反压 NPU。板端测试命令如下（两个输入必须各为 1228800 字节）：

串口终端先核对文件，再让该终端独占 LVGL：

```sh
ls -l /data/npu/yolov8n-pcq-v3.a7pm
ls -l /data/npu/yolov8n-dog-640x640-rgb.bin
ls -l /data/npu/yolov8n-black-640x640-rgb.bin

display vision 180
```

保持上面的命令运行，在第二个 SSH/串口终端执行：

```sh
npu yolo-ui /data/npu/yolov8n-dog-640x640-rgb.bin
sleep 3
npu yolo-ui /data/npu/yolov8n-black-640x640-rgb.bin
dmesg
free
```

观察完毕后在第一个终端按 `Ctrl+C`，确认返回提示符，再执行 `display demo 15`
回归原有表情。不要让两个 `display` 命令并行争用 LVGL。

预期 dog 产生至少一个框，black 的帧头清除旧框且检测数为零。v131 只绘制状态带与
检测框，不把 640×640 原图传入 128×160 屏幕；这是有意把推理/IPC 门禁与后续摄像头
预览分离。

2026-10-11 已通过实机门禁：dog 输入得到 3 个框并在实体屏显示，black 输入得到
0 个框并清除上一帧；两次 NPU 用时分别为 `1686627 us` 和 `1680635 us`。因此阶段 2
状态为 **hardware-passed**。阶段 3/4 仍未完成，不能把本结果描述为实时摄像头识别。

### 阶段 3：标准摄像头节点（主线改为 IMX214 MIPI）

按上述 IMX214 门禁完成 sensor、CSI/VIN 单帧和连续帧后注册标准 `/dev/video0`，
产品应用只使用 openvela/NuttX V4L2/video API。USB UVC 若以后恢复，则另走描述符、
格式枚举、PROBE/COMMIT、单帧与连续帧门禁；两条输入后端不得改变上层接口。任何
I2C、帧等待、DMA 停止或 USB 控制/等时传输都必须有有限超时。

### 阶段 4：实时全链路

捕获任务使用固定数量缓冲区，预处理执行裁剪/letterbox、颜色转换和 640×640 RGB
输出；推理任务调用同一原子 ABI；UI 任务只消费最新帧/最新检测，过期帧直接丢弃，
避免慢 SPI 或 NPU 反压摄像头。至少记录以下单调时钟指标：

```text
capture_ms preprocess_ms npu_ms postprocess_ms display_ms total_ms
captured dropped inferred displayed timeout errors
```

## 故障与回退策略

- `/dev/video0` 不存在：报告 camera-unavailable，桌宠 UI 和 Agent 继续运行。
- 摄像头超时：释放流缓冲并允许重新打开，禁止无限阻塞。
- NPU 超时/复位：丢弃当前帧，保留最后一次有效结果并标记 stale。
- UI 队列已满：覆盖旧帧，只保留最新结果；不得阻塞 Agent 或 Wi-Fi。
- 温度或内存压力过高：降低摄像头/显示更新率，而不是并行堆积帧。
- 任一新版本失败：烧回 v128 显示基线，确认 SPI/LVGL 未被破坏后再定位上层。

## 后续实现顺序

1. 已用 v129 实机帧率确定 UI 刷新预算；
2. 已通过“RGB 文件 -> YOLO -> LVGL 框层”dog/black 实机门禁；
3. 完成 IMX214 MIPI `/dev/video0` 单帧与连续帧；
4. 接入预处理和最新帧队列；
5. 测量端到端延迟、掉帧、温度和数小时稳定性；
6. 只有阶段 3～5 也通过后才标记 realtime/hardware-passed。
