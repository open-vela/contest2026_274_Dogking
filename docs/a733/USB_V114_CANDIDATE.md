# A733 外部 USB v114 候选状态

v113 是最后一个取得实机日志的外部 Type-C/USB 诊断版本。该日志仍未看到端口
连接状态，因此没有枚举到 USB 麦克风或摄像头。

v114 在仓库中保留了下一检查点：

- 从本地 Allwinner A733 BSP 提取 Combo PHY USB 模式寄存器表；
- 明确设置 A733 USB2 host/UTMI 路径；
- 保留已证明可读的 DWC3/xHCI 固件交接状态，禁止破坏寄存器窗口的 HCRST；
- 准备对齐的 DCBAA、command ring、event ring、ERST 与 scratchpad；
- 输出 xHCI 启动、端口和运行时寄存器诊断。

代码已经随后续 v115 完整固件构建通过，但尚无 v114 实机日志。它是可恢复的
开发候选，不是“UAC/UVC 已完成”。在获得开发板前，不继续添加传输环、UAC 或
UVC 上层逻辑，以免在未知 PHY/端口状态上叠加故障。

下一次上板先只执行：

```text
cat /dev/a733-uvc
echo probe > /dev/a733-uvc
cat /dev/a733-uvc
dmesg
```

必须分别保存不插设备启动、启动后插入、带设备冷启动三组日志。只有看到稳定的
CCS、有效 VID/PID 和描述符后，才开始 UAC PCM 或 UVC frame streaming。
