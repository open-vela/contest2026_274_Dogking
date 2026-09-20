# v110 USB2 PHY Host ID 修复

## v109 实机结论

v109 已验证真实 A733 应用窗口和强制 VBUS 写入正确：

```text
iscr=03003000 line=1 vbus=1
ports=000002a0/000002a0
```

USB2 PHY 已检测到设备线路状态，VBUS Valid 也已经送到 SIE，但 `ISCR[15:14]=00` 仍选择外部 ID。Type-C 插座没有传统 USB OTG ID 引脚，ET7304 报告的 source/host 角色尚未反映到 USB2 PHY 的 SIE 输入。

## v110 修改

- 在 `U2P0_ISCR[15:14]` 写入 `10b`，按 A733 手册强制 ID 为低，即 Host。
- 保留 `U2P0_ISCR[13:12]=11b` 的强制 VBUS Valid。
- 预期实机读回值由 `03003000` 变为 `0300b000`。
- 报告新增 `u3utmi`（CCU `0x1360`）和 `u2pipe`（CCU `0x1364`）原始值；本版只读取，不改变这两个时钟。
- 保留 Combo PMA、PHY soft reset 和 xHCI HCRST 禁止逻辑。

## 实机复测

烧录 `openvela-a733-cubie-a7z-sd-usb-v110-candidate.img`，连接 USB 麦克风后执行：

```text
echo probe > /dev/a733-uvc
cat /dev/a733-uvc
dmesg
```

重点保留：

1. `A733 USB PHY v110 ... iscr=0300b000`；
2. `ports=` 是否有端口 bit0（CCS）置位；
3. `clock:` 行中的 `u3utmi` 和 `u2pipe`；
4. `line`、`vbus`、DWC3 ID 和 Combo ready。

## 构建与校验

- USB2 PHY Host ID/VBUS 状态机回归：通过。
- xHCI halted handoff 与 HCRST 禁止回归：通过。
- 内核构建、链接通过，并核验包含 `A733 USB PHY v110` 标识。
- 内核大小 1908152 字节，SHA256：`198f5957170d27347b08431149c0352e84df5c851bd14315b7d4dd42f7964ab2`。
- 镜像大小 2147483648 字节，SHA256：`f2ab045a5f4588006f58781c0138dad69c34740a396a33621819524cfa09f9ae`。
- 镜像内核回读 SHA256 与构建结果一致；ext4 和 GPT 校验通过。
