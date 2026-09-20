# A733 Cubie A7Z openvela v40：Ctrl+C、硬件随机数、HTTPS 与 SSH 修复候选

更新时间：2026-09-01

## 结论

v40 已完成全量构建、512 MiB SD 镜像封装和离线完整性验收。它修复 v39 实板日志确认的两个共同根因：

- `/dev/a7zconsole` 虽已具有 RX 线程和 `TIOCSCTTY`，但未实现 `TCGETS`，因此 `isatty()` 仍返回假，NSH 没有把前台命令设置成控制终端前台进程。v40 为轮询控制台补齐最小 termios 语义以及 `TCGETS`、`TCSETS`、`TCSETSW`、`TCSETSF`，使 NSH 能完成前台进程绑定，UART RX 线程收到字符 `0x03` 时可向该进程发送 `SIGINT`。
- curl 的 `CTR_DRBG - The entropy source failed` 和 sshd 无法生成 Ed25519 主机密钥均来自系统没有安全随机源。v40 依据 A733 官方 CE v5 驱动的寄存器、任务描述符和 TRNG 命令格式，实现 CE 硬件 TRNG，启动时执行连续性/均匀值自检；只有自检通过才注册 `/dev/random` 和 `/dev/urandom`。

Wi-Fi、NPU、SD、curl 同步 DNS、mbedTLS pthread 和 CA 包保持 v39/v50 已有实现。工程、镜像和文档不保存 Wi-Fi 密码、SSH 密码、外部 AI API 密钥。

## 候选镜像

文件：

`openvela-a733-cubie-a7z-sd-console-entropy-curl-ssh-v40-candidate.img`

大小：536870912 字节

镜像 SHA-256：

`495926c84f798e4edc6d07d9b974951ece4557cd784002d4fae1e47f2cacb1a6`

内核大小：1522568 字节

内核 SHA-256：

`f3c9345cc8e750e1117eb652c1c334e2f97c1530f1ff00c31cb9a7b82dacc596`

CA 包 SHA-256：

`0f119da204025da7808273fab42ed8e030cafb5c7ea4e1deda4e75f066f528fb`

离线验收已通过：全量编译和链接、TRNG/控制台符号进入最终内核、GPT 分区表、ext4、FAT、嵌入内核哈希和嵌入 CA 包哈希。v40 在实板测试完成前仍为候选，不覆盖 v39、v38 或当前 NPU 稳定镜像。

## 一次性实板验收

烧写 v40 候选镜像并启动后，依次执行以下测试。Wi-Fi 密码继续只在板端交互输入，不写进命令行、脚本或项目。

### 1. 启动与硬件随机数

```text
dmesg
ls /dev
hexdump /dev/random count=64
hexdump /dev/urandom count=64
```

必须看到 `/dev/random`、`/dev/urandom`，以及类似下列日志：

```text
A733 TRNG: CE v... startup/continuous test passed; /dev/random and /dev/urandom ready
```

两次 `hexdump` 应立即返回且数据不同。若 TRNG 自检失败，驱动会拒绝注册随机设备；不要用固定数值或软件伪随机替代它继续测试 TLS/SSH。

### 2. Wi-Fi

```text
wifi connect <SSID> 5
ifconfig
ping -c 4 223.5.5.5
```

`ifconfig` 应显示有效 IPv4 地址，基础 ping 应正常。

### 3. Ctrl+C

```text
ping -c 30 223.5.5.5
```

收到 3 至 5 个回应后按一次 `Ctrl+C`。期望立即返回 `nsh>`，而不是继续到 30 个包。随后执行：

```text
ps
ifconfig
```

确认没有遗留 ping 任务且网络仍在线。后续也可用同样方式中止长时间 curl 或前台 sshd。

### 4. curl、DNS 与 HTTPS

```text
curl --version
curl --max-time 20 -L -o /data/example.html https://example.com/
ls -l /data/example.html
```

预期不再出现 `mbedtls_ctr_drbg_seed ... entropy source failed`。若系统默认 CA 路径未被 curl 识别，再显式验证镜像内 CA 包：

```text
curl --cacert /data/curl/ca-certificates.crt --max-time 20 -L -o /data/example.html https://example.com/
```

### 5. SSH 服务端

确认 Wi-Fi 已获得 IPv4 地址后执行：

```text
sshd -n -k /data/ssh_host_ed25519_key -u openvela -p 2222 0.0.0.0
```

首次启动应能利用 `/dev/random` 生成主机密钥，不再报告 `Failed to generate host key`。密码只在板端按提示临时设置。Windows 端另开终端连接：

```text
ssh -p 2222 openvela@<开发板IP>
```

完成登录、命令执行和退出后，在串口用 `Ctrl+C` 结束前台 sshd。

## 失败信息采集

若任一测试失败，请保留以下完整输出，不要只截取最后一行：

```text
dmesg
ls /dev
ps
ifconfig
cat /dev/a733-wifi
```

此外保留失败命令从开始到返回提示符的全部串口文本。TRNG 失败时重点保留 CE 版本、ISR 和错误码；HTTPS 失败时保留 curl 的完整错误；SSH 失败时保留主机密钥文件是否生成以及 sshd 的完整输出。

## 回退

- v40 实板失败时可直接烧回 v39 候选镜像进行对比；v40 没有覆盖旧镜像。
- 在 v40 的 TRNG、Ctrl+C、HTTPS 和 SSH 全部通过前，不将其提升为稳定基线。
- 本阶段没有改动已冻结的 FCU760K Wi-Fi 与 NPU v50 推理实现。
