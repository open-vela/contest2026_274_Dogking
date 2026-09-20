# v108 USB2 PHY 到 DWC3 接管

## v107 实机结论

v107 已解决 HCRST 造成的寄存器窗口丢失：

```text
A733 USB combo v107 ... ready=1
CCU-reset halted handoff passed; HCRST suppressed
id=33313130 cmd=00000000 sts=00000001 reset=0
```

新的失败点为 `PORTSC=000002a0/000002a0`，即端口已供电并停在 RxDetect，但 xHCI 没有置 CCS。与此同时 `USB2_ISCR=02000000` 的 line-status 为 1，且 ET7304 报告 CC2/Rd、VBUS present。这证明外部 Type-C 连接、VBUS 和 USB2 设备上拉已经到达独立 USB2 PHY，断点位于 USB2 PHY 到 DWC3/xHCI 的内部交接。

## v108 修改

- 在 USB2 PHY 和 Combo PHY 初始化完成后执行厂商 Linux `dwc3_core_soft_reset()` 的 host-only PHY reset 顺序。
- 置位 USB3 `GUSB3PIPECTL.PHYSOFTRST` 与 USB2 `GUSB2PHYCFG.PHYSOFTRST`，等待 2 ms，清除后等待 50 ms 时钟同步。
- reset 后重新应用 host capability、USB2/USB3 suspend quirks 和 Allwinner wrapper 设置。
- reset 后核验 DWC3 ID 与 Combo PMA ready；任一状态丢失即停止。
- 继续禁止写入已确认会使 A733 寄存器窗口失效的 xHCI `USBCMD.HCRST`。

## 实机复测

烧录 `openvela-a733-cubie-a7z-sd-usb-v108-candidate.img`，保持 USB 麦克风连接在 DP Type-C 口，执行：

```text
echo probe > /dev/a733-uvc
cat /dev/a733-uvc
dmesg
```

重点保留以下信息：

1. `A733 USB combo v108 ... ready=1`；
2. `A733 USB PHY v108: soft reset passed ...`，特别是 `u2/u3/app/ext/iscr`；
3. `CCU-reset halted handoff passed; HCRST suppressed`；
4. `ports=` 是否从 `000002a0` 变为 bit0 为 1 的连接状态；
5. DWC3 ID 必须继续为 `33313130`。

物理连接通过后，后续阶段是初始化 xHCI DCBAA、command/event/transfer rings，并迁移描述符读取到 xHCI 控制传输，最后解析 USB Audio Class 接口。

## 构建与校验

- xHCI halted-handoff/HCRST 禁止回归：通过。
- Combo PHY、suspend quirks 与 DWC3 PHY soft-reset 回归：通过。
- 内核构建、链接通过，并核验包含 `A733 USB PHY v108` 标识。
- 内核大小 1908144 字节，SHA256：`0a30f30375ac764163b960136d3d83adca9f67d30bc0b4566da6d1436bff8632`。
- 镜像大小 2147483648 字节，SHA256：`71b3c78ad9cfc808b80d9ffe6329baadb6dfe23f951e7f15a42fe3645f165b8f`。
- 镜像内核回读 SHA256 与构建结果一致；ext4 和 GPT 校验通过。
