# v139 HCI 基础通过记录（2026-10-12 用户测试；2026-10-13 归档）

初次完整启动日志：用户附件 `4bb32ad3-593e-478d-90d1-6f7b1982f4c6/已粘贴的文本.txt`。随后又在同一次启动执行两次 probe。

```text
probes=1 events=3 last=0 stage=hci-query-passed hci-status=00 unsafe=0
probes=2 events=6 last=0 stage=hci-query-passed hci-status=00 unsafe=0
probes=3 events=9 last=0 stage=hci-query-passed hci-status=00 unsafe=0
firmware: mode=5 HCI-port=1 readback=1
command=0/3 event=0/14 polls=3
version=0d revision=000b manufacturer=0b3b
address=f4:ab:5c:e1:c8:62
le-features=000000b6ff1f79ff
```

结论：USB 控制端点命令、periodic interrupt event、DATA toggle 跨请求保持、三条查询已在单次启动下重复验证。此前 UART 端口配置遗漏的修复有效。
不扩大结论：还没有 ACL、连接、GATT 或应用动作的实机验证；未完成冷启动循环和压力测试。

随后 Wi-Fi 扫描、ZBCK-E-5G WPA2 与 DHCP 成功。ping 网关 4 次，3 收 1 丢，RTT 6–13 ms，25% 丢包。网络可用，但不能把这四个包称为“零丢包/稳定性通过”；SSH 未提供本轮日志。

作为基础恢复镜像保留：
`openvela-a733-cubie-a7z-sd-ble-v139-usb-route-candidate.img`，SHA256 `8832681e78f3ba0173cb453aada587666d11477e8aa9a7d3eb6f5ed293eaabde`。
源码 `73cbce8`，构建记录 `f29cbc5`。下一阶段开始前分支 `backup-before-ble-v140` 保留该状态；I2S 仍暂停。
