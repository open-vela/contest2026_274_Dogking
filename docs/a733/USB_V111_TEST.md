# v111 xHCI 最小运行时启动

## 实机结论

v110 已确认 Type-C 控制器进入 Source/Host，CC2 附着、VBUS 有效，Combo PHY ready，DWC3 标识稳定，且 USB2 SIE 的 Host ID/VBUS 强制位生效。但 xHCI 一直保持 `cmd=0 sts=1`，两个端口都没有 CCS。

A733 手册说明 `SUBSYS_USB3P1_BGR` 的 U3-UTMI 与 U2-PIPE 两个选择位在正常 Combo PHY 模式应保持 0：USB2 使用 USB2 PHY 产生的 UTMI 时钟，USB3 使用 Combo PHY 产生的 PIPE 时钟。因此 v110 中 `u3utmi=0 u2pipe=0` 是正确状态，v111 不修改这两个寄存器。

## v111 修改

在继续禁止 `USBCMD.HCRST` 的前提下，为 xHCI 配置最小、对齐且缓存已清理的运行时结构：

- 8 个设备槽位的 DCBAA；
- 64 TRB 命令环及 Link TRB；
- 64 TRB 事件环和单项 ERST；
- 最多 8 个 4 KiB scratchpad；
- 配置 `CONFIG`、`DCBAAP`、`CRCR`、`ERSTSZ`、`ERSTBA`、`ERDP` 后置 `USBCMD.RUN`；
- 只轮询端口，暂不启用中断；
- 报告运行结果以及关键寄存器回读。

## 实机测试

烧录 `openvela-a733-cubie-a7z-sd-usb-v111-candidate.img`，连接 USB 麦克风后执行：

```sh
echo probe > /dev/a733-uvc
cat /dev/a733-uvc
```

请保存从开机到上述两条命令结束的完整串口输出。重点关注：

1. `A733 xHCI v111: running ... cmd=00000001`；
2. `controller` 行中 `start=0`、`sts` 的 bit0 清零；
3. `xhci-regs-v111` 中 `config=8`，CRCR、DCBAAP、ERSTSZ 非异常值，MFINDEX 随时间运行；
4. `ports` 是否从 `000002a0/000002a0` 变为至少一个端口 bit0（CCS）置位；
5. 若失败，保留 `unsupported`、`controller fault` 或 `RUN timeout` 原始行。

## 本地验证

- `test-usb-v111-xhci.py`：验证实际启动函数的运行时结构、寄存器顺序、RUN、边界和 fatal 分支；
- `test-usb-v105-phy.py`：验证 Combo PHY、Host ID/VBUS 和 PHY soft reset 回归；
- 完整 A733 内核构建、链接及镜像内核哈希回读校验。
