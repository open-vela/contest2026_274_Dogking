# v198：单帧摄像头 → 同输入 YOLO → LVGL 底图/叠框候选

## 范围

以 v197 候选为基线，v196 的一次真实包级通过记录仍单独保留。
**v197 没有回传板测结果，本版也尚未实机出图/识别验收。**
本轮完成的是可离线测试的产品管线和取帧错误收敛，不跳过前置硬件门禁：

1. 仍只允许每次脚本冷启动一次真实 RAW DMA；没有后台无限取流或自动重试。
2. 同一份验证通过的 RAW，导出显示预览和 YOLO 的 640×640 RGB24 输入。
3. 新增 `display vision-image`，底图来自该模型输入，检测结果通过既有 NPU 后端进入 LVGL。
4. 通过输入 CRC、序号和整批完整性拒绝错图/错框、旧消息、缺包和混杂结果。

不改 NPU MMU、DMA、IRQ、模型包、算子或后处理，不更改音频/LLM/UFS。
不是 ISP/AWB/AE 色彩验收，不保证未校色 RAW 的检测准确率，不是实时视频或 FPS 验收。
标准 V4L2、ISP 和连续采集仍需在真实单帧验证后推进。

## 实机命令

烧录会覆盖目标卡对应分区；先备份现卡模型和配置。本轮开发没有写物理 SD/UFS。
断电连接 V1.11 的 J5/Camera 13M 214 和既有 ST7735 接线。烧录候选后进入 SD U-Boot：

```text
sunxi_card0_probe
mmc dev 0
mmc rescan
mmc part
ext4load mmc 0:3 0x4fc00000 /boot/openvela/a733/camera-v198.scr && source 0x4fc00000
```

进入 NSH 后直接捕获，不要先跑旧版 prepare/id/mode/link 试验。
输出目录必须不存在；失败不应继续 NPU 联调。

```text
camera capture /data/camera198
camera control status
ls -l /data/camera198
cat /data/camera198/COMPLETE.txt
display image /data/camera198/preview.rgb
```

要求 frame published=1、error/cleanup=0、owned=0；模型输入 1,228,800 字节，
RAW 4,147,200、preview.rgb 61,440 字节。只有文件全部写入/sync/close 成功，
COMPLETE.partial 才 rename 为 COMPLETE.txt；失败时保留部分目录但没有完成标志。
同一已发布帧可 `camera export /data/camera198-other rggb` 比较 Bayer，不重启硬件。

确认真实场景和 RAW/PPM 后，再运行已上板验证过的固定模型：

```text
ls -l /data/npu/yolov8n-pcq-v3.a7pm
npu info
npu selftest
display vision-image /data/camera198/model-rgb.bin 120 &
sleep 2
npu yolo-ui /data/camera198/model-rgb.bin
```

不要同时运行其他 pet/demo/vision/raw/image/fps 显示任务。
也可两窗口运行，等显示端打印 `waiting for matching inference` 后再提交 NPU。
固定模型契约：8,017,024 字节，SHA256
3c2891ef77ce9a44dd02cd32bdd7db5ec92d14185014e6de8db5803a6cbecce2；
模型缺失时恢复现有受控资产，不下载/替换为任意同名模型。

验收必须同时看到：相机实际场景、NPU 成功及匹配输入 CRC、显示端
`complete vision batch=... input-bound=1`、与场景一致的检测框；零框也可能是正常结果，
不能仅凭完成标志证明识别准确。拍摄照片/导出 RAW 和 PPM，保存全部输出。

负面测试：保持底图不变，用 CRC 不同的 dog/black 旧输入提交 `npu yolo-ui`。
显示端应打印 `input CRC mismatch`，清框并显示橙色状态条，不把其他图的结果盖在本图上。
先比较双方 CRC，CRC 相同不是这项负测。退出/超时后显示资源正常回收。
采集失败时发回完整 `camera control status`；owned=1 或 cleanup 非零则重启，禁止反复初始化。

## 数据与界面约定

- YOLO 采用原始 RAW 经 CPU Bayer/letterbox 的 RGB24，padding=114。
  1080p → 640×360，model pad-y=140。显示预览的自动对比度不用于模型输入。
- LVGL 使用同一模型文件的中心 5:1 采样：128×128，位置 (0,24)，框在同一模型空间映射。
  保持比例，避免原来把正方形模型区拉伸为 128×140；取整误差至多约一显示像素。
- IPC 保持 32 字节，协议版本升至 2。FRAME x1/y1 携带输入 CRC 的低/高 16 位，
  x2/y2=640；DETECTION.value 也携带 CRC；FRAME.value 仍是 NPU 延迟微秒。
- CRC 是 NuttX crc32part 种子 0、无首尾 XOR 的约定，不是 zlib 默认；全零输入 CRC=0。
  仅用于偶发错配检测，不是密码学绑定或认证。
- 序号采用原子分配且跳过零，单个发布批次保留自己的序号，避免并发发送借用其他批次序号。
  只在所有检测索引齐全后画框；重复/旧序号、错输入、非法框、缺包不形成有效整批。
  启动时丢弃旧队列，单次 drain 最多 8 条；不靠无限队列轮询阻塞 LVGL。

## 取帧审查修正

v197 在首次 frame-done 后可能漏掉停机期间才出现的 DMA/parser 错误。
现在源停止、FIFO drain 已确认后，在关时钟前再次读取错误和 IOMMU 映射状态。
迟到数据错误会拒绝发布，但可正常恢复已停止资源；I/O/停机不确定仍保留资源到重启。
IOMMU enable 变化的首错地址改为实际 enable 寄存器，不再错误指向未变化的 bypass。
状态中加入 frame-final；RAW 实验不再显示“NO-DDR-WRITES”；报告溢出返回错误而非静默截断。

## 验证与构建

主机/ARM r2 通过：278 个单帧事务场景，269 个回调后故障注入；真实控制路径模拟
覆盖迟到错误禁止发布；文件输出含四 Bayer、模型 padding、独立 CRC、完成标志写失败；
实际生产 loader、NPU 消息生产和 LVGL 消费函数在模拟 MQ/LVGL 下验证了整批、缺包、
错输入和序号回绕。目标最大栈：控制 1184、camera 1904、display 2112、NPU CLI 1216 字节。
这些结果不是实机摄像头或 NPU 新路径验收。旧 dog/black 文件 NPU 联动板测结论不扩大。

```sh
bash tools/build-a733-camera-v198.sh out/camera-build-v198-r1 out/ort-static-v185-r2
python3 tools/check-a733-camera-v198.py \
  --build ../../quickly-openvela/cmake_out/cubie-a7z_nsh_v192_camera \
  --output out/camera-check-v198-r3 --evidence docs/evidence/camera-v198-host-r3
```

每次使用全新输出目录。保留 v197 镜像/标签及 v196 包级通过版；
最终发布身份、ELF 运行时与帧池、镜像 SHA 和恢复信息追加在最终发布证据。

## 最终候选身份

- 固件源码提交：735c1ddd9c1c9a5fe62ccbfb0cf614156f774d9e。
- 镜像：openvela-a733-cubie-a7z-sd-v198-camera-vision-candidate.img，2,147,483,648 字节。
- 镜像 SHA256：d9236d51985f26a713aca55d5fd19cb94aa0f676bc059db541b575371c44ed07。
  Windows 原生 Get-FileHash 与打包器独立计算一致。
- Image：7,062,048 字节，CRC32=9772b064；
  SHA256=a213729856e97e7bdfb556b072b2990d5fc42c27238c481aafebba2349790f39。
- ELF：9,609,104 字节；运行时末端 0x45dcd000，独立 4 MiB 帧池
  0x458fb000..0x45cfb000，恒等映射、页对齐、未与 NPU arena 混用；最终 ELF 断言通过。
- 启动脚本 SHA256：1d99a37c7b545f43a849cb8cf6c43038405f10767d1ea311a1c44888f1b37f83。
- 最终配置 r3 全回归通过，SDK 289 项逐字节恢复。只改镜像系统分区，其他字节区间摘要
  与 v191 原始基线相同；既有模型 FAT 未重写，旧镜像未修改。
- 完整发布审查：docs/evidence/camera-v198-final；目标对象/日志/来源哈希：
  docs/evidence/camera-v198-host-r3；十项固件/脚本封存：out/v198-release-20261028。
- 本地候选标签和恢复快照：a733-v198-camera-vision-candidate-20261028。
  通过板测后再追加独立通过标签；当前 hardware_verified=false。
