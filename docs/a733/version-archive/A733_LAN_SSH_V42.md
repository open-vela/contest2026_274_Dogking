# A733 Cubie A7Z 局域网入站与 SSH v42

## 根因证据

v41 已在实板确认以下板端阶段全部成功：Wi-Fi 关联、WPA2、DHCP、NTP、
SSH 主机密钥导入，以及 `ssh_bind_listen()`。但同一时刻 Windows 主机
`192.168.31.228/24` 无法 ping 通开发板 `192.168.31.27`，ARP 表中也没有
开发板 MAC。因而 SSH 超时发生在 SSH 协议之前：Windows 连 TCP SYN 都无法
发给开发板。

## v42 合并修复

- DHCP 设置地址后发送三次 gratuitous ARP，主动刷新 AP 转发表和同网段主机
  的 ARP 缓存。
- `/dev/a733-wifi` 新增 `garp=成功次数/状态`。
- `/dev/a733-wifi` 新增 `lan-rx`：累计收到的 ARP、IPv4、TCP、TCP SYN，及
  最近 IP 地址、TCP 源/目的端口。
- 启用 NuttX TCP backlog，允许 8 个待接收连接，避免 sshd 处理连接期间丢掉
  后续连接。
- 启用网络统计，便于通过 `/proc/net` 继续诊断。
- 保留 v41 的 SSH 主机密钥修复、NTP、curl/HTTPS、Ctrl+C、TRNG、Wi-Fi、
  NPU 与其他已完成内容。
- 镜像不含 Wi-Fi 密码、SSH 密码、私钥或 API 密钥。

## 构建与离线验证

- 构建脚本：`build-a733-lan-ssh-v42.sh`
- 封装脚本：`package-a733-lan-ssh-v42.sh`
- 构建目录：`quickly-openvela/cmake_out/cubie-a7z_nsh_v42_lan_ssh`
- 候选镜像：`openvela-a733-cubie-a7z-sd-lan-ssh-v42-candidate.img`
- 内核 SHA-256：`5f36bae3f0d91b180e47206b78606acda2513e2ec3f62530c8f6136584536f94`
- 镜像 SHA-256：`1302e3ac468bdca7373d7030f65c83670e464b619baff4cb9e785324187018c2`
- CA 包 SHA-256：`0f119da204025da7808273fab42ed8e030cafb5c7ea4e1deda4e75f066f528fb`

完整构建、GPT、ext4、FAT、内嵌内核、CA 包、配置及关键字符串检查均通过。
`.config` 已确认 `CONFIG_NET_TCPBACKLOG=y`、连接数 8、网络统计开启。

## 实板验收

烧录 v42 后在开发板执行：

```text
wifi connect <SSID> 5
ifconfig
cat /dev/a733-wifi
```

确认 DHCP 行含有效地址，并且 `garp=3/0`。然后启动 sshd：

```text
sshd -n -k /data/ssh_host_ed25519_key -u openvela -p 2222 0.0.0.0
```

设置本次密码并保持该前台命令运行。Windows PowerShell 依次执行：

```powershell
ping 192.168.31.27
arp -a | findstr 192.168.31.27
Test-NetConnection 192.168.31.27 -Port 2222
ssh -p 2222 openvela@192.168.31.27
```

用户名与地址之间直接写 `@`，不要写 `\@`。如果仍超时，先用 Ctrl+C 停止
sshd，再执行 `cat /dev/a733-wifi`：

- `arp=0`：Windows/AP 的广播仍未到达驱动；问题位于 AP 隔离、无线转发或
  FCU760K RX 路径。
- `arp>0` 且 `syn=0`：ARP 已通，但 Windows 没有发来 TCP SYN，应查 Windows
  路由或防火墙。
- `syn>0` 且最近目的端口为 `2222`：请求已抵达板端，应继续检查 TCP/sshd，
  不再误查 Wi-Fi 扫描或密码。

