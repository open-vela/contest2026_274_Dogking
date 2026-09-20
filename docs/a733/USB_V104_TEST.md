# v104 Combo PHY 参考路径修正

## v103 实机依据

两次探测均为：复位前 `GSNPSID=33313130`，HCRST 超时后 `USBCMD=00000002`、`USBSTS=00000000`、`GSNPSID=00000000`。因此控制器并非一开始不可访问，失败在 HCRST 阶段。日志不足以确认唯一根因。

## 本次改动

参考本地 Linux BSP 的 `sun60iw2p1.dtsi`、`sunxi-cadence-combophy.c`，补入 Combo0 数字参考路径：退出 IDDQ、选择 PLL 数字参考、设置 PIPE clock/RX map、按 CC 方向设置连接方向、释放数字 reset 并请求 PLL/工作电源状态。在 HCRST 前等待 PMA-ready，100 ms 内不就绪则明确失败，不继续 HCRST。

保留固件原来的模拟调谐值，没有复制 Linux GPL 模拟参数配置函数，也没有宣称完成全套 PHY 校准。这个修正依赖固件留下可用的模拟配置；如果 PMA-ready 不成立，还需要进一步适配模拟 PHY，而非跳过检查冒充成功。

每次 probe 清理旧 reset、端口和 CNR 统计，避免先前结果混入新输出。

## 烧录与输出

镜像：上级 `openvela-a733-cubie-a7z-sd-usb-v104-candidate.img`。

先备份 TF 卡中的模型和私人配置；整卡烧录会覆盖这些数据。使用已在 Linux 验证的麦克风和转接头，接 DP Type-C 口。

```text
echo probe > /dev/a733-uvc
cat /dev/a733-uvc
dmesg
```

请保留完整开机串口输出。关键字段：

- `A733 USB combo v104` 的 ctrl、status、maps 和 ready。
- `combo-v104: checkpoint=0` 表示 PMA-ready 检查通过，不等于录音可用。
- `Combo PHY not ready; HCRST skipped` 表示问题停在 PHY，就不应期待 HCRST 日志。
- PMA-ready 后仍 HCRST 超时，说明参考路径修正没有完全解决复位问题，需继续核对模拟参数和控制器复位需求。

## 未完成项

xHCI 传输环/设备枚举、UAC 音频类采集、openvela Media 音频路由、UART4 驱动和实际 TTS 输出仍未完成。本版不提供麦克风录音命令，不应称为语音全链路完成版。

源代码恢复包：上级 `a733-usb-v104-source.bundle`；构建前恢复包 `a733-usb-v104-before-build.bundle`。官方源码仅临时 staging 并恢复。

## 构建记录

构建通过；内核 SHA256：`af47e949079e0b51c77165d05f1840a0f7e50fff60a06d623eb80ad9bc5f5a2e`。内核中已核验 `A733 USB combo v104` 字符串。

打包与 GPT、文件系统、内核回读验证通过；镜像 2147483648 字节。最终 SHA256：`1430517e5a674221c169a334d9efdcc956e755c94d8c98ba964402c39fe97725`，已用 Windows 再次独立核验。

打包基底为校验一致的 v103 镜像（v102 文件已不在目录中），基底 SHA256：`3e0f0700bed8015b9d4c6b29ca343e0f28f688d1fa4afabb75173e399ffff3c2`。基底未修改。官方 USB 源文件构建后 SHA256 恢复为 `53917e9e4bdc74b0d653838db538c651b4a7ed1e0ea6326bc44921a892f46156`。
