# A733 Wi-Fi v71 稳定基线（2026-09-15）

## 结论

FCU760K 5 GHz 联网和长时间运行回归通过，可以作为后续 AI 桌宠开发的网络恢复点。
本版本补齐 runtime message endpoint 后台排空，避免命令应答、异步通知长期堆积后让
扫描、连接和网络命令逐渐变慢或卡死。

## 真机证据

- 5 GHz SSID `706E` 扫描成功并完成 WPA2、DHCP；
- 获取地址 `10.195.194.22`；
- 网关首次 `ping` 4/4 成功；
- 启动 FTP 并空闲 180 秒后，网关 `ping` 20/20 成功；
- 长稳等待后再次执行全频段扫描，得到 29 个 2.4/5 GHz BSS；
- 驱动状态显示 `profile=HT40/PS-off`；
- message pump 统计显示后台已处理 runtime 消息与异步通知；
- 后续 FTP 服务仍能正常启动。

对同一热点内另一台主机的 ping 失败不能单独判定板端 Wi-Fi 失败；网关稳定、ARP 与
目标主机防火墙/热点客户端隔离需要分别验证。

## 恢复点

- 源码提交：`17b06c9 fix: drain FCU760K runtime message endpoint`
- 候选镜像：`openvela-a733-cubie-a7z-sd-wifi-msg-pump-v71-candidate.img`
- 镜像 SHA-256：`f00fc5a48c48f7958bdf742913c6d045b56906fb54309669ed1b34d71999322f`
- 内核 SHA-256：`b69bca151f9b154ee46c684b416c6cfb69b69bc20300a16122e1e64ca0fa947a`

Wi-Fi 密码没有写入仓库或镜像说明文件。
