# v103 USB 实机诊断镜像

这是可烧写的 USB 初始化诊断版本，不是已完成 UAC 音频采集的版本。

镜像：上级 `openvela-a733-cubie-a7z-sd-usb-v103-candidate.img`。

修正：USB 源文件进入实际构建 staging；控制器复位前等待 CNR；Type-C 初始化失败停止后续操作；移除误操作 USB0 的回退；新增复位前身份和分阶段超时日志。

## 烧录与测试

烧录前备份 TF 卡中的模型、Wi-Fi、Agent 和服务私有配置。烧录会替换整卡内容，镜像不携带私人凭据。不要把 API Key 发到日志或聊天中。

先不插麦克风开机，保存完整串口启动输出，执行：

```text
cat /dev/a733-uvc
dmesg
```

然后将已在 Linux 验证的麦克风和转接头接到 DP Type-C 口，执行：

```text
echo probe > /dev/a733-uvc
cat /dev/a733-uvc
dmesg
```

最后保持麦克风连接，重启再保存完整启动日志和相同命令输出。先不使用 USB Hub，以减少诊断变量；不是认定 Hub 有问题。

## 判读

- 新内核应输出 `A733 USB host: before reset`。出现 `halt timeout`、`pre-reset CNR timeout` 或 `HCRST timeout` 时，保留该行寄存器值。
- Type-C 失败会直接返回其错误，不继续操作 xHCI。
- `A733 USB diagnostic node registered` 只表示诊断入口存在，不表示音频可用。
- 本版尚未实现 xHCI 控制传输环和 UAC，因此不能要求其录音或输出麦克风 VID/PID。
- 仍须后续完成 Combo PHY 初始化、主机传输、USB 音频类采集、openvela Media 路由，再联调官方 ASR 与 UART4 TTS。
