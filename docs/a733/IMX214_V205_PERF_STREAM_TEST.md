# v205：CRC／预处理／单次刷新／模型常驻与受控连续采集候选

> 2026-10-29 后续板测：同输入普通/常驻冷/常驻热核验和逐帧循环通过；常开传感器模式第二帧
> `DMA=00220083`、`error=-74` 失败，底层恢复成功但应用错误报告 `cleanup=-74`。
> **v205 连续模式未通过。** 以下保留原计划和封存历史；修复见 [v206](IMX214_V206_STREAM_REPAIR_TEST.md)。

日期：2026-10-29。v204 已有用户实机通过，原镜像、源码、构建缓存和恢复点不覆盖。
v205 是独立实验配置，必须完成主机回归、真实 SDK ARM 编译、完整链接、镜像回读后才能交付板测。
**本文中的功能实现和主机测试不等于 v205 板测通过，不承诺 FPS。**

## 通过版与可回退性

- v204 实机记录：[原始串口与核对结果](../evidence/camera-v204-board-20261029/README.md)。
- 板测归档恢复点：`a733-v204-camera-live-board-pass-20261029`，提交 `1c3cc56`。
- 完整原封存：`a733-v204-camera-yolo-live-sealed-20261029`；旧源码、文档不删。
- 保留原 2 GiB 镜像 `openvela-a733-cubie-a7z-sd-v204-camera-yolo-live-candidate.img`。
  SHA256：`f21a9d0bd8aab20264db3e44273ebf372c629f33ee7bba6c49659f0e2e82326d`。
- v204 实测预览约 1.695 FPS；每帧推理约 1.092 FPS；每三帧推理约 1.507 显示 FPS／0.502 推理次每秒。
  这不是 NPU 理论上限或长时温度测试。日志未独立记录该次内核 CRC，不补造二进制身份。

## 本轮实现

1. CRC 保持 NuttX raw seed=0、无首尾异或约定，改为 256 项查表；仍校验真实输入和输出，不关闭校验。
2. 每会话建立 RAW10→RGB 的 tone/坐标计划：三色 1024 项 LUT、固定坐标表、Bayer 内部快速路径。
   原慢路径保留；随机边缘／四 Bayer／RAW10 两种排布／180°旋转逐像素对照，布局改变必须拒绝。
3. 检测帧只提交一次“真实底图＋同帧识别框”；无目标清框。旧两次提交 API 保留回归，杜绝旧框配新图。
4. 每会话固定模型只从媒体读入一次官方 tmpfs，再导入驱动私有 CPU 快照。模型限定为已通过 YOLOv8n 包
   8017024 字节、raw CRC `f5f558c5`，不是任意模型加载器。CPU 快照不用于 DMA。
   首次检查完整描述符／模型；热运行仅复用前后 CRC 均证明未被修改的 LOAD 区，命令、输入和可变区重新装载。
   每轮仍建立／恢复原 MMU 映射；全部输出重新执行、初始化和校验。其他客户端修改池、失败或关闭会使热缓存失效。
   只对已 invalidate＋CRC 验证、CPU 未写入的保留 LOAD 区省去重复 clean；命令、输入、输出、可变区和 MMU／尾页仍同步。
   没有删除硬件执行、IRQ、守卫、超时和恢复检查，没有改变 NPU 时钟或原 ABI v4。
5. 额外 `live-stream`：传感器只配置、开流一次；每帧仍使用单帧 SCAP，停止 parser/DMA 并确认排空，
   守卫与两次指纹检查通过才供 CPU 读取。CPU RAW 副本与永久内核 DMA 缓冲不同，可预取下一帧并与 CPU/NPU 重叠。
   队列最多一张，读者未释放不能覆盖。**不是循环 DMA，也不是 MP4/AVI 文件录制。**
6. 增加 setup/稳态/预处理/CRC/输入写入/显示 CRC/绘制/SPI flush、NPU 导入/准备/映射/输入同步/执行/输出/解码/恢复计时。
   `prepare` 包含其子项，不能再次相加；`hw-exec` 含初始化与等待，不是纯 MAC 时间。每帧 NPU 总阶段还含输入写入／ioctl。
   默认只打印前两帧与每十帧细节，状态累计所有成功帧，不把采样日志行数当实际帧数。

## 构建与主机回归

```sh
python3 tools/test-a733-perf-v205.py --output out/perf-check-v205-new
bash tools/build-a733-camera-v205.sh out/camera-build-v205-new out/ort-static-v185-r2
```

首次构建要求新缓存 `cubie-a7z_nsh_v205_camera_perf`；不覆盖 v204 缓存。
增量仅允许显式 `--incremental` 并匹配配置。SDK 临时覆盖后逐字节恢复，共新增两个 `.inc` 路径；
完整构建和最终验收日志另行记录，不用只编译对象代替完整链接。

## 板测顺序（收到独立 v205 镜像后执行）

先验证 SWOUT1 自动检查／读回、屏幕和单帧路径；不手动改电压，不擦 UFS。

```text
display raw8
camera selftest
camera live 10 0
sleep 15
camera live-status
camera live-verify 5 1
sleep 15
camera live-status
camera live 30 1
sleep 40
camera live-status
```

每次确认 `active=0 error=0 cleanup=0`，再进行下一项；`sleep` 是操作间隔，不是验收依据。
`live-verify` 对第一张真实 RGB 依次进行普通执行、常驻冷执行、常驻热执行，比较全部六路输出 CRC 和完整检测列表。
必须看到两行 `equal-legacy=1`、`hit=0/1` 和 `validation: requested=1 verified=1`。
它额外执行两次 NPU RUN，不增加显示帧；`run-ioctls` 单独计数。该命令用于正确性，不作为性能基准。
输出不一致、输入变化、并发客户端打断池归属或模型不符时拒绝发布，不自动重试／回退。
对照旧两次刷新／非常驻推理流程（本版本 CRC 已加速，不能宣称它逐字节等于 v204）：

```text
camera live-legacy 30 1
sleep 40
camera live-status
```

随后独立验证连续传感器模式，不直接从长测开始：

```text
camera live-stream 10 0
sleep 15
camera live-status
cat /dev/a733-camera-control
camera live-stream 30 1
sleep 40
camera live-status
camera live-stream 60 3
sleep 75
camera live-status
```

先移动镜头/目标、再让目标离开，验证底图变化、识别框更新及零目标清框；不要只拍静止灯光。
普通性能命令首次应 `hit=0`，同会话后续通常 `hit=1`；验证命令额外做过冷／热检查，第一条显示帧也可能 `hit=1`。
`hit` 表示同一已加载会话及池的热归属；真正减少拷贝的区数看 `retained`。若为零，不能称为权重免拷贝。
其他客户端使用池或运行失败会使热归属失效。测试期间不要并发执行其他 NPU／ASR／TTS 工作。
首次建模／缓存时间与稳态分别报告；比较同场景、同模型、同推理频率。

停止：`camera live-stop`，等待 `active=0 cleanup=0`。不要 `kill -9` 或 Ctrl+C 强杀后台任务。
源任务无请求 10 秒会有界退出；一次会话最长 3600 秒、32768 帧预算。
帧预算由内核按本次开机累计，其他手动／循环采集也消耗预算；达到上限后拒绝再次采集，不自动重置计数。
如果恢复／DMA 排空不确定，永久帧缓冲和时钟所有权隔离保留，拒绝下一次重试，需要重启后排查；不自动 reset。
短测全过再做 300／1800 秒稳定性测试，记录错误、帧数、温度和内存。尚未完成这些板测时不能称为长视频验收。

保存完整 SSH／串口日志后可离线核对（`--output` 要求新文件）：

```sh
python3 tools/audit-a733-perf-board-v205.py board-v205.txt --output board-v205-report.json
```

默认核对最后一个会话，可用 `--run-index 0` 选第一个。它区分采样行数、固件累计帧数和 NPU RUN 次数；
校验详细采样帧、框、冷热检查与分段时间的绑定，不把串口文字当图像／真实硬件身份认证。

## 未改变／限制

- SWOUT1 仍只允许已核实的 PMIC reg11 `17→1f`，已开启不重复写，读回通过才启动；NuttX 不抢 SCP 电源总线。
- UFS、Agent、语音、Wi-Fi、显示接线和模型输入 640×640 不改；不做格式化、迁移或更换模型精度。
- 摄像头仍是当前已通过的 RAW10 软件处理链，不宣称完整 ISP、AE/AWB 或 30 FPS。
- 新连续路径必须板测；保留 `camera capture`、v205 普通 `camera live` 和原 v204 整镜像回退。
- 接下来应依据新分段数据判断 NPU 执行、内存带宽、CPU 像素或 SPI 的实际瓶颈，而不是仅看总 FPS 判断 NPU 极限。

## 本轮交付状态

初始计划记录：源码与候选构建准备中；最终 Git 提交、镜像 SHA 和封存路径在完整构建／回读完成后追加。现已完成，见下方交付封存。

构建过程可追溯：初始源码点 `e08a764`／`a733-v205-camera-perf-source-20261029` 保留；
`out/camera-build-v205-r1` 在加入上述同输入验收入口前主动中断，包装器状态 2，293 个 SDK 路径逐字节恢复。
它不是通过构建，不用于包装镜像。最终源码修订和完整 r2 构建另行封存，绝不覆盖失败／中断记录。

## 交付封存

完整 r2 构建、293 项 SDK 恢复、14 组主机回归、原生 ARM、完整链接、ABI／硬件实现不变、ELF、
新镜像回读与 Windows 独立完整 SHA 均已通过。[封存证据](../evidence/camera-v205-final/README.md)。
**v205 仍待上板，未测得新 FPS，不宣称 NPU 达到硬件极限。**

| 内容 | 值 |
| --- | --- |
| 新镜像，仓库上一级 | `openvela-a733-cubie-a7z-sd-v205-camera-perf-stream-candidate.img` |
| 大小 | 2147483648 字节（2 GiB） |
| SHA256 | `826fe505c026ffa0c75fc7882008c831f1c33560f6649ba856b07770a16c89a7` |
| 校验文件 | 同名 `.img.sha256` |
| 内核大小／U-Boot CRC32 | 7111272／`2c5d0475` |
| 内核 SHA256 | `27fef4b0b2425e42106305cdd0fabfd1d80db2a7f880d18bcab2d8dfac42bdbf` |
| 固件源码 | `38323b5c81a19b1963059e2dbeca50180179dd53` |
| Git 交付恢复标签 | `a733-v205-camera-perf-sealed-20261029` |
| 二进制／配置／模式／脚本封存 | `out/v205-release-20261029`（10 项） |
| 完整源码及 Git 历史恢复包 | `out/checkpoints/a733-v205-camera-perf-sealed-20261029` |

烧写前备份 SD 的 `/data`；本次只生成独立普通镜像文件，未写板或 UFS。
回退最直接使用已保留的 v204 完整镜像，不要直接执行旧启动脚本去启动新内核。
若自动进入了错误的旧 UFS U-Boot，沿用 SD 初始化流程后加载本版脚本；本轮不删除 UFS 引导内容：

```text
sunxi_card0_probe
mmc dev 0
mmc rescan
mmc part
ext4load mmc 0:3 0x4fc00000 /boot/openvela/a733/camera-v205.scr && source 0x4fc00000
```

必须确认 SD 的 GPT 正常再加载，异常容量／分区错误时不要格式化或直接写设备。
镜像重建：

```sh
python3 tools/check-a733-perf-v205.py --build ../../quickly-openvela/cmake_out/cubie-a7z_nsh_v205_camera_perf --build-report out/camera-build-v205-r2 --output out/camera-check-v205-new --firmware-commit 38323b5c81a19b1963059e2dbeca50180179dd53
python3 tools/package-a733-camera-v205.py out/v205-release-20261029 ../openvela-a733-v205-rebuilt.img out/camera-package-v205-new --firmware-commit 38323b5c81a19b1963059e2dbeca50180179dd53
```

输出路径必须全新；工具拒绝覆盖。新镜像包含 v204 内核／启动脚本备份，但恢复仍建议使用完整通过镜像。
恢复包将大镜像与 10 项封存作为外部哈希引用，不把数 GiB 二进制塞进 Git；保留这些引用文件才能验证完整恢复链。
