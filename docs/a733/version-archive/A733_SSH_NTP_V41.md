# A733 Cubie A7Z SSH 主机密钥与自动校时 v41

## 实板故障结论

v40 已确认 A733 CE TRNG、`/dev/random`、`Ctrl+C`、Wi-Fi 和 DHCP 正常。
Windows 连接 `192.168.31.27:2222` 超时并不是 Windows、防火墙或 Wi-Fi 故障，
而是板端 `sshd` 在打印以下内容后已经退出：

```text
ECDSA, ED25519, DSA, or RSA host key file must be set
```

原因是 `-k /data/ssh_host_ed25519_key` 在命令行解析时立即让 libssh 导入密钥，
但第一次启动时该文件尚不存在。程序随后虽然成功生成 Ed25519 密钥，却没有再次
将它绑定到 `ssh_bind`，因此 `ssh_bind_listen()` 失败，2222 端口没有监听。

HTTPS 的 `BADCERT_FUTURE` 则来自系统实时时钟尚未校准，与 CA 包无关。

## v41 修复

- `-k` 仅记录主机密钥路径；生成或确认文件存在后再导入 libssh。
- 已有 `/data/ssh_host_ed25519_key` 会直接复用，首次启动才生成。
- 只有 `ssh_bind_listen()` 真正成功后才打印 `SSHD listening`。
- Wi-Fi 完成 WPA2 和 DHCP 后自动执行一次 NTP 校时。
- 提供 `ntpcstart`、`ntpcstatus`、`ntpcstop` 供手动重试和诊断。
- NTP 失败不会断开 Wi-Fi，也不会把连接失败误报成校时失败。
- 镜像不包含 Wi-Fi 密码、SSH 密码、通用私钥或 API 密钥。

## 构建与镜像

- 构建脚本：`build-a733-ssh-ntp-v41.sh`
- 构建目录：`quickly-openvela/cmake_out/cubie-a7z_nsh_v41_ssh_ntp`
- 候选镜像：`openvela-a733-cubie-a7z-sd-ssh-ntp-v41-candidate.img`
- 内核 SHA-256：`00b35c9b9e232fe66a05384da59dc31908217885d60cfefab4455d12aa48090e`
- 镜像 SHA-256：`4fe4edfc61031501b757a21e53bde84e066a543d370c1561a84d471a7e881d02`
- CA 包 SHA-256：`0f119da204025da7808273fab42ed8e030cafb5c7ea4e1deda4e75f066f528fb`

完整编译、GPT、ext4、FAT、嵌入内核、CA 包和配置/字符串检查均已通过。

首次实板日志已确认主机密钥成功生成且 `SSHD listening` 出现，说明密钥导入、
socket bind 和 listen 均已通过。该次 Windows 超时发生时，当前启动周期没有执行
Wi-Fi 扫描、关联、WPA2 或 DHCP，因而旧地址 `192.168.31.27` 并未配置到板端。
每次冷启动后必须先执行 `wifi connect` 并用 `ifconfig` 确认本次地址，再启动
`sshd`。PowerShell 用户名写法为 `openvela@地址`，不要写成 `openvela\@地址`。

## 实板验收

先连接 Wi-Fi。密码仍由板端隐藏输入，不写进命令或工程：

```text
wifi connect <SSID> 5
ifconfig
ntpcstatus
curl --max-time 20 -L -o /data/example.html https://example.com/
```

连接完成后应看到 `Network time synchronized: ...Z`。如果 NTP 临时失败，可执行：

```text
ntpcstart
sleep 5
ntpcstatus
```

在板端启动 SSH 服务：

```text
sshd -n -k /data/ssh_host_ed25519_key -u openvela -p 2222 0.0.0.0
```

按提示输入并确认本次设备密码。看到以下行以后保持该前台命令运行：

```text
SSHD listening; host key=/data/ssh_host_ed25519_key (press Ctrl+C to stop)
```

然后在 Windows PowerShell 连接（`@` 前不要加反斜杠）：

```powershell
ssh -p 2222 openvela@192.168.31.27
```

首次连接接受主机指纹，再输入刚才在板端设置的密码。若板端没有出现
`SSHD listening`，不要继续排查 Windows；应先保存板端报错。
