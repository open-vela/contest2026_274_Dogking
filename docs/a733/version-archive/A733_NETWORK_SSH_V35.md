# A733 Cubie A7Z openvela 联网与 SSH 候选版（v35）

日期：2026-08-31

## 状态与边界

- NPU v50 已冻结，稳定镜像与全部 NPU 数据未被覆盖。
- v35 是独立的联网/SSH 候选内核，已完成源码、配置、ARM64 编译和链接验证。
- 在实板执行本文件的验收命令前，不把 v35 标记为正式稳定版。
- 项目和镜像不保存 Wi-Fi 密码、SSH 密码或用户私钥。

## 已封装组件

- 基础联网：`ifconfig`、`ping`、DHCP、DNS、`nslookup`。
- 传输与诊断：`wget`、`netcat`、`iperf`。
- SSH：基于项目现有的 libssh + mbedTLS，提供 `ssh`、`scp`、`sshd`。
- 远端 shell：`sshd` 通过 NuttX PTY 启动内置 `sh`。
- 安全修正：删除 libssh 示例服务端原有的默认用户名和默认密码。未显式配置
  `-u/-P` 时，口令认证不会成功。
- 稳定性修正：SSH 通道转发缓冲由示例原有的 1 MiB 改为 4 KiB，避免 NuttX
  工作线程发生栈溢出；反汇编确认实际栈帧为 4,128 字节。

没有直接移植 Linux OpenSSH。OpenSSH 依赖完整 POSIX 进程、账户、权限、
PAM 和文件系统语义，体积也更大；项目自带 libssh 已针对 NuttX 构建系统、
PTY、内置命令和 mbedTLS 集成，更适合当前 openvela 环境。

## 产物

- 内核：`openvela-a733-stage1/Image-netssh-v35-candidate`
- 可烧写候选镜像：
  `openvela-a733-cubie-a7z-sd-netssh-v35-candidate.img`
- 构建目录：
  `../quickly-openvela/cmake_out/cubie-a7z_nsh_v35_netssh2`
- 原稳定镜像仍为 `openvela-a733-cubie-a7z-sd-minimal.img`。

最终 SHA-256 记录在
`archives/a733-network-ssh-v35-candidate/MANIFEST.md`。

## 首次实板验收

烧录候选镜像并按现有方式连接 Wi-Fi。不要把无线密码写进脚本或项目。

```text
uname -a
ifconfig
ping -c 4 192.168.31.1
nslookup baidu.com
wget -o /data/network-test.html http://example.com/
ls -l /data/network-test.html
netcat -h
iperf
ssh -h
scp -h
sshd -?
free
dmesg
```

`netcat -h`、`ssh -h` 等帮助参数若返回用法信息和非零状态，属于正常帮助行为。
若目标网络拦截明文 HTTP，可把 `wget` 地址改为局域网内的 HTTP 文件服务器。

## 推荐：公钥方式启动 SSH 服务

在可信电脑生成两类密钥：一份服务器主机密钥、一份客户端用户密钥。不要把
私钥提交进项目。将服务器主机私钥与客户端公钥复制到数据分区：

```text
ssh-keygen -t rsa -b 3072 -f ssh_host_rsa_key -N ''
ssh-keygen -t rsa -b 3072 -f a7z_client_key -N ''
```

```text
/data/ssh/ssh_host_rsa_key
/data/ssh/authorized_keys
```

当前轻量服务端按单个公钥文件读取 `authorized_keys`，首轮验收只放一把 OpenSSH
RSA 公钥。然后在开发板启动：

```text
sshd -p 22 -k /data/ssh/ssh_host_rsa_key \
  -a /data/ssh/authorized_keys 0.0.0.0 &
```

Windows 或 Linux 客户端连接：

```text
ssh -p 22 <用户名>@<A7Z-IP>
scp -P 22 <本地文件> <用户名>@<A7Z-IP>:/data/
```

公钥模式由 `authorized_keys` 决定是否允许登录；当前轻量服务端不实现 Linux
的 `/etc/passwd`、PAM 或多用户权限隔离，因此只应在可信局域网中使用。

## 临时口令方式

仅用于封闭网络测试：

```text
sshd -p 22 -k /data/ssh/ssh_host_rsa_key \
  -u <用户名> -P <临时密码> 0.0.0.0 &
```

口令会出现在命令行和任务参数中，不建议长期使用，也不得写入项目文档、启动
脚本或固件。正式使用应切换回公钥认证。

## 客户端测试

A7Z 主动连接另一台 SSH 服务器：

```text
ssh <用户名>@<服务器IP>
scp <用户名>@<服务器IP>:/path/file /data/file
```

## 通过标准

1. 启动、Wi-Fi、DHCP、DNS 和 ping 保持 v33.1 的稳定性。
2. `wget` 可把文件完整写入 `/data`。
3. `sshd` 启动后电脑可完成密钥认证并进入 NSH shell。
4. 双向 `scp` 文件哈希一致。
5. 退出 SSH 后串口 NSH、Wi-Fi 和 NPU v50 基线仍正常。
6. 连续重启三次，服务端均可重新启动，无崩溃、泄漏或默认密码登录。

完成以上实板证据后，才能把候选版升级为 v35 正式稳定存档。
