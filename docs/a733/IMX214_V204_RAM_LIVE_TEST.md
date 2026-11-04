# v204：相机 → YOLO → LVGL/ST7735 长时循环候选

2026-10-29。以已通过双次实拍/色彩/显示的v203-r2为回退基线。
本轮是新的**后台循环协调器候选，不是已通过实机长稳，也不是MP4录像或常开CSI/DMA视频驱动**。
原始v203-r2镜像、完整文档、两个恢复标签及私有人像目录保留；不擦UFS，不改语音待测结论。

## 实现与边界

1. 一条命令启动独立NuttX `task_create` 任务；不是CLI退出就会被终止的分离pthread。
   `live-status`随时查询，`live-stop`只设置停止请求，不强杀、不全局取消/复位NPU。
2. 每帧沿用已验证的prepare→单帧DMA→停流→保护区/数据检查→寄存器与控制恢复。
   只有完整清理成功、无读者时才能继续；硬件失败不重新启用不确定的DMA。
   启用新实验配置时每启动清洁采集预算为32768，单会话最多3600秒；这是边界，不是实测数量。
3. RAW仅复制到固定RAM缓冲；增强输入仍为v203的640×640 RGB、114填充、手动tone/Gamma/180°。
   去掉原来丢弃的线性插值预计算；400组与固定v203实际C实现逐字节对照，像素语义不变。
4. 使用官方tmpfs，专用伪文件系统根挂载点 `/camram-v204`，不能嵌套挂到FAT的/data内部。
   模型启动时读入一次；输入文件分配后复用，不逐帧向SD/UFS导出RAW/RGB；挂载点也不创建在SD上。
   NPU v4 ABI/执行器不改，只在实验配置下额外许可两个精确RAM路径：model.a7pm和input.rgb。
   支持的默认包为 `yolov8n-pcq-v3.a7pm`，并非通用任意模型接口；可选路径只用于该包的其他位置。
5. 一个工作任务串行采集、预处理、推理和LVGL刷新，只有一个在途帧，无异步裸指针队列。
   底图从同一个模型RGB按5:1中心采样，框使用同一个序号/CRC；新预览立即清旧框。
   推理结果须status/hardware=0、完成IRQ、六输出、完整输入字节数/CRC通过后才显示。
6. 预览未推理时显示灰色VIEW；推理完成显示绿色YOLO，框旁为 `类别ID:置信度%`，不是中文物体名称。
   零检测清除全部旧框；结束清框并显示橙色STOP。不是拿上次框套新图。
7. 时间为真实单调时钟；统计各阶段累计耗时和实际提交显示帧率。模型预加载计入总耗时，
   指定seconds从预加载/显示就绪后开始；最后一个有界操作可能使结束稍晚于指定时间。
8. 停止/异常清理模型fd、固定缓冲、LVGL及tmpfs；只删除两个本任务RAM文件，不递归删除。
   清理失败会保留隔离状态，拒绝下一会话，不能把 `active=0` 单独当作清理成功。

## 上板前

新候选镜像名：`../openvela-a733-cubie-a7z-sd-v204-camera-yolo-live-candidate.img`。
**烧写会覆盖SD内容，先备份个人模型、配置、照片；打包器不触碰实卡/UFS，不等于替你备份SD。**
使用新Image配套的自动boot.scr / `camera-v204.scr`，不能混用v203脚本和新Image。
停止其他LVGL UI及其他NPU语音/手动推理任务；本版不声明多模型并行或并发总线稳定。

```text
display raw8
npu info
npu selftest
ls -l /data/npu/yolov8n-pcq-v3.a7pm
free
```

默认模型应为8017024字节，SHA256：
`3c2891ef77ce9a44dd02cd32bdd7db5ec92d14185014e6de8db5803a6cbecce2`，缓存CRC32=`f5f558c5`。
已只读检查v203-r2基础镜像内含该包；新包不修改数据分区。不得把ONNX/NBG直接改名当作A7PM。

## 分阶段测试（一个串口窗口即可，不需要SSH）

首轮优先在串口启动并保留完整日志。若用SSH，测试期间保持会话连接，先合作停止并等清理完成再断开；
不承诺强杀任务、网络断开或突然断电后的进程级自动清理，异常中断后的恢复入口是保留状态并重启。

先10秒预览，不占用NPU、不创建tmpfs：

```text
camera live 10 0
camera live-status
```

命令立即返回只是accepted；等最终日志或查询显示 `active=0 error=0 cleanup=0 frames>0` 再开始下一项。
期间移动摄像头，确认画面更新，不是只看单张静态图。之后启动逐帧识别：

```text
camera live 30 1
camera live-status
```

在镜头前依次放人、杯子/瓶子等COCO物体，再对准空白墙。保存完整串口日志，核对变化的输入CRC、
六个输出CRC、类别/分数/框，以及空白场景是否清框。传输完成不等于识别准确，需人工对照。
若无框，请回传日志，不强行调低阈值或使用虚构框。

确认前两项无错后逐级延长；第二个参数为每N帧推理一次，中间帧仅预览且不复用旧框：

```text
camera live 60 3
camera live-status
```

正常结束再测试10分钟，必要时延长到1小时（不可同时启动）：

```text
camera live 600 3
camera live-status
```

```text
camera live 3600 4
camera live-status
```

任意时刻停止：

```text
camera live-stop
camera live-status
```

收到stop请求不是已停止；等active=0并核对cleanup/error。不要用Ctrl+C/kill强行中断DMA或NPU。
停止只在当前有界操作后生效；若NPU被其他任务占用，等待还可能更长，因此请单独测试此链路。

正常结束后检查资源及重启会话：

```text
free
camera control status
camera live 10 0
```

free仅是辅助指标，不证明所有分配器/静态池无泄漏。请记录每次帧数、各阶段耗时、错误、温度/电源情况，
分别验证预览、推理、正常结束、用户停止、再次启动及冷启动；热稳定性不能由主机测试替代。

串口保存为UTF-8日志后，可离线核对一个已结束会话（默认最后一次，`--run-index 0`选择第一次）：

```text
python3 tools/audit-a733-live-board-v204.py /path/to/serial.txt /path/to/NEW-live-report.json
```

输出文件必须不存在；核对帧顺序、推理间隔、六输出、框数量/范围、零检测、最终清理状态、
最后输入CRC和计数/时间/FPS一致性。预览帧仅首帧及每10帧记录，不能把日志条数当帧数。
日志损坏/缺失会拒绝，不补造数据；通过仅表示输入文本内部一致，不代替实机画面、准确率、温度或二进制身份验收。

## 错误处置

- 缺模型/无存储：启动失败，不进行相机采集。先检查原文件和挂载，不会改用假帧、CPU或云端。
- display owned / EBUSY：先退出已有 `display pet/demo/vision` 再启动；不抢占别人的LVGL实例。
- 相机/NPU/SPI错误：立即停止循环，不自动重试硬件，不抹掉驱动故障；回传live-status和control status。
- cleanup非零：保持现场并重启，不强制卸载未知挂载点。
- 中断任务留下的RAM挂载点可能导致EEXIST：不要强制卸载或删除，先回传列表/状态并重启；
  程序不接管已有目录。伪文件系统节点不跨断电保留。正常umount可能自动移除节点，ENOENT不算清理失败。

## 构建与验证

```text
python3 tools/test-a733-tone-compat-v204.py
python3 tools/test-a733-live-v204.py
python3 tools/test-a733-live-display-v204.py
bash tools/build-a733-camera-v204.sh out/camera-build-v204-NEW out/ort-static-v185-r2
```

新编译缓存为 `../../quickly-openvela/cmake_out/cubie-a7z_nsh_v204_camera_live`，不是覆盖v203缓存。
已有v204缓存时只用显式`--incremental`，构建报告目录每次必须新建。官方覆盖层291项必须逐字节恢复。
完整构建后用 `check-a733-live-v204.py` 验证原摄像头回归、原生配置ARM对象、24构造器和帧池边界。
用 `package-a733-camera-v204.py` 从确切v203-r2普通镜像封装，回读内核/引导脚本和所有未改分区摘要。
所有源代码/配置/原始通过版保留，硬件通过与主机128次模拟帧/100000元数据检查严格区分。

构建过程记录：r1在发现SDK VFS不支持/data下嵌套挂载后正常中断Ninja，包装器exit=2，
291项官方暂存路径已逐字节恢复；没有生成/上板该版镜像。修复使用根挂载点及窄路径许可，
并按官方umount自动移除inode的行为补测，随后从相同缓存增量构建。

仍缺的“长视频”能力：常开传感器/DMA环形缓冲、标准视频接口、编码器/MP4存盘、实测长稳与目标帧率。
本版先验证零逐帧介质导出的长时间循环预览/识别，不宣称这些未完成能力。

## 本次候选封存

- 镜像：`../openvela-a733-cubie-a7z-sd-v204-camera-yolo-live-candidate.img`，2147483648字节。
- 完整SHA256：`f21a9d0bd8aab20264db3e44273ebf372c629f33ee7bba6c49659f0e2e82326d`；同名`.sha256`旁文件可校验。
- 内核：7094888字节，SHA256=`cd6bf4cc8e9ec6d2fee01f92ee2c660f8abadca563c530aca47377f536c22cc9`，U-Boot CRC32=`f1b3cc88`。
- 固件源码：`fe641a49de964208f9092da39f408acb764c22d6`；封装工具：`318784ec0ebef77d1332e5c33613678bd2fa3adf`。
- 构建/核验/封装：`out/camera-build-v204-r2`、`out/camera-check-v204-r1`、`out/camera-package-v204-r1`。
- ELF/内核/map/配置/模式/GPL来源/实际脚本封存：`out/v204-release-20261029`；[81份证据与摘要](../evidence/camera-v204-final/README.md)。
- 24构造器；4 MiB帧池=`0x45903000`，NOBITS/4096对齐；runtime_end=`0x45dd5000`，低于`0x48000000`。
- 构建完整成功、291项官方暂存路径逐字节恢复；所有旧/新主机与ARM回归通过，Windows独立完整SHA一致。
- 未改区间`[0,348127232)`和`[469762048,2147483648)`与通过版逐字节摘要一致；不写实卡或UFS。
- 封装中一次WSL大文件读取ENOMEM按既有工具策略改用原生Windows I/O重算完整区间，最终摘要仍完全核对，未使用部分hash。
- 本地候选封存恢复标签/检查点：`a733-v204-camera-yolo-live-sealed-20261029`，通过版v203-r2另存不变。
  初始候选标签`a733-v204-camera-yolo-live-candidate-20261029`仍保留；封存补齐被通用build/规则忽略的三份构建日志，镜像字节不变。

启动日志内保留的`boot188`是历史诊断标签，不能单独判断镜像版本；使用配套脚本CRC、内核CRC及`camera-live-v204`状态确认。
此处主机/封装通过不等于真实循环、准确率、热稳定、常开传感器视频或MP4已通过。
