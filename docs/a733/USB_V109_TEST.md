# v109 A733 USB2 应用窗口修复

## v108 实机结论

v108 的 host-only PHY soft reset 已成功完成，DWC3 ID 和 Combo PMA ready 均保持稳定，但端口仍停在 `PORTSC=000002a0`：

```text
A733 USB PHY v108: soft reset passed id=33313130 ...
CCU-reset halted handoff passed; HCRST suppressed
ports=000002a0/000002a0
usb2-phy: iscr=02000000 line=1 vbus=0
```

`line=1` 表明 USB 麦克风的上拉已经到达 USB2 PHY；`vbus=0` 表明 PHY 传给 SIE 的 VBUS Valid 仍未成立。

## 根因与 v109 修改

A733 User Manual V0.92 第 18.4.4 节规定，USB3.1 DRD 的 Application Registers 位于控制器基址加 `0x00100000`，因此实际窗口是 `0x06b00000`。旧版厂商 xHCI glue 中的 `+0x10000` APP/PIPE/EXT 定义属于其他 SoC，在 A733 上写入后不会控制 USB2 PHY，这与 v108 的实机读回结果完全一致。

v109 做了以下修改：

- 删除对错误 `0x06a10000` APP/PIPE/EXT 窗口的写入。
- 在真实的 `U2P0_ISCR`（`0x06b00000`）设置 `[13:12]=11b`，按手册把送往 SIE 的 VBUS Valid 强制为高。
- 保留 v108 已验证的 DWC3 PHY soft reset、Combo PHY ready 检查和 xHCI HCRST 禁止逻辑。
- 报告中用真实的 `iscr`、`physts` 代替无效的 `app`、`ext` 字段。

## 实机复测

烧录 `openvela-a733-cubie-a7z-sd-usb-v109-candidate.img`，保持 USB 麦克风连接在 DP Type-C 口，执行：

```text
echo probe > /dev/a733-uvc
cat /dev/a733-uvc
dmesg
```

重点保留以下信息：

1. `A733 USB combo v109 ... ready=1`；
2. `A733 USB PHY v109 ... iscr=02003000`，确认强制 VBUS 位写入并保持；
3. `ports=`，重点看 bit0（CCS）是否置 1；
4. `usb2-phy:` 行中的 `line`、`vbus`；
5. DWC3 ID 必须继续为 `33313130`。

如果端口 CCS 置位，下一步将初始化 xHCI DCBAA、command/event/transfer rings，读取 USB Audio Class 描述符。如果 `iscr=02003000` 仍无 CCS，日志将把问题进一步限定到 UTMI 端口映射或 xHCI 端口布局。

## 构建与校验

- USB2 PHY/Combo PHY/SIE VBUS 状态机回归：通过。
- xHCI halted handoff 与 HCRST 禁止回归：通过。
- 内核构建、链接通过，并核验包含 `A733 USB PHY v109` 标识。
- 内核大小 1908144 字节，SHA256：`35fe57a199a04d838feb06d14e89ee4379df54ec40ac69fff88f12c0a1f6703c`。
- 镜像大小 2147483648 字节，SHA256：`b6a4275dffbd57f4320bcdc4a6a827d52d2ecd33dd4aaefbfdbcf5b2668fe963`。
- 镜像内核回读 SHA256 与构建结果一致；ext4 和 GPT 校验通过。
