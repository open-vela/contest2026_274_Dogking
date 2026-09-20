# Cubie A7Z / A733 USB 摄像头适配 v56

## 本阶段结论

v56 在 v55（Wi-Fi 自动重连、SSH/FTP 可选自启动、NPU 与准备模型运行时）基础上加入外置 USB 摄像头的第一个硬件关口：A733 USB0 EHCI 主机初始化、USB 设备枚举、UVC VideoStreaming 描述符解析，以及 `/dev/a733-uvc` 诊断接口。

此版本用于获得实物摄像头准确的格式、分辨率、帧间隔、接口 alternate setting 和传输端点。它还不是最终实时取帧版本；只有读到实物的端点参数后，才能安全启用 EHCI 等时或批量传输，避免错误 DMA 配置破坏当前已稳定的 Wi-Fi、存储和 NPU 链路。

## 接口选择

- 摄像头接 Cubie A7Z 的外置 USB-C 2.0 OTG/供电口（USB0）。
- 需要带 Host/OTG 功能的转接头；只有充电功能的 Type-C 线不能使用。
- 板载 FCU760K Wi-Fi/蓝牙独占 USB1，本阶段不占用它。
- 若摄像头耗电较大或枚举不稳定，使用独立供电的 USB Hub。
- 当前 USB0 数据路径先实现 EHCI 高速设备。若报告显示端口被交给 OHCI，说明摄像头是全速设备，下一版需补 OHCI 路径。

## 烧录与首次验证

烧录镜像：

`openvela-a733-cubie-a7z-sd-usb-camera-v56-candidate.img`

启动后执行：

```sh
ls /dev/a733-uvc
cat /dev/a733-uvc
dmesg
free
cat /dev/npu0
```

如果先开机后插入摄像头，或需要重新探测：

```sh
echo probe > /dev/a733-uvc
cat /dev/a733-uvc
dmesg
```

请保存并返回 `cat /dev/a733-uvc` 与 `dmesg` 的完整输出。后续实时取帧代码将以其中的 `mode[]` 和 `endpoint[]` 为依据。

## 判定标准

成功的描述符阶段应满足：

- `controller ... checkpoint=0`
- `device: enumeration=0` 且 VID/PID 非零
- `uvc: parse=0`
- 至少一个 `mode[]`
- 至少一个方向为 IN 的 `endpoint[]`

常见失败值：

- `-19`：未检测到设备，优先检查 OTG 转接、线材、插口和 VBUS 供电。
- `-95`：端口所有权转给 OHCI，通常是全速 USB 摄像头；不是 NPU 或模型故障。
- `-7`：摄像头配置描述符大于当前 4096 字节诊断缓冲，需要扩大缓冲。
- `enumeration=0` 但 `parse` 失败：设备能枚举，但不是标准 UVC VideoStreaming 布局，需根据原始描述符适配。

## 最终全链路路线

实物端点确认后按以下顺序继续，并保持每个阶段可独立回退：

1. 发送 UVC `VS_PROBE_CONTROL` / `VS_COMMIT_CONTROL`，选择摄像头真实支持的模式。
2. 为等时端点建立 EHCI periodic/iTD 队列；若摄像头提供 bulk 端点则使用 qTD 队列。
3. 解析 UVC payload header、FID/EOF，完成帧边界与丢包检测，注册 `/dev/video0`。
4. 优先选 YUYV，转换并 letterbox 到 YOLOv8 的 640×640 RGB/NCHW 输入；若设备只支持 MJPEG，再加入 JPEG 解码。
5. 把预处理结果写入现有 NPU 动态输入缓冲，运行 `/data/npu/yolov8n-pcq.a7pm`。
6. 复用现有 CPU 后处理/NMS，输出类别、置信度和坐标；同时生成终端报告及 `/data/detections.json`。
7. 做连续运行、断插恢复、帧率、内存保护和 Wi-Fi/SSH/FTP 并发回归测试。

最终验收目标是：摄像头连续采集 → 图像预处理 → VIP2 NPU YOLOv8 推理 → CPU 解码/NMS → 终端/JSON 输出，且不影响现有网络和存储服务。

## 构建和封装

在当前 openvela/WSL 环境中执行：

```sh
bash /mnt/d/My-Program/AI-Agent-Program/GPTsprogram/Openvela/A733-A7Z-ALL-Files/build-a733-usb-camera-v56.sh
bash /mnt/d/My-Program/AI-Agent-Program/GPTsprogram/Openvela/A733-A7Z-ALL-Files/package-a733-usb-camera-v56.sh
```

增量构建：

```sh
bash /mnt/d/My-Program/AI-Agent-Program/GPTsprogram/Openvela/A733-A7Z-ALL-Files/build-a733-usb-camera-v56.sh --incremental
```

封装脚本仅复制 v55 候选镜像并替换新镜像中的 openvela 内核；不会写物理 SD 卡，且会验证启动区和数据分区未被改变。
