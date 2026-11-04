# v139 USB 蓝牙路由修复候选（2026-10-12）

## 验收状态

修复代码：`73cbce8`。恢复分支 `backup-before-ble-v139` → `85b2a15`。
source-ready / build-passed / image-produced 已完成；boot-passed / hardware-passed / stress-passed 等待用户实测。
v138 原始实机日志已保存在 `20261012-v138-bt-hci-timeout.txt`；原因分析见 `BLE_V138_HARDWARE_FAILURE.md`。
I2S 保持暂停，不涉及其后续调试。

## 修复范围

按 Linux D80 加载器覆盖完整九项 BTMODE，尤其 cpmode=0、HCI-port=mailbox(1)，避免使用模板中的 UART(2)。HCI-port 写入后 read32 回验。保持 Linux D80 默认 mode=5，不随意改成另一种 radio profile。
诊断报告增加 EP0 return/bytes、event return/bytes、polls、periodic 调度和 FRINDEX。这是修复候选，不宣称已经证实唯一根因或 HCI 实机通过。

## 构建与镜像

完整构建 2730 targets；GCC 13.4.0，CMake 3.23.0，Ninja 1.12.1。

```text
build: quickly-openvela/cmake_out/cubie-a7z_nsh_v139_ble_usb_route
.config SHA256: 8f9226ef0f35feb55e57d975fab98691f65a21244502536dc2558ffd30c2951f
nuttx.bin bytes: 2203880
nuttx.bin SHA256: 3a532942c863614d008380bc69884a3ca36a7648b38795ea278e2bfd81c523ea
image: A733-A7Z-ALL-Files/openvela-a733-cubie-a7z-sd-ble-v139-usb-route-candidate.img
image bytes: 2147483648
image SHA256: 8832681e78f3ba0173cb453aada587666d11477e8aa9a7d3eb6f5ed293eaabde
```

构建工具输出 `build completed successfully (06:47)`，生成 ELF、bin、System.map，配置断言及 SHA 输出完成。外层 shell 尾部记录了 `exit: : numeric argument required`（命令封装的变量转义问题，不是编译错误），因此又独立核对磁盘内核 SHA 与新增诊断字符串，并通过封装/验证脚本返回 0 确认实际产物。不得仅凭外层错误掩盖内核校验，亦不把该错误称为内核故障。

由 v138 的副本只替换 partition 3 的 `/boot/openvela/a733/Image`，原文件保留。ext4、GPT、反向内核 SHA 全部通过；partition 4 的 2048-sector 提示继承原布局，GPT 报告 No problems found。
原官方 USB 源码恢复：没有本轮 `bt_patch_port`/include，新增 `.inc` 不留在官方目录。持续源码仅位于本仓库 overlay。
保留 build 中 ELF、map、`.config`、`.ninja_log` 以便解析故障。
启动仍为 U-Boot `booti` → EL1 / 0x40200000；UART 115200 8N1。

## 上板测试

烧录前备份 SD 内模型、凭据和用户数据，核对实际 SD 目标；保留磁盘旧镜像。

```text
cat /dev/a733-bt
echo probe > /dev/a733-bt
cat /dev/a733-bt
dmesg
```

首先确认 firmware 行：

```text
firmware: mode=5 HCI-port=1 readback=1 expected-port=1(USB)
```

三项查询通过应为 `last=0 stage=hci-query-passed hci-status=00 unsafe=0`，并核对有效 version/address/features。正常 command 应为 `0/3`（return=0，3-byte HCI 请求）。重复三次并冷启动复测；随后实际连接 Wi-Fi、ping 网关、SSH 与显示/Agent 回归。

如果仍失败，回传全部报告和 dmesg：phase 可区分 EP0/event；periodic 的 cmd/sts/qtd/frame 帮助判断 host 调度。不要只回传 `last=-110`，不要跳过 HCI gate 接入 GATT。
`echo invalid > /dev/a733-bt` 应拒绝且不增加 probes；`unsafe=1` 时保存报告后重启，不继续探测。

本镜像仍无 BLE scan/connect/GATT 业务命令，不能驱动 ESP 下位机。下一阶段条件是实机 HCI 稳定通过。
