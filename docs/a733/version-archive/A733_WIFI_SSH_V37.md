# A733 / Cubie A7Z Wi-Fi 稳定性与 SSH v37 候选版

## 修复内容

v36 实板已证明双频扫描、WPA2、DHCP、DNS、HTTP 下载均可工作。后续长 ping
出现间歇丢包，日志同时显示 `ME profile=HT40/PS`。原因有三部分：

1. 数据态 USB RX 每次 2 ms 轮询后休眠 250 us，存在约 11% 的接收空窗；
2. 固件 station 省电和 U-APSD voice queue 已启用，但主机尚无完整 DTIM/U-APSD
   唤醒队列，可能造成强信号下仍出现延迟和漏包；
3. DHCP 下发的 DNS 是路由器自身地址，路由器 DNS 代理响应较慢。

v37 将数据 RX 改为 3 ms 轮询、25 us 空闲间隔，错误退避从 10 ms 降为 1 ms；
关闭 station PS 和 U-APSD 队列，优先保证 DNS、SSH 和持续连接稳定；新增
`wifi dns <IPv4>`；断开连接时清除 IP、掩码、默认路由和 DNS，不再留下看似有
地址但不可发送的接口状态。

SSH 方面修复了 `ssh -h` 被当作主机名、`scp -h`、`sshd -?` 帮助，以及 NuttX
内置程序共享地址空间时一次 `ssh_finalize()` 导致下一次 ssh/scp/sshd 无法重新
初始化的问题。服务端还增加：

- 显式主机密钥不存在时，在板上生成唯一 ED25519 私钥并设为 0600；
- 只给用户名时安全交互读取并二次确认密码，不进入 NSH 历史；
- 没有公钥或用户名/密码认证时拒绝启动；
- 不提供默认账号、默认密码或通用主机私钥。

## Wi-Fi 验收

```text
wifi connect Xiaomi_47C7_5G 5
wifi dns 223.5.5.5
ping -c 4 192.168.31.1
ping -c 30 192.168.31.1
ping -c 30 223.5.5.5
nslookup baidu.com
```

首次 ping 的第一个包可能用于 ARP 建邻，应重点看随后 30 包测试。完成后检查断开
是否清理地址：

```text
wifi disconnect
ifconfig
```

此时 `wlan0` 应不再保留原来的 IPv4 地址、默认路由和 DNS。

## SSH 帮助与重复初始化验收

连续执行以下命令，三者都应立即显示帮助，不能解析主机名，也不能出现
`ssh_init failed`：

```text
ssh -h
scp -h
sshd -?
ssh -h
sshd -?
```

## 从 Windows 登录 A7Z

先确保 Wi-Fi 已连接。在 A7Z 串口前台启动服务端：

```text
sshd -n -k /data/ssh_host_ed25519_key -u openvela -p 2222 0.0.0.0
```

第一次启动会生成 `/data/ssh_host_ed25519_key`，然后无回显地要求输入并确认 SSH
密码。该命令以前台方式运行，便于观察首轮测试日志；按 Ctrl+C 可停止。

Windows PowerShell 执行：

```powershell
ssh -p 2222 openvela@192.168.31.27
```

首次连接确认主机指纹，随后输入刚才在板上设置的密码。进入远程 shell 后测试：

```text
uname -a
free
ls /dev
```

不要用 `-P 密码` 启动服务端，因为密码会进入 NSH 历史；该选项只为兼容原示例
保留。正式部署应改用 `-a /data/authorized_key` 公钥认证。

## 产物

- 镜像：`openvela-a733-cubie-a7z-sd-wifi-ssh-v37-candidate.img`
- 内核：`openvela-a733-stage1/Image-wifi-ssh-v37-candidate`
- 内核大小：1,037,152 字节
- 内核 SHA-256：`320985ea701c4e858e89680db82e61d0ad0a74d0fd487cda1d1f3e87430e2f49`
- 镜像 SHA-256：`6f586f73a090947141941bced3906e86c1d860f95403cb682fc04cc0e9468611`

镜像已通过 GPT、ext4、FAT 和嵌入内核哈希校验。v37 仍需上述实板网络与 SSH
验收，v50 正式稳定镜像未被覆盖。

