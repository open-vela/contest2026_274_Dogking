# 语音、IMX214 视觉与项目恢复约定

补充v187：传感器模式执行/启停和CSI帧容量规划继续源码推进，仍不改语音v185或
实际DMA/IOMMU布局。合并板测前的缺口与恢复入口见
[v187记录](IMX214_V187_SENSOR_MODE.md)，历史过程记录全部保留。

## 2026-10-27 当前补充（下方为历史阶段记录）

用户要求转向 Radxa 13M 214，语音与摄像头稍后统一板测。v185 本地全语音候选镜像、
恢复点与所有模型资产保持原样；候选不是整机硬件通过。摄像头 v186 增加受限定的
TWI3 控制协议、被动板级诊断、独立 RGB/RAW10/NV12 前处理、框映射和文件工具；
未启动真实 CSI/VIN/ISP/DMA、未更改 NPU 内存、未生成替代语音候选镜像。
详见 [v186 实施与缺口](IMX214_V186_BRINGUP.md)；保留所有历史开发文档。

新增约定（2026-10-20）：用户焊接128GB UFS3.1，将模型及高频数据高速存取纳入开发。
SD启动/原数据保留、不格式化、不自动搬迁；语音和IMX214待办不取消，新旧统一板测。
UFS只读第一阶段及冷启动缺口见[UFS实施说明](UFS_STORAGE.md)，不标记高速存取已通过。

更新：2026-10-20。用户要求：NPU 语音链路推进至适合衔接的阶段后，继续完整适配
Radxa Camera 13M 214，对接本地 NPU YOLO 与 SPI 屏幕；之前和之后的板测暂缓，
由用户之后统一执行。开发过程、旧版和可恢复能力都要保留，允许随时补充改进。

这是后续实施约定，不是“摄像头已适配”的声明；不要求用户现在烧录或逐阶段回传。
软件/主机测试可以继续，涉及未知供电/引脚/DMA 的硬件门禁不能凭源码推测跳过。

## 当前可恢复状态

| 模块 | 可复用内容 | 仍待统一实测/开发 |
| --- | --- | --- |
| I2S 麦克风/扬声器 | ICS43434 与 MAX98357A、应用录播、默认 x5；v162 用户确认录音电流声消失 | 削顶根因、长稳和并发负载 |
| 本地 NPU 语音 | 四组件 native 参考执行、KWS 三模型/固定输入；v163 前端数值对照/导出候选 | WAV 数值差异、带标签 KWS 准确率、完整流式 ASR/端到端语音，不冒充已完成 |
| NPU YOLO | YOLOv8n PCQ 动态 RGB 输入、dog 三框/black 零框 | 摄像头实时输入和并发调度 |
| SPI/LVGL | ST7735、表情/Agent 联动、约 29.56 fps、文件 YOLO 框层及清框有历史板测 | 摄像头画面预览与框/帧对应、持续运行 |
| Camera 13M 214 | 已有 Linux sensor/VIN/CSI/ISP 参考资料 | openvela sensor/CCI、供电时钟、CSI/D-PHY、VIN DMA、ISP、V4L2 均未完成 |

语音相关开发继续保持独立目录/命令与可关闭接口。不以“识别出一个候选词”或
“NPU 返回成功”作为完整 ASR 验收。原本地 LLM、云端 Agent、UART TTS、BLE/ESP32
方案和 USB 输入兼容路线保留，不为摄像头删除或重写。

## 视觉实施顺序

1. **资料与安全契约**：确认真实板级 overlay、连接器、PMIC 电源轨、reset/pwdn、
   CCI 控制器、MCLK、lane 映射及 sensor ID 定义；记录来源文件与哈希。
   不把旧 DTS 的另一颗 sensor 配置或没有校验 ID 的 detect 函数当成功依据。
2. **Sensor/控制面**：独立可选模块，默认不启动；有限 I2C/CCI 超时、严格 ID 比较、
   供电/时钟失败清理与关流。首轮不启动 VIN DMA，也不控制未确认的自动对焦器。
3. **CSI/VIN 单帧**：先用已核对的模式表建立 RAW10 单帧，核对 lane、stride、帧长、
   IRQ/错误位、DMA 地址宽度、缓存维护、前后 guard 与失败退出；禁止无期限等帧。
4. **ISP 与标准采集**：核查 Allwinner ISP 运行依赖、IQ/调校文件的可用性和许可。
   颜色链路未确认时只报告 RAW 阶段；不能把灰度扩展三通道冒充正确 RGB。
   对外走 openvela/NuttX video lower-half/V4L2，目标 `/dev/video0`，不以文本诊断节点替代。
5. **预处理 → YOLO**：正确 Bayer/色彩转换、缩放与 letterbox，生成现有契约的
   640×640 RGB（1228800 字节），保留既有 A7PM/整数检测 ABI；记录 scale/padding。
   模型坐标与摄像头/屏幕坐标必须共用同一变换，不直接拉伸输入后画错框。
6. **SPI 结果展示**：先框层，再增加低分辨率画面缩略预览、类别/置信度和计时。
   唯一任务拥有 LVGL；预览帧和框必须具有同一 sequence/时间戳，零检测清框，过期结果标 stale。
   v131 的 32 字节队列只传检测元数据，不塞入图像或未经所有权管理的裸指针。
7. **并发与产品化**：固定数量 DMA/CPU/显示缓冲，只保留最新帧、允许丢帧不积压。
   NPU 语音与 YOLO 先明确互斥，再做有界调度/优先级；不并行占用单执行器或偷偷重置另一请求。
   出错释放本模块资源，不能破坏 Wi-Fi/Agent/音频或拖死启动。

“完全适配”的最终验收还包括恢复重开、冷启动、持续运行、曝光/颜色、可选 AF、
音视频/NPU 共存、温度和内存。当前文件输入 YOLO 实测约 1.68 秒/帧，不能因为
屏幕约 29.56 fps 就承诺相机识别达到 30 fps。捕获、预览和推理解耦并分别计时。

## 本轮核查到的参考差异

- 官方 [A7Z 13M 214 指南](https://docs.radxa.com/en/cubie/a7z/accessories/camera-13m-214)
  列出 1080p `/dev/video0` 与全分辨率 `/dev/video1` 的参考流程，并提示较低分辨率
  尚未充分验证。因此先从已有 1080p 表及真实配置核对，不假设越低分辨率越容易。
  这是 Linux 参考，不是 openvela 已具备对应节点/ISP。
- 本地 `bsp-product-aiot-stable/drivers/vin/modules/sensor/imx214.c` 的
  `sensor_detect` 读取 0x0000/0x0001 后直接返回成功，没有检查读取失败或 ID 相符。
  该逻辑不能照搬，也不据此臆造新 ID 寄存器偏移。
- 本地 `allwinner-device-device-a733-v1.4.6/configs/cubie_a7z/linux-6.6/board.dts`
  默认 sensor0 是 `ov13850_mipi`，并非 IMX214 overlay 的实际运行状态。
  里面的 CCI=3、MCLK=1、PE8/PE9 与各电源轨仅作待核对线索，不直接启用。
- 旧全志 sensor 的 I2C 常量 0x20 在 probe 中右移为 7-bit 地址；不得不加区分地
  将旧 8-bit 地址、普通 TWI 端口和专用 CCI 接口混用。

## 版本与恢复规则（长期执行）

- **不可覆盖通过版**：v162 音频通过镜像、v163 候选及其 source/capsule/日志均保留。
  不将新版本标记为 passed，也不回写旧 manifest 掩盖历史失败。
- 每个新阶段采用独立名称、源码快照、完整构建目录与镜像；大改前先创建 checkpoint。
  当前 `.git` 历史保留，不强制 commit/push，不 reset/clean/delete 用户工作树。
- checkpoint 保存 Git 已跟踪与未忽略的未跟踪文件，包括全部当前 docs/开发记录、
  dirty/index patch（保留暂存区与工作树差别）、HEAD/refs、删除记录，
  以及所有 refs+HEAD 可达历史的 Git bundle；
  不含原 Git 配置、reflog-only/unreachable 对象，原 `.git` 仍保留，不执行清理/GC。
  Git 忽略的大文件、私密状态和 SD `/data` 不能假定已备份；已跟踪内容仍会进入私有快照。
  外部大文件只登记路径/大小/SHA，旧镜像仍放原位；本地同盘快照不代替异地灾备。
- 默认关闭实验摄像头和自动录音/拍摄；新增调试接口可显式启用，发布模式默认安静，
  不删诊断与错误清理。后续补充需求只形成新版本，不擦除旧过程文档。
- 官方 SDK 继续临时暂存→构建→恢复→逐文件核验；原 Linux 开发文件夹只读。

### 本地 checkpoint 命令

在本仓库根目录执行（Windows 可将 `python3` 换为可用 Python 3.10+）：

```sh
python3 tools/test-project-checkpoint.py
python3 tools/checkpoint-a733-project.py create a733-pre-imx214-NEW --reference ../openvela-a733-cubie-a7z-sd-v162-capture-quiet-candidate.img --reference ../openvela-a733-cubie-a7z-sd-v163-frontend-audit-candidate.img
python3 tools/checkpoint-a733-project.py verify out/checkpoints/a733-pre-imx214-20261020-r2 --check-references
python3 tools/checkpoint-a733-project.py restore out/checkpoints/a733-pre-imx214-20261020-r2 out/recovery-a733-pre-imx214-NEW --with-git
```

创建和恢复目标都必须不存在，重跑换新名字；不覆盖原目录，不执行烧录或服务启动。
默认恢复纯源码树；`--with-git` 从 bundle 初始化全新仓库、分离到保存的 HEAD，
填充原索引与实际脏工作树，不 checkout 旧文件覆盖改动。其他 refs/分支以 bundle 和记录为准，
原仓库/`.git` 均不动。不自动提交/推送。恢复后显式设置 `OPENVELA_WORKSPACE` 指向原 SDK，
选择新 `A733_BUILD_DIR`，不能默认复用原构建缓存或宣称恢复源码就等于板子恢复。
`COMPLETE` 缺失的中断快照不能验收使用。校验是内容完整性验证，不是信任签名、
实时功能通过或恶意归档沙箱；只恢复自己保存并校验过的本地快照。
快照含项目日志与历史材料，保持本地私有，不自动发往 GitHub 或外部服务。
当前推荐的初始检查点是 `a733-pre-imx214-20261020-r2`，不是再次使用同名创建命令。
首版检查点和首次部分恢复目录保留：它们暴露 Windows 261 字符相对路径限制，
修复为文件 IO 使用扩展绝对路径、Git 使用 longpaths；深层目录回归通过后使用 r2。
不要把首版的部分恢复目录当成成功恢复，也不要为了重跑直接覆盖/清空它。

## 统一板测账本

| 编号 | 内容 | 当前状态 |
| --- | --- | --- |
| V01 | v163 六 WAV 的实际 fbank 对照与六 f32 导出 | 待用户统一板测 |
| V02 | 正常距离录音、增益前/后峰值、削顶、电流声与重放 | 待用户统一板测；x5 保持 |
| V03 | 带标签 KWS 正/负例及完整流式 ASR | KWS 待验收；完整 ASR 未完成 |
| C01 | IMX214 供电/时钟/严格 ID | 待实现与板测 |
| C02 | CSI/VIN 单帧/连续帧及错误恢复 | 待实现与板测 |
| C03 | ISP/颜色、V4L2、预处理数值 | 待实现与板测 |
| C04 | 摄像头 → YOLO → SPI 画面/框同步 | 待实现与板测；不能用历史文件输入冒充 |
| C05 | 语音/YOLO 调度、长稳、资源回收与回退 | 待实现与板测 |

每个编号保留软件检查、候选构建和实机结果三列证据，之后统一给出可复制命令。
暂缓板测不阻止独立模块的软件开发，但未知的硬件参数不能用“mock 通过”自动填补。
