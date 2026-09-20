# A733 Cubie A7Z openvela v39：控制台、curl/HTTPS、SSH 与 Wi-Fi 重试修复候选

更新时间：2026-09-01

## 结果

v39 已完成构建和 512 MiB SD 候选镜像封装。它针对 v38 实板日志中的四个独立问题进行修复：

- `Ctrl+C`：`/dev/a7zconsole` 原先是裸轮询字符设备。NSH 等待前台任务时无人读取 UART，字符 `0x03` 因而无法转换成 `SIGINT`。v39 增加永久 UART RX 线程、256 字节接收环形缓冲、阻塞/非阻塞读取以及 `TIOCSCTTY`/`TIOCNOTTY`，将 `Ctrl+C` 直接发送给当前前台任务。
- curl：NuttX 下关闭 curl 的 POSIX 异步 DNS 线程，改用同步解析，避免 `getaddrinfo() thread failed to start`。HTTPS、mbedTLS 和 CA 根证书包保持启用。
- SSH：启用 mbedTLS pthread 线程后端，修复 libssh 初始化阶段 1 的线程初始化失败。
- Wi-Fi：`wifi connect` 找不到指定 SSID 时自动重新扫描，最多三轮、轮间 300 ms，降低首次漏扫直接返回 `ENOENT` 的概率。

工程、镜像和文档均不保存 Wi-Fi 密码、SSH 密码或外部 AI API 密钥。

## 候选镜像

文件：

`openvela-a733-cubie-a7z-sd-console-curl-ssh-v39-candidate.img`

大小：536870912 字节

镜像 SHA-256：

`3488d590f1a1ae8383af9691bcc6dcf1d2c3ac9f5734f28638bdc2a83720bf6f`

内核大小：1518472 字节

内核 SHA-256：

`a0737cfd3cc98b4dc08074c77a2b21deba1b46c7921cd2586a5772c8312e276c`

CA 包 SHA-256：

`0f119da204025da7808273fab42ed8e030cafb5c7ea4e1deda4e75f066f528fb`

GPT、ext4、FAT、嵌入内核及嵌入 CA 包均已离线校验通过。v39 在实板验收完成前仍是候选版本，v38 和 v50 稳定镜像均未覆盖。

## 实板验收

### 1. Wi-Fi 与基础状态

```text
wifi connect <SSID> 5
ifconfig
ps
```

输入密码时由板端交互读取，不要将密码放入命令、源码或脚本。若第一轮漏扫，屏幕应出现重试提示并自动继续；不应立即以 `Unknown error 2` 退出。

`ps` 中应看到名为 `a7z-uart-rx` 的内核线程。

### 2. Ctrl+C

```text
ping 223.5.5.5
```

在持续 ping 期间按一次 `Ctrl+C`。期望立即返回 `nsh>`，系统不重启。随后执行：

```text
ps
ifconfig
```

确认没有遗留 ping 任务且 Wi-Fi 仍在线。再分别对长时间 curl 和前台 sshd 做一次相同测试。

### 3. curl、DNS 与 HTTPS

```text
curl --version
curl --max-time 20 -L -o /data/example.html https://example.com/
ls -l /data/example.html
```

`curl --version` 不应再列出 `AsynchDNS`；HTTPS 不应再报告解析线程无法启动。若自动 CA 路径不生效，可显式验证：

```text
curl --cacert /data/curl/ca-certificates.crt --max-time 20 -L -o /data/example.html https://example.com/
```

首次 ping 丢一个包通常是 ARP 邻居解析，不代表 Wi-Fi 驱动失败；持续丢包才需要保留 `ifconfig`、`cat /dev/a733-wifi` 和 `dmesg` 继续分析。

### 4. SSH 服务端

确认 `ifconfig` 已获得 IPv4 地址后执行：

```text
sshd -n -k /data/ssh_host_ed25519_key -u openvela -p 2222 0.0.0.0
```

预期不再出现 `libssh: initialization stage 1 failed`。首次启动按提示在板端设置运行时密码；密码不回显，也不写入工程。Windows 端连接：

```text
ssh -p 2222 openvela@<开发板IP>
```

完成后在串口按 `Ctrl+C` 停止前台 sshd。

## 回退

- v39 实板未通过：回退 v38 以对比 curl/SSH 行为，或回退 `openvela-a733-cubie-a7z-sd-minimal.img` 获得当前 v50/NPU 稳定基线。
- 不要删除 v38，直到 v39 的 Ctrl+C、HTTPS 和 SSH 服务端全部通过。
- v39 的修复不改动已冻结的 FCU760K 和 NPU v50 实现。
