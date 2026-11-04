# v144 官方 BLE 生命周期五轮实机通过

日期：2026-10-15。用户在烧录 v144 后回传完整串口记录并确认测试完成。
原始附件逐字节复制为 `20261015-v144-five-cycle-hardware-pass.txt`，SHA-256：
`905267c69e4eab48e2e39b20c453a361c8f93c2baca68fab55bc32e39ec83491`。

## 通过范围

本次启动进入 NSH，`ps` 可见 CPU0..7 IDLE；这不是一次新的逐核 cpucheck 验收。
随后手动启动官方 `kvdbd`（PID 13）、`bluetoothd`（PID 14），创建 bttool 实例成功。
连续五次重新进入 bttool，每次均完成如下闭环：

`pair auto 0 → enable → BLE_ON(2) → scan start → 真实广播 → scan stop → stopped 回调 → idle → disable → 6 → OFF(0) → state=0 → quit → NSH`。

| 轮次 | 命令编号 | ScanResult 条数 | 扫描器 | 结束状态 |
| --- | --- | --- | --- | --- |
| 1 | 1–9 | 212 | `0x4255c7a0` | OFF(0)，quit 返回 NSH |
| 2 | 10–18 | 51 | `0x42553fa0` | OFF(0)，quit 返回 NSH |
| 3 | 19–27 | 20 | `0x42554210` | OFF(0)，quit 返回 NSH |
| 4 | 28–36 | 30 | `0x42554100` | OFF(0)，quit 返回 NSH |
| 5 | 37–45 | 351 | `0x42550c90` | OFF(0)，quit 返回 NSH |

总计 664 条真实 `ScanResult`。条数包含停止请求后的已排队回调，不代表唯一设备数。
每轮均有新的真实扫描结果，并且状态 6 后到达 0；本轮未复现 v143 后续重开无广播、
关闭长期停在 6 的症状。正常 quit 的 `rc=-2` 是工具退出标记，不是蓝牙失败。

因此将 v144 标记为 **boot-passed / BLE scan-and-lifecycle-five-cycle-passed**。
这是限定范围的短循环回归，不命名为整机全功能通过或长时间 stress/soak-passed。
之前未启动服务时的 `create instance error` / status=7 不属于一次有效的扫描测试；
此记录保留明确的服务启动前置步骤，不能在重启后直接从 bttool 开始。

## 尚未覆盖

- Wi-Fi 关联、DHCP、ping，以及 BLE 扫描与 Wi-Fi 同时负载的共存测试。
- 扫描尚在运行时直接 quit，然后重进的专项路径；本次都是先 stop/disable 再 quit。
- 多次冷启动/热重启、长时间循环、内存泄漏和高负载/热稳定性。
- 连接、配对、ACL 数据、GATT 服务发现、Read/Write/Notify、断连重连。
- ESP32-S3 下位机固件和双板智能家居通信。

本附件没有 dmesg/hostdiag，未直接验收每条 BLE-LIFE checkpoint 或控制器 buffer 统计；
不根据这五轮成功排他认定旧问题只有一个根因。

## 恢复点和产物

构建来源标签：`a733-ble-v144-reopen-candidate` → `c8d80df`。
新增实机证据恢复标签：`a733-ble-v144-lifecycle-5cycle-pass`。
本轮仅归档证据和更新文档，不修改驱动、不重新构建、不覆盖现有镜像、不推送远端。

保留镜像：上级 `A733-A7Z-ALL-Files/openvela-a733-cubie-a7z-sd-ble-v144-reopen-candidate.img`。
文件名继续保留 candidate 以维持原始交付引用；五轮通过范围由本文件说明，不重命名成全功能版本。

- 镜像：2,147,483,648 bytes，SHA-256
  `829a1e972129c35a3347575ee5dac219a060cdddec6206fbd410954c44b66716`。
- 内核：2,581,928 bytes，SHA-256
  `643701ecb181ae48bc203d87e0ff36b346947bd8dc9017b19faa7425e71ef956`。
- 原构建配置、工具链、补丁、未剥离 ELF 位置、119 个官方暂存路径恢复结果和
  GPT/ext4/内嵌 hash 校验见 `BLE_V144_REOPEN_CANDIDATE.md`，不覆盖原始候选记录。

下一步先补 Wi-Fi 共存检查，再推进官方 GATT Client 与 openvela ESP32-S3 Peripheral
的服务发现、写入、通知和断线重连；I2S 仍暂停。
