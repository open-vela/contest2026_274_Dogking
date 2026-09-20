# A733 Cubie A7Z openvela v38：Ctrl+C、curl/HTTPS 与 SSH 修复候选

更新时间：2026-09-01

## 本阶段结果

v38 基于当前稳定的 512 MiB SD 镜像制作，没有覆盖稳定镜像。它合并了：

- NSH 控制终端的 `Ctrl+C` / SIGINT 支持，可中止前台 `ping`、`curl`、`sshd` 等任务。
- 完整 curl 命令与 libcurl，启用 HTTP、HTTPS、重定向、请求头、POST、JSON 文件上传、超时等常用能力。
- mbedTLS HTTPS 和镜像内 CA 根证书包，默认位置为 `/data/curl/ca-certificates.crt`。
- libssh 初始化时序修复：NuttX 不再在板级内存、随机数和设备初始化前执行静态构造函数；显式初始化失败时会回滚并允许重试。
- SSH 初始化失败时输出具体的初始化阶段编号，便于继续定位实板差异。
- 保留 `wifi`、`ssh`、`scp`、`sshd`，不在工程和镜像中保存 Wi-Fi 密码、SSH 密码或外部 AI API 密钥。

候选镜像：

`openvela-a733-cubie-a7z-sd-curl-ssh-v38-candidate.img`

镜像 SHA-256：

`827603705ea17508074814ec3e472545b0b813466f77fa6c151161b5be4c5f7e`

内核大小：1518392 字节

内核 SHA-256：

`d00367b242bcaa88eadecd3ddc55b3504bcd2366f5b6cb7c6659fa650c249a70`

CA 包大小：258424 字节

CA 包 SHA-256：

`0f119da204025da7808273fab42ed8e030cafb5c7ea4e1deda4e75f066f528fb`

GPT、ext4、FAT、嵌入内核和嵌入 CA 包均已离线校验通过。v38 在完成下面的实板验收前仍是候选版本。

## 实板验收顺序

### 1. 启动和命令确认

```text
uname -a
help
curl --version
ssh -h
scp -h
sshd -?
ls /data/curl
```

`help` 中应包含 `curl`、`wifi`、`ssh`、`scp` 和 `sshd`。

### 2. Ctrl+C

联网后运行一个持续时间较长的前台命令：

```text
ping 223.5.5.5
```

按 `Ctrl+C`。期望命令退出并重新出现 `nsh>`，系统不重启，Wi-Fi 不掉线。再运行：

```text
ps
ifconfig
```

确认没有遗留的 ping 任务。

### 3. HTTPS 与 curl

```text
curl --max-time 20 -L -o /data/example.html https://example.com/
ls -l /data/example.html
```

若需要显式指定证书包：

```text
curl --cacert /data/curl/ca-certificates.crt --max-time 20 -L -o /data/example.html https://example.com/
```

测试 Ctrl+C 对 curl 的中止：

```text
curl --max-time 120 https://example.com/
```

传输过程中按 `Ctrl+C`，应立即回到 NSH。

### 4. 外部 AI API 备用通路

不要把 API Key 写入源码、构建脚本、镜像模板或聊天记录。运行时可在 `/data/private-api.cfg` 创建私有 curl 配置，并在测试后删除。示例结构如下，其中占位内容由用户在板端替换：

```text
url = "https://API服务地址/v1/chat/completions"
request = "POST"
header = "Content-Type: application/json"
header = "Authorization: Bearer 运行时密钥"
data-binary = "@/data/ai-request.json"
output = "/data/ai-response.json"
connect-timeout = 15
max-time = 120
cacert = "/data/curl/ca-certificates.crt"
```

执行：

```text
curl -K /data/private-api.cfg
cat /data/ai-response.json
rm /data/private-api.cfg
```

工程内不提供任何真实服务地址或密钥。

### 5. SSH 服务端

先确认 Wi-Fi 已获得地址，再启动服务端：

```text
ifconfig
sshd -n -k /data/ssh_host_ed25519_key -u openvela -p 2222 0.0.0.0
```

首次运行会生成板端独有的 ED25519 主机密钥，并在串口安全提示输入登录密码；密码不回显，也不写入命令历史。该命令以前台方式运行。

Windows 端连接：

```text
ssh -p 2222 openvela@开发板IP
```

完成测试后在串口按 `Ctrl+C` 停止 `sshd`。若仍失败，请完整保留 `libssh: initialization stage ... failed (...)`、`ssh_init failed` 以及 `dmesg`；阶段编号将直接指向失败子系统。

### 6. SSH/SCP 客户端

```text
ssh -p 22 用户名@服务器IP
scp /data/example.html 用户名@服务器IP:/目标目录/
```

## 回退与保留规则

- 当前稳定镜像 `openvela-a733-cubie-a7z-sd-minimal.img` 保持不变。
- v37 保留到 v38 实板通过，便于比较 SSH 行为。
- 官方 Debian 镜像保持不变。
- v35/v36 已由 v37/v38 覆盖，可在 v38 离线校验完成后删除其 512 MiB 候选镜像；相应源码存档仍保留。

