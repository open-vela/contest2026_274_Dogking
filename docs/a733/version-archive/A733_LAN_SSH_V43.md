# A733 Cubie A7Z 局域网 ARP 与 SSH v43

## 问题结论

v42 实板已经证明 Wi-Fi 关联、WPA2、DHCP、NTP、SSH 主机密钥以及
`ssh_bind_listen()` 均正常。Windows 与开发板连接同一 5 GHz AP/BSSID，
Windows 也有正确的 `192.168.31.0/24` 直连路由，但对开发板地址没有 ARP
表项，并返回“无法访问目标主机”。因此当前 SSH 超时发生在 TCP 和 SSH 协议
之前，核心问题是局域网二层 ARP 可达性。

## v43 修复

- DHCP 成功后的三次地址公告改为 Request / Reply / Request。
- 两次 Request 使用 RFC 5227 风格的 ARP Announcement：发送者 IP 与目标 IP
  均为开发板地址，目标硬件地址为全零。
- 保留一次广播 Reply 作为兼容路径，但不再只依赖容易被 AP 或 Windows
  防欺骗策略忽略的 unsolicited Reply。
- `/dev/a733-wifi` 的 `lan-rx` 新增：
  - `req`：收到的 ARP Request 数；
  - `reply`：收到的 ARP Reply 数；
  - `op`、`sender`、`target`：最近 ARP 报文；
  - `tx-arp`：驱动实际提交到 FCU760K 的 ARP 帧数；
  - 保留 IPv4、TCP、SYN 及最近端点诊断。
- 保留 TCP backlog 8、网络统计、Wi-Fi、NPU、curl/HTTPS、TRNG、Ctrl+C 和
  SSH 服务端等既有功能。
- 镜像不保存 Wi-Fi 密码、SSH 密码、私钥或 API 密钥。

## 构建和离线验收

- 构建脚本：`build-a733-lan-ssh-v43.sh`
- 封装脚本：`package-a733-lan-ssh-v43.sh`
- 构建目录：`quickly-openvela/cmake_out/cubie-a7z_nsh_v43_lan_ssh_arp`
- 候选镜像：`openvela-a733-cubie-a7z-sd-lan-ssh-v43-candidate.img`
- 内核 SHA-256：`c28fddfa79e4859e1c654890f7d639e5720c5b94fac0971ef22e369c55e3cbf0`
- 镜像 SHA-256：`cafb03e793cea20f37cab6e024dfd16225611762e58d8908591fe642308badb8`
- CA 包 SHA-256：`0f119da204025da7808273fab42ed8e030cafb5c7ea4e1deda4e75f066f528fb`

完整 WSL 构建通过；GPT 四分区、ext4、FAT、内嵌内核和 CA 包均通过校验。
最终配置确认启用了 TCP backlog（8）与网络统计，内核中确认包含新 ARP 诊断
和 SSH 监听信息。

## 实板验收步骤

开发板：

```text
wifi connect <SSID> 5
ifconfig
cat /dev/a733-wifi
sshd -n -k /data/ssh_host_ed25519_key -u openvela -p 2222 0.0.0.0
```

保持 sshd 前台运行，在 Windows PowerShell 执行：

```powershell
ping 192.168.31.27
arp -a | findstr 192.168.31.27
Test-NetConnection 192.168.31.27 -Port 2222
ssh -p 2222 openvela@192.168.31.27
```

用户名与地址之间必须是直接的 `@`，不要输入反斜杠。测试完成后在开发板用
Ctrl+C 停止 sshd，并立即执行：

```text
cat /dev/a733-wifi
```

判定方法：

- `req=0`：Windows/AP 的 ARP 广播没有到达开发板，问题在 AP 无线客户端转发
  或 FCU760K 接收路径之前。
- `req>0` 且 `tx-arp` 增加：开发板已收到请求并发出回复；若 Windows 仍无 ARP
  表项，则回复被 AP 或 Windows 丢弃。
- `syn>0` 且最近目的端口为 2222：二层和 TCP 请求已经到达开发板，才转入
  TCP/sshd 处理路径排查。
- 若仍无入站 ARP，可先在开发板执行 `ping -c 4 192.168.31.228`，让开发板
  主动建立对端邻居，再立即重复 Windows 的 ping、ARP 和端口测试。
