# A733 USB 摄像头 v58：安全的 USB2/xHCI2 物理链路检查

v58 延续 v57 的正确硬件路径：外置 USB 摄像头必须通过 Host/OTG 转接器连接 Cubie A7Z 的 USB-C 3.1/DP 接口，该接口对应 A733 USB2/DWC3/xHCI2。USB-C 2.0/供电口为 device-only，不能用于本阶段。

相比 v57，v58 不再在命令环和事件环尚未安装时复位或启动 xHCI。它只完成官方电源域、VBUS、时钟、复位、USB2 PHY 和 DWC3 Host 模式初始化，然后在 xHCI 停机状态读取根端口 `PORTSC`。这样不会污染后续正式平台 xHCI 驱动的控制器状态。

## 实机验收

先插好摄像头和 Host 转接器，再开机并执行：

```text
cat /dev/a733-uvc
echo probe > /dev/a733-uvc
cat /dev/a733-uvc
dmesg
```

需要回传 `/dev/a733-uvc` 的 `controller`、`dwc3`、`clock` 三行以及最后一条 `A733 UVC` 日志。成功的物理连接应满足：

- `id=5533xxxx`；
- `checkpoint=0`；
- `ports=` 的至少一个 `PORTSC` 最低位为 1；
- `PL2-vbus=1`；
- `USBSTS` 可以显示 halted，这是本阶段的预期状态。

连接位确认后，下一版将接入平台 xHCI 的 DCBAA、命令环、事件环和 IRQ 155，再完成 USB 描述符枚举、UVC Probe/Commit、视频采集节点及 NPU 动态输入。
