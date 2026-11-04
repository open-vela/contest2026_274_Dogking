# v131 文件 RGB → NPU → LVGL 实机验收

日期：2026-10-11

状态：**hardware-passed（文件输入联动）**。本结论不包含 UVC 摄像头采集、实时预处理
或数小时压力测试。

## 测试环境

- 镜像：`openvela-a733-cubie-a7z-sd-yolo-lvgl-v131-candidate.img`
- 板端地址：`192.168.1.220`
- 显示：ST7735，128×160，RGB565
- 模型：`/data/npu/yolov8n-pcq-v3.a7pm`，8017024 bytes
- dog 输入：`/data/npu/yolov8n-dog-640x640-rgb.bin`，1228800 bytes
- black 输入：`/data/npu/yolov8n-black-640x640-rgb.bin`，1228800 bytes

Windows 到开发板 ping 为 4/4 成功，随后两个 SSH 会话同时连接；一个会话独占
`display vision 180`，另一个会话依次提交 dog 和 black 输入。

## dog 输入

```text
status=0 hw=0 elapsed=1686627 us irq=00000010 idle=7fffffff polls=165
input=1228800/2d95eaba outputs=6 candidates=31 detections=3
det[0]: class=16 score=0.910 box=108.36,263.05,255.86,530.70
det[1]: class=1 score=0.907 box=109.53,193.93,474.20,429.57
det[2]: class=2 score=0.654 box=389.31,143.31,575.94,224.15
npu: vision sequence=1 boxes=3 queued=3
```

显示端收到同一序号并绘制三种颜色的三个框：

```text
display: vision frame=1 boxes=3 npu=1686627 us
display: vision det=0 class=16 score=0.910
display: vision det=1 class=1 score=0.907
display: vision det=2 class=2 score=0.654
```

## black 输入

```text
status=0 hw=0 elapsed=1680635 us irq=00000010 idle=7fffffff polls=165
input=1228800/00000000 outputs=6 candidates=0 detections=0
npu: vision sequence=2 boxes=0 queued=0
```

显示端收到新的零框帧头并清除上一帧的三个框：

```text
display: vision frame=2 boxes=0 npu=1680635 us
```

实体屏照片与日志一致：第一张有三个彩色检测框，第二张不再保留检测框。这同时证明
黑帧不会显示上一帧的陈旧结果。

测试后内存状态：

```text
total=4239872000 used=226800 free=4239645200 maxused=238960
```

`dmesg` 未观察到 NPU、消息队列、SPI、LVGL、异常或堆错误。

## 网络附带观察

第一次连接同名 `ZBCK-E-5G` BSSID 时，固件连续收到 WPA2 M1，但 AP 以 reason 15
断开；重新扫描后选择另一个同名 BSSID，四次握手与 DHCP 立即通过并取得
`192.168.1.220`。因此本次网络现象属于同 SSID 多 BSSID 的接入点差异，而不是
FCU760K 驱动失效。成功连接后 ping 和 SSH 均正常。

## 结论边界

已通过：已知 RGB 文件 → VIP2 YOLOv8 → 非阻塞消息队列 → LVGL 框层，并验证新帧
清除旧框。

未通过/未实现：USB UVC `/dev/video0`、摄像头实时帧、颜色转换与 letterbox、连续
推理、实时画面预览、端到端掉帧/温度统计和数小时压力测试。
