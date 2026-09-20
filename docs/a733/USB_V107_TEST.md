# v107 xHCI halted 接管

## v106 实机结论

v106 的完整 Combo0 USB 模拟参数已经生效：

```text
A733 USB combo v106 ... status=00000001 ... ready=1
combo-v106: checkpoint=0
```

这证明 SerDes/Combo PHY、方向路由和 PMA 初始化已经通过。失败只发生在随后写入 `USBCMD.HCRST` 时：命令位保持为 `2`，且 DWC3 ID、xHCI 状态和两个 PORTSC 全部读回零。

A733 CCU 的 `0x135c bit16` 是 USB2 子系统复位。驱动在 HCRST 前已经对该位产生低到高的硬复位边沿；随后实机状态为 `USBSTS=1`（HALTED）且 CNR 清零。因此第二次 HCRST 是重复复位，并会使此 SoC 上的整个寄存器窗口失效。

## v107 修改

- 保留 CCU USB2 硬复位、DWC3 host 设置以及 v106 Combo PHY 参数初始化。
- 将 `xhci2_halt_reset()` 改为 `xhci2_prepare_halted()`：等待 CNR 清零、停止 RUN、确认 HALTED，并再次核验 DWC3 ID。
- 不再写 `USBCMD.HCRST`；成功日志明确显示 `HCRST suppressed`。
- 增加主机侧回归，覆盖正常接管、MMIO 丢失、CNR 超时和 halt 超时，并断言除了清 RUN 外没有其他 USBCMD 写入。

## 实机复测

烧录 `openvela-a733-cubie-a7z-sd-usb-v107-candidate.img`，保持 USB 麦克风接在 DP Type-C 口，执行：

```text
echo probe > /dev/a733-uvc
cat /dev/a733-uvc
dmesg
```

需要核对：

1. `A733 USB combo v107 ... ready=1`；
2. `CCU-reset halted handoff passed; HCRST suppressed`；
3. `dwc3: id=33313130`，且 `cmd/sts/ports` 不再全部归零；
4. 若任一 PORTSC bit0 为 1，物理连接检测已经通过。

请保留完整 `cat` 和新增 USB 相关 `dmesg`。本版验证控制器与物理端口的稳定接管；xHCI command/event/transfer rings 及 USB Audio Class 枚举是连接检测通过后的阶段。

## 构建与校验

- xHCI halted-handoff 回归：通过。
- Combo PHY CC1/CC2、PMA、IDDQ、复位间隔和 suspend quirks 回归：通过。
- 内核构建、链接通过，并核验包含 `A733 USB combo v107` 标识。
- 内核大小 1908144 字节，SHA256：`ab3416d13296f4b2d26cee80d9b5dcb97317309d0fc1574ee9b946bc94d99b35`。
- 镜像大小 2147483648 字节，SHA256：`7765bf70d3afc3c7757336db9b6adfe260c828d09e6574c925468be823e536c7`。
- 镜像内核回读 SHA256 与构建结果相同；ext4 和 GPT 校验通过。
