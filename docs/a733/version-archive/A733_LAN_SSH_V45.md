# A733 Cubie A7Z SSH 交互式 NSH v45

## v44 实板结论

Windows OpenSSH 调试日志已证明以下阶段全部成功：TCP 连接、SSH 2.0 协商、
Curve25519 密钥交换、Ed25519 主机密钥、密码认证、session channel、PTY 分配
和 shell request。连接随后输出 `nsh: sh: syntax error` 并正常退出。

根因是上游 Linux 示例服务器通过 `sh -l` 启动登录 shell，而 NuttX NSH 不支持
`-l`，因此认证后的 shell 子进程立即退出。这不是密码、网络或加密问题。

## v45 修复

- 带 PTY 的交互式 SSH 会话改为启动纯 `sh`（NuttX NSH）。
- SSH exec 请求继续使用受支持的 `sh -c <command>`。
- 无 PTY 的 shell request 现在也真正启动 NSH 并连接标准输入输出管道。
- `posix_spawn()` 失败时输出具体错误，不再只表现为客户端静默断开。
- 保留 v44 的 Ctrl+C 优雅关闭和监听端口释放。
- 镜像不保存 Wi-Fi 密码、SSH 密码、私钥或 API 密钥。

## 构建与验证

- 构建目录：`quickly-openvela/cmake_out/cubie-a7z_nsh_v45_ssh_nsh`
- 候选镜像：`openvela-a733-cubie-a7z-sd-lan-ssh-v45-candidate.img`
- 内核 SHA-256：`5009450190e613ded059568e300413e434a9a3c7e43bcc50c747cf517dfa49cc`
- 镜像 SHA-256：`5e4da1ba485c3c2a853fb789b53541696cad09f08e0f3a21db6a5217ebd97685`
- CA SHA-256：`0f119da204025da7808273fab42ed8e030cafb5c7ea4e1deda4e75f066f528fb`

全量 WSL 构建以及 GPT、ext4、FAT、内嵌内核、CA 包检查均通过。

## 实板验收

```text
wifi connect <SSID>
ping -c 1 192.168.31.228
sshd -n -k /data/ssh_host_ed25519_key -u openvela -p 2222 0.0.0.0
```

Windows 直接执行：

```powershell
ssh -p 2222 openvela@192.168.31.27
```

登录后应看到远程 `nsh>`，并测试：

```text
uname -a
free
ifconfig
exit
```

若仍断开，使用 `ssh -vvv` 并同步保留板端出现的
`Unable to start NSH...` 信息。
