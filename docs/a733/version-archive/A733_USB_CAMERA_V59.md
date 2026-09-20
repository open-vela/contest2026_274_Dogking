# A733 USB 摄像头 v59：USB2/SerDes 上游门控修正

v58 实机日志显示 `PCK_USB2=on`、PL2 VBUS 已开启，但 `0x06a00000` 的 xHCI capability、DWC3 ID 和 `0x06b00000` 的 USB2 PHY 寄存器全部读回零。这不是 UVC 摄像头兼容性问题，而是控制器寄存器窗口尚未被时钟送达。

v59 根据 A733 官方 `sun60iw2p1.dtsi`、CCU 驱动和 `sunxi-cadence-combophy.c` 补齐：

- USB system AHB 与 SerDes AHB gate（含 CCU 写入 key）；
- USB2 suspend 精确选择 24 MHz；
- SerDes PHY configuration 100 MHz；
- `DCXO_SERDES0` 与 `RST_BUS_SERDES`；
- SerDes subsystem 内 USB ACLK、HCLK 和 USB2 PHY reset；
- 在父时钟稳定后产生一次 DWC3 reset edge。

本版仍不会在 xHCI 环结构未创建时设置 `USBCMD.RUN`，避免再次破坏控制器状态。它首先验证 DWC3/xHCI 寄存器可见和 USB2 根端口连接。

## 实机验收

摄像头通过 Host/OTG 转接器接到 USB-C 3.1/DP 接口，开机后执行：

```text
cat /dev/a733-uvc
echo probe > /dev/a733-uvc
cat /dev/a733-uvc
dmesg
```

请回传 `controller:`、`dwc3:`、`clock:`、`serdes:` 四行和最后一条 `A733 UVC` 日志。关键判定是：

- `dwc3: id=5533xxxx`；
- `controller: version`、`caplen`、`maxports` 不再为零；
- `serdes: usb-bgr` 至少包含 `0x00030010`；
- 摄像头连接后，某个 `ports=` 值最低位为 1。

若寄存器可见而连接位仍为零，下一步只调整 USB2 PHY/Type-C 角色；若连接位为一，则直接进入完整 xHCI ring、USB 描述符和 UVC Probe/Commit。
