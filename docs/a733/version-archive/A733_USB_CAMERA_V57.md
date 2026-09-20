# A733 USB 摄像头 v57：纠正到 USB2/xHCI2

## v56 实机结论

无摄像头和已接摄像头时，EHCI0 的 `PORTSC` 均为 `0x00001000`，连接位始终为 0。该结果不是摄像头故障，而是控制器选择错误。

Cubie A7Z 官方 DTS 明确规定：

- USB0（USB-C 2.0 与供电口）为 device 模式；
- USB1 专用于板载 FCU760K Wi-Fi/蓝牙；
- USB2（USB-C 3.1/DP）为默认 host 的 DWC3/xHCI2，基址 `0x06a00000`；
- USB2 PHY 基址 `0x06b00000`；
- USB2 电源域为 PCK600 domain 8；
- 外部 USB-C 3.1 VBUS 由 PL2 控制。

## v57 范围

v57 将 `/dev/a733-uvc` 迁移到正确的 USB2/xHCI2，完成：

1. PCK600 USB2 电源域开启；
2. USB2 时钟与复位开启；
3. U2 PHY 电阻校准、退出 SIDDQ、写入官方调谐值 `0x143338d6`；
4. PL2 输出高电平，为 USB-C 3.1 端口提供 VBUS；
5. DWC3 强制 Host 模式并启动 xHCI；
6. 同时读取两个 xHCI 根端口的 `PORTSC`。

本版的验收目标是验证实机物理端口。xHCI 传输环、UVC 描述符和视频流将在连接位确认后启用，避免继续在错误硬件路径上叠加代码。

## 实机测试

摄像头必须通过 OTG/Host 转接器接到 USB-C 3.1/DP 口，不要接到供电用 USB-C 2.0 口。

```text
cat /dev/a733-uvc
echo probe > /dev/a733-uvc
cat /dev/a733-uvc
dmesg
```

请保留 `controller`、`dwc3` 和 `clock` 三行。插入设备后，`ports=` 至少一个值的最低位应为 1。
