# v105 USB 主机复位修正

实机日志对应 v103：复位前 DWC3 ID 为 `33313130`，HCRST 后 ID 为零，USBCMD 的 RESET 位一直为 1，返回 `-110`。没有进入设备枚举，日志不能确认麦克风本身是否正常。

## 修改

- 将 overlay 中已有的 Combo0 参考时钟与 PIPE 映射初始化同步到实际参与编译的主源码，Type-C 初始化失败时停止。
- 清除 Combo0 的两个 IDDQ 位；先释放 PHY reset，等待 1 ms，再释放 PIPE reset；请求 PLL/工作电源状态后释放 DP link reset，按厂商 BSP 的数字初始化步骤补齐。
- 按 `sun60iw2p1.dtsi` 同时清除 USB2 SUSPHY、ENBLSLPM 和 USB3 SUSPHY，防止复位所需的 PHY 路径挂起。
- 在 PMA-ready 检查前打开 wrapper 时钟并禁止 PHY 挂起；顺序与本地 Linux DWC3 `core.c` 先 `dwc3_phy_setup()` 再 `phy_init()` 一致。
- 等待 CNR 清零后才写操作寄存器，先停机再 HCRST；复位后要求 DWC3 身份有效、控制器 halted 且 CNR 清零，寄存器全零不能被当成成功。
- 每次 probe 清理阶段结果；没有启动未配置传输环的主机，也没有回退操作供电用 USB0。

参考文件：本地 BSP 的 `drivers/phy/sunxi-cadence-combophy.c`、`drivers/usb/host/xhci_sunxi.c` 和 `configs/linux-5.15/sun60iw2p1.dtsi`。未移植完整模拟 PHY 参数校准，仍依赖固件的模拟配置；PMA-ready 未成立时停止复位并明确报错。

完整构建必须复用 `tools/build-a733-wapi.sh --incremental` 的 staging 流程。直接编译恢复后的官方源码会缺少已有 Agent 适配补丁，触发字符串截断编译错误和 `popen/pclose` 链接错误。v105 构建脚本已改为调用原流程，临时补丁构建后自动恢复；没有永久修改 Agent 源码。

## 构建与验证

在 WSL Ubuntu 运行上级 `build-a733-usb-v105.sh`，使用现有 v69 构建目录，输出到 `a733-usb-v105-kernel`。主源码和 overlay 保持一致，原文件分别保存在 `usb-v105-source-backup`。

`test-usb-v105-reset.py` 提取真实驱动的复位函数，在 host C 编译器上执行六种 MMIO 场景：正常复位、身份丢失、HCRST 超时、复位前 CNR 超时、halt 超时、复位后状态全零。它验证控制流程，不验证硬件时序、电气连接或模拟 PHY。

`test-usb-v105-phy.py` 提取真实 PHY 函数，验证 CC1/CC2 方向、两个 IDDQ 位、PHY/PIPE 复位间隔、PMA-ready 超时和 USB2/USB3 挂起配置。两组模拟回归均已通过。

`package-a733-usb-v105.sh` 复制 v104 镜像并只替换 openvela 内核；打包后检查文件系统、GPT 和内核回读一致性。v104 基底保持原样。

打包与校验辅助脚本使用 GNU dd 的字节偏移/长度参数和 1 MiB 块，替代跨 WSL 文件系统的逐 512 字节访问；分区偏移仍为 348127232 字节，长度仍为 121634816 字节。

## 实机复测

候选镜像文件名：`openvela-a733-cubie-a7z-sd-usb-v105-candidate.img`。烧录整卡会覆盖目标卡，先备份卡中数据。麦克风接外部 DP Type-C 口。

```text
echo probe > /dev/a733-uvc
cat /dev/a733-uvc
dmesg
```

开机与手动 probe 应出现 `A733 USB combo v105` 和 `combo-v105`。检查：

1. `combo-v105: checkpoint=0`：PMA-ready 成立，允许尝试 HCRST。
2. `reset=0`、DWC3 ID 有效、USBSTS halted/CNR 清零：控制器复位通过。
3. PORTSC 的 CONNECT 位：根端口检测到连接。

`Combo PHY not ready; HCRST skipped` 表示停在 PHY；仍出现 `HCRST timeout` 或 `register window lost after HCRST` 则复位故障尚未解决，保留完整开机和 probe 日志。分别测试不插设备启动、启动后插入、带设备重启。

本次交付是 USB 主机初始化候选修复，尚无实板通过结论。xHCI 传输环、设备枚举和 UAC 采集仍待实现，不能用本版宣称麦克风录音可用。

## 最终构建记录

最终源码构建、链接通过，内核包含 `A733 USB combo v105` 标识。

- 内核 SHA256：`8179e06bb08f5bfb716135ada3edc2ae915b1a416187f847558874855f45a83f`。
- 主源码与 overlay 驱动 SHA256 相同：`e7232889c88b6c1383bfe742ca18cebb8f203fb79f51edb549e58c8685751058`。
- staging 结束后，三个 Agent 文件与本次备份的原文件 SHA256 分别一致。
- v104 基底 SHA256 独立核验：`1430517e5a674221c169a334d9efdcc956e755c94d8c98ba964402c39fe97725`。
- 镜像大小：2147483648 字节；文件系统、GPT 与内核回读校验通过。
- 最终镜像 SHA256：`f808a9ff6c2ac62a183814d8c54c8d995ffd7452ed1e4c0f66c459db40b4fa6f`；Windows 独立计算结果相同。
