# v106 Combo0 USB PMA 初始化

## v105 实机结论

v105 成功保持 DWC3/xHCI 寄存器窗口可读，并在 HCRST 前停止了错误操作。实机两次 probe 均显示：

```text
id=33313130
combo-v105: checkpoint=-110
ctrl=00001111/01100001/00000000/00000011
status=00000000 ready=0
```

Type-C 为 source/host、CC2，USB2 电源域与时钟开启。失败点已从 v103 的 HCRST 后寄存器窗口丢失，收敛为 Combo0 USB3 PMA 未初始化。

Cubie A7Z 的厂商 DTS 指定 `serdes1v8-supply = <&reg_cldo5>`，CLDO5 为 boot-on/always-on；实机问题不是 Type-C 角色或外部 VBUS 检查点。v105 仅写入数字控制、PIPE 映射和复位位，没有写入厂商 `combo0_usb_param_config()` 的 USB-only 模拟参数。

## v106 修改

- 从本地厂商 BSP `drivers/phy/sunxi-cadence-combophy.c` 提取 USB-only、SSC-enabled 的寄存器偏移和值，生成 `a733_combo0_usb_tables.inc`。
- 保持厂商顺序，将参数分为 common0、方向路由0、common1、活动通道1、common2 五段。CC1 和 CC2 分别选择 normal/reverse 表。
- 在退出 IDDQ、释放 PHY/PIPE reset 和等待 PMA-ready 前写入参数表。
- 保留 v105 的 CNR/HCRST 保护；PMA-ready 仍为零时不会执行 HCRST。
- 参数提取脚本保留在上级 `extract-a733-combo0-usb-table.py`，可从厂商源重新生成并审计全部值。

表项数：CC2 为 40+16+42+68+3；CC1 为 40+16+42+60+3。host 模拟回归验证 CC1/CC2 活动/非活动通道、SSC、IDDQ、复位间隔、ready 超时与 DWC3 suspend quirks。

## 实机复测

烧录 `openvela-a733-cubie-a7z-sd-usb-v106-candidate.img`，麦克风接 DP Type-C 口，执行：

```text
echo probe > /dev/a733-uvc
cat /dev/a733-uvc
dmesg
```

预期依次越过：

1. `A733 USB combo v106 ... ready=1`；
2. `combo-v106: checkpoint=0`；
3. `reset=0`，DWC3 ID 仍为 `33313130`；
4. PORTSC 出现 CONNECT。

请保留完整输出。即使这四项通过，当前代码中的 xHCI ring/控制传输仍是后续阶段，UAC 麦克风录音尚未完成。

## 构建与校验

- v106 内核构建、链接通过，并核验包含 `A733 USB combo v106` 标识。
- 内核大小 1908144 字节，SHA256：`bb85b27c786dd3f6972fb1040eef8e4f33d86032a78b6f8c44883b7d42a32a69`。
- 镜像大小 2147483648 字节，SHA256：`38d5ba734c5773d66adaa2f8c130315cfab8f686b633466e540a7562f2381a83`。
- 镜像内核回读 SHA256 与构建结果相同；ext4 和 GPT 校验通过。
