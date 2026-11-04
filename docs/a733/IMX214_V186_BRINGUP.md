# Radxa 13M 214 / A7Z V1.11：v186 控制面与图像前处理

更新：2026-10-27。用户本轮要求转向摄像头，语音和摄像头稍后统一板测。
本版是**源码阶段，不是已完成摄像头驱动或可实拍镜像**；没有连接板卡、改 SD/UFS、
执行传感器探测、启动 DMA 或运行 YOLO，也没有生成替代 v185 的镜像。

## 恢复与隔离

- 开始前 HEAD 为 `c982d2a`，工作区干净；新建不可覆盖恢复点
  `out/checkpoints/a733-v186-camera-start-20261027`，含 1215 个文件、完整可达 Git
  历史和 v185 镜像引用。历史过程文档不删除。
- v185 语音候选镜像、模型资产、NPU 内存布局、ORT、LLM、I2S、UART4、UFS 和
  ST7735/LVGL 运行代码均不改。语音候选镜像 SHA256 保持：
  `fbcd67f55429c83550edd7c983b63f5862e7eccc8840de898b60aa6c62afdfd0`。
- 新 `A733_IMX214_CHECKPOINT`、`SYSTEM_A733CAMERA` 均默认关闭；显式构建开关
  `A733_CAMERA_EXPERIMENTAL=1` 仅包含被动诊断和文件前处理，**不是开启拍摄**。
- 标准 SDK 不在本轮被修改。ARM 检查在 `out/` 写对象，采用 v185 真实编译数据库；
  构建脚本新增文件也纳入原有备份/恢复清单。未执行新的完整固件链接/烧录。

## 已核对的板型与时钟事实

用户确认 V1.11，照片与原理图相符。单摄 **J5** 是 31 芯、0.3mm 接口，
MCSIB 四数据通道；**J3 是 PCIe，不能插摄像头**。TWI3 使用 PE3/PE4 mux2，
MCLK1 使用 PE5 mux2；PE8 为 STBY、PE9 为 RESET。这不是新版 CAM2 接线。

Radxa 模块产品简报标注：28/29 为 VCC3.3V，30/31 为 VCC5V；20 为 MCLK，
24/25 为 SCL/SDA，27 为 RESET；23 为 PWDN/NC。这不等于裸 IMX214 的内部
IO/模拟/数字电源轨，不能照搬另一 sensor 的 PMIC 电压和上电顺序。
排线应使用官方 A7Z 指定的 31pin/0.3mm 异面线，断电操作；当前没有模块连接实拍验收。

进一步核实的 sun60iw2 事实：

| 项目 | 原始来源与限定 |
| --- | --- |
| TWI3 寄存器窗口 | BSP `configs/linux-6.1/sun60iw2p1.dtsi`：`0x02513000/0x400` |
| APB1 | BSP `ccu-sun60iw2.c`：CCU + `0x0518`，不是 `0x0520` |
| TWI3 门控/复位 | CCU + `0x0e8c` |
| CSI master1 | CCU + `0x1804`；parent[26:24]、M[4:0]、N[12:8]、gate31 |
| master1 分频 | `ccu_mp.c` NO_INDEX 模式：M+1、N+1，不是把 N 当 2 的幂 |
| master1 parent0 | **sys24M**；DTS 单独声明 24000000Hz，另有 dcxo26M |
| PE3/4/5 功能 | BSP `pinctrl-sun60iw2.c` 量产引脚表，不使用文件前部 FPGA 表 |

因此不能根据 RTC 的 DCXO 26MHz 选择器推断摄像头 MCLK 也是 26MHz；但
**源码标称 24MHz 和寄存器选择正确仍不等于已测到模块上的 24MHz**。
供电、IO 电平、时钟和独占所有权没有用本轮软件测试代替。

固定本地参考摘要见 `../evidence/camera-v186/final/summary.json`，Linux/BSP 原文件只读。

## 本轮新增实现

### 1. 被动板级诊断

`a733_camera_board.c/.h` 是固定 12 个 CCU/PIO 地址的只读采样与解码；
`a733_camera_diag.c` 注册 `/dev/a733-camera`。注册时不采寄存器，不做任何写操作。
每次打开保存独立快照，处理短读、EOF、零长度读、OOM、NuttX 字符设备 dup/open
约定和关闭释放，避免多个 shell 共用一个可变报告。

诊断既不访问 CSI/ISP/D-PHY/IOMMU，也不访问传感器、PMIC 或未开时钟的 TWI3。
报告明确写 `NOT video capture`、电气条件未知；不会注册假的 `/dev/video0`。
100ms/单读10ms 的主机契约不能拦截真实同步总线异常；物理适配器只读现有映射的
常开 CCU/PIO 页面，仍需要后续板测确认。

### 2. IMX214 TWI3 控制协议核心

`a733_imx214_twi3.c/.h` 衔接原 v176 严格 ID 核心：0x10 是 7-bit 地址，
0x0016 的 BE16 ID 必须为 **0x0214**，不能用旧 Linux detect 的无条件成功。
协议实现包含 16-bit 子地址、重复 START、逐字节 ACK/末字节 NACK 和有界 STOP。

它仅接受**已完成电气限定并独占**的 V1.11/J5 handoff 契约：

- 实测 MCLK=24MHz，模块供电限定通过，独占 sensor/TWI3；缺少任何一项，零 MMIO。
- 只读确认 TWI3 gate/reset、PE3/4/5、PE8/9、master1=sys24M/1、APB1=sys24M/1；
  检查总线空闲和 TWI 分频 <=100kHz。不替调用者切引脚、改钟或重置总线。
- 寄存器写入只限 TWI3 DATA/CTL 的正常 I²C 事务；没有 sensor 值写入、SRST、
  GPIO、PMIC、PLL、CSI 或 ISP 写操作。
- 单次事务受 100ms 总预算、10ms STOP 预留、总轮询次数上限约束。单调时钟不动
  也不会无限轮询。超时/无法确认停止会锁存隔离；不自动复位、重试或清除隔离。
- 仲裁丢失/总线错误不驱动 STOP 去干扰另一个主机，锁存所有权不确定。

**没有把实机 MMIO 回调、板级供电/锁持有者绑定到这套核心，CLI 不提供绕过门禁的
参数。** 这是已实现且用模拟控制器验证的协议核心，不是“已经在板上读到 ID”。
它也没有冷启动上电、模式表、流控制或自动对焦功能。

### 3. 图像前处理与框坐标

`apps/system/a733camera/a733_camera_image.c` 提供无分配 CPU 转换：

- RGB24；明确 601/709、limited/full 的 NV12，不猜颜色矩阵或量程。
- RAW10_PACKED（4 高字节+低位字节）和右对齐 LE16，不能把 MIPI 线格式直接
  当 DMA 内存布局；四种 Bayer 排列均可指定，不只按 sensor 名字猜排列。
- 独立 RGB 三通道 Bayer 双线性去马赛克。**没有 AWB/AE/gamma/IQ 调校，不冒充 ISP**，
  也不是把灰度复制三份。实际颜色和 YOLO 准确率仍需实拍验收。
- stride/长度/维度/重叠检查；输出固定最大 640×640；等比补边默认 114。
- 1920×1080 → 640×360，垂直各补 140；完整保存反变换元数据与帧序号/时间戳。
  框还原裁掉补边，纯补边框返回无目标，序号不一致返回 `-ESTALE`。

既有 32 字节视觉 MQ ABI 不改；元数据结构不能塞进这个队列，指针/图像所有权
也不能靠该队列传递。未来需要独立的有界帧缓冲通道，由同一个 LVGL owner 刷新。

### 4. `camera` 文件工具

新增 NuttX CMake/Kconfig/Make 接入，默认关闭。`camera board` 读取被动节点；
`camera prepare` **只处理用户显式提供的单帧文件**。输入必须普通文件、精确长度，
64MiB 上限；输出为全新目录，已存在即拒绝，不覆盖历史文件。
RGB 文件完整写入、fsync/close 后才通过 rename 发布 `ready.txt`；失败保留 partial，
不当作成功结果。此流程仍需正常同步关机，不能视为突然断电的文件系统事务。
未来真实链路应走内存，不能每帧借此写 SD 再读 SD。

**以下命令不在现有 v185 镜像里，不能现在拿它们验证实拍：**

```text
camera board
camera prepare nv12-709-limited 1920 1080 1920 /data/frame.nv12 /data/camera-frame-001
npu yolo /data/camera-frame-001/model-rgb.bin
```

示例矩阵不是实际采集值；必须按照协商到的颜色空间选择。只有完整单帧、
`ready.txt` 存在才可以把 RGB 文件交给既有 NPU 命令。它不是实时摄像头输入验收。

## 可复现的软件验证

在本地 WSL 仓库根目录运行（不需板卡，不改 SDK）：

```sh
python3 tools/check-a733-camera-v186.py \
  --build ../../quickly-openvela/cmake_out/cubie-a7z_nsh_v185_general_voice \
  --output out/camera-v186-check-NEW
```

要求输出目录不存在。实际验证为 7 个目标源文件的真实 AArch64 编译和 7 套主机回归：
原 ID 核心、TWI3 协议、板寄存器采样、诊断 FD 生命周期、像素/框几何、文件 CLI、
Linux 采集脚本的 inspect-only 桩测试。主机 C 路径启用 ASan/UBSan。
目标最大单函数栈帧 1840B（CLI），像素核心 224B；不把单函数栈等同于实机总栈验收。

最终证据在 `docs/evidence/camera-v186/final/`；同目录上层保留封存前检查，失败的
中间检查留在 `out/camera-v186-check-r*`。最后补充了外层探测期限错误的隔离传递，
保证迟返回调也不会丢失故障锁存。
本轮修正过一次主机报告长度断言、一次 TWI 分频测试夹具编码，并按真实 NuttX
字符设备接口去掉不适用的 mountpoint dup 回调；没有隐藏失败的检查目录。

## 真实取帧仍缺什么

1. 绑定已核对的板级供电/IO/MCLK/独占提供者，按冷启动时序实施并严格读取 ID；
   未过这个门禁不启动真实 CSI/DMA。可先继续离线模式/寄存器规划，不能把计划当板测。
2. 合法许可和确定版本的 sensor 模式初始化/流停止；不能把 GPL Linux 大段模式表
   改名为 Apache，也不能无条件默认另一摄像头的时序。首目标按官方已验证的
   1080p参考，不能声称任意低分辨率已经验证。
3. CSI/D-PHY/VIN 单帧、IRQ、超时与停流闭环；独立规划摄像头 DMA/IOMMU master、
   地址宽度、stride、缓存一致性、保护区和故障时内存生命周期。
   **不能借用/扩大 NPU 预留区来凑摄像头 DMA**；本版未改变任何内存布局。
4. 标准视频 lower-half/V4L2、真实 RAW→RGB 或 ISP；先核对文件帧长/变化/颜色。
5. 相同序号的画面+框送 LVGL；语音与 YOLO 在共享 NPU 上明确互斥/调度，不能
   重置另一任务，也不能把 fault latch 或旧帧当新结果。

历史文件 YOLO 约 1.68s/次，屏幕约30fps并不等于摄像头识别30fps。
实时显示和语音首音频3–4秒均没有新增实测，不能在这里承诺已经达到。
板测按用户要求稍后和语音一起安排；不要求现在切 Linux/刷卡或逐步回传。

## 官方来源

- [Radxa A7Z Camera 13M 214 使用方法](https://docs.radxa.com/en/cubie/a7z/accessories/camera-13m-214)
- [Radxa Camera 13M 214 产品简报](https://dl.radxa.com/accessories/camera-13m-214/radxa_camera_13m_214_product_brief.pdf)
- [V1.11 原理图](https://dl.radxa.com/cubie/a7z/docs/hw/radxa_cubie_a7z_schematic_v1.11.pdf)
- [上游 IMX214 驱动](https://github.com/torvalds/linux/blob/master/drivers/media/i2c/imx214.c)

Linux 官方预览使用全志 ISP，不能当 openvela 已有 ISP 的证据。
本地 BSP 只读参考的具体 SHA256 已随软件验证证据保留。
