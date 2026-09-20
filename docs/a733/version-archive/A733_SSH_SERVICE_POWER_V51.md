# A733 Cubie A7Z：SSH 服务与电源管理 v51

## 本版结果

本版基于已经通过实板 SSH 登录的 v45 镜像，只替换 openvela 内核，保留原镜像
的 SD 启动、数据分区、FCU760K 固件、Wi-Fi、NPU 模型及其他文件。

- `sshd` 无参数启动标准服务：`0.0.0.0:22`。
- `/data/ssh` 持久保存本机 host key、认证哈希和可选 authorized key。
- 开机检测到有效认证配置后自动启动 SSH；启动时不要求 wlan0 已获得地址。
- 服务端 accept 循环为每个连接创建独立线程，支持多个远程客户端并发登录。
- 没有默认账号密码；密码使用随机盐 PBKDF2-HMAC-SHA256 保存。
- 新增 `reboot` 和 `poweroff`，通过官方 BL31/TF-A 的 PSCI SMC 调用实现。

## 镜像

`openvela-a733-cubie-a7z-sd-ssh-service-power-v51-candidate.img`

- 大小：536870912 字节
- SHA-256：`13d8f7a5f56a0e02ff8f1a4ef0c9fa053824830670fb24f8c807ae08540f2a82`
- 内核大小：1539040 字节
- 内核 SHA-256：`1d3633fd2985661cb0891869bf40ce8b24a2a559c6809b113d58c8d30a8e456a`

## 第一次配置（只做一次）

烧录并启动后，在串口执行：

```text
sshd --setup openvela
```

依次输入两遍至少 8 个字符的 SSH 密码。输入不会回显。随后执行：

```text
reboot
```

重启后，`dmesg` 应出现：

```text
A733: SSH service started pid=... on 0.0.0.0:22
```

如果尚未配置认证，系统会安全地拒绝开放无认证服务，并提示执行 setup。

## 联网和远程连接

SSH 服务在开机后已经监听；Wi-Fi 完成 DHCP 后无需再运行 `sshd`。确认 IP：

```text
ifconfig
```

Windows 连接（端口已经改为标准 22）：

```powershell
ssh openvela@192.168.31.27
```

若同一 IP 的主机密钥曾被旧镜像更换过，只删除这一台设备对应的旧记录：

```powershell
ssh-keygen -R 192.168.31.27
```

## 多设备验收

保持第一台电脑的 SSH 会话不退出，同时在第二台电脑或手机 SSH 客户端登录同一
IP。两个终端分别执行 `uptime`、`ps` 和 `ls /data`，两边都应正常响应；关闭
其中一个会话不应影响另一个会话或串口 NSH。

## 电源命令验收

先验证重启：

```text
reboot
```

应重新经过 Boot0/U-Boot/BL31 并回到 NSH。确认认证文件保留、Wi-Fi 联网后 SSH
无需手动启动即可再次连接。最后验证关机：

```text
poweroff
```

TF-A/PMIC 若支持 SYSTEM_OFF，开发板会进入关机状态；若供电设计使 Type-C 保持
上电，CPU 也应停止运行。重新上电前不要在数据分区写文件。

## 公钥认证（可选）

将单个 OpenSSH 公钥保存为 `/data/ssh/authorized_keys`，重启后服务会同时开放
公钥认证。密码认证文件仍可保留作为回退。不要把私钥放入开发板、镜像或项目。

## 回退

若 v51 实板电源或自动启动回归失败，直接烧回已验证的
`openvela-a733-cubie-a7z-sd-lan-ssh-v45-candidate.img`。v51 没有修改 v45 文件。
