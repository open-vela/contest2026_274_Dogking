# A733 Cubie A7Z SSH 端口清理 v44

## 实板根因

v43 已恢复 Windows 与开发板的双向局域网通信：Windows ARP 表成功学习
`f4:ab:5c:e1:dc:5e`，ping 为 0% 丢包、约 5 ms，TCP 2222 探测成功。
因此 FCU760K、ARP、IPv4 和 TCP 入站路径均已通过。

随后 Ctrl+C 停止 sshd 后，`ps` 中没有 sshd，但再次绑定 2222 返回错误 98
（`EADDRINUSE`）。libssh 已设置 `SO_REUSEADDR`，所以根因不是普通 TIME_WAIT，
而是默认 SIGINT 直接终止 sshd，绕过 `ssh_bind_free()`，使监听 socket 未经正常
清理。

## v44 修复

- sshd 捕获 SIGINT 和 SIGTERM，仅设置异步安全的停止标志。
- 信号中断阻塞的 `accept()` 后，主循环正常退出。
- 恢复原信号处理器并调用 `ssh_bind_free()`、`ssh_finalize()`。
- 清除内存中的 SSH 密码缓冲。
- 正常停止时输出 `SSHD stopped; listening socket released`。
- 保留 v43 标准 ARP Announcement、逐方向 ARP/TCP 诊断及所有既有功能。
- 镜像不保存 Wi-Fi 密码、SSH 密码、私钥或 API 密钥。

## 构建与离线验证

- 构建目录：`quickly-openvela/cmake_out/cubie-a7z_nsh_v44_ssh_cleanup`
- 候选镜像：`openvela-a733-cubie-a7z-sd-lan-ssh-v44-candidate.img`
- 内核 SHA-256：`3d6f01ef996f8ed2d1a18bb5ec48942fd60637832ce81eaad22e8af4d6abef9a`
- 镜像 SHA-256：`326ad5066d26cb4c4aa82f6c6b4399c5778e13286e5de212b4afe11fdb7a6c39`
- CA 包 SHA-256：`0f119da204025da7808273fab42ed8e030cafb5c7ea4e1deda4e75f066f528fb`

全量 WSL 构建通过；GPT、ext4、FAT、内嵌内核和 CA 包检查全部通过。

## 实板验收

重启或烧录 v44，以清除 v43 已遗留的孤立端口。连接 Wi-Fi 后先从开发板主动
联系 Windows，使小米 AP 建立无线客户端转发表：

```text
ping -c 1 192.168.31.228
sshd -n -k /data/ssh_host_ed25519_key -u openvela -p 2222 0.0.0.0
```

不要先运行 `Test-NetConnection`，直接在 Windows 登录：

```powershell
ssh -p 2222 openvela@192.168.31.27
```

退出登录后，在开发板用 Ctrl+C 停止 sshd，应看到：

```text
SSHD stopped; listening socket released
```

随后立即再次运行同一条 sshd 命令；若再次显示 `SSHD listening`，即证明端口
清理和重启均通过。
