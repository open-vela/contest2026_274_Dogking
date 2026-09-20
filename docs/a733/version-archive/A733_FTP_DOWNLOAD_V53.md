# A733 v53：FTP 模型文件传输与 wget / curl 检查

## 状态与边界

这是基于 v52 SSH/LAN 修复代码的候选版。NPU 代码保持不变，本轮不推进模型适配。
已完成代码修复、WSL 交叉构建和本地主机回归；开发板上的实际 Wi-Fi、SD 写入和 HTTPS 测试仍需刷入后验收，不能把主机测试当成板端通过。

镜像：`openvela-a733-cubie-a7z-sd-ftp-v53-candidate.img`，完整 SD 镜像 2 GiB。
SHA256：`e4335eec4f4ed6e3901102a4c123979d6372d88b74c7406a558dee137b6f72ab`。
内核 1563768 字节，SHA256：`53b397ff629455b503ded1d216c1b21ea0d8732a707795cbacb1574f38534ae1`。
原来约 64 MiB 的 FAT 数据分区扩大到约 1.56 GiB。前三个分区的位置不变；保留候选基线镜像中的固件、模型与 CA 文件。
当前内核仍为 32 位有符号文件偏移，单文件必须小于 2 GiB，而且不能超过 `/data` 的实际剩余容量。本版空间通常足够存放数百 MiB 至约 1 GiB 的模型，不代表支持任意大小模型。FAT32 本身的 4 GiB 上限不是本版可用上限。

不会在构建时操作物理 SD 卡。刷写会覆盖卡上个人数据、SSH 主机密钥和配置，请先备份；不要删除 Windows 整个 known_hosts 文件来绕过主机身份核验。
如果只更新已有卡上的 Image，则不会自动扩大数据分区，仍需注意旧的 64 MiB 容量。

## 启动 FTP

先用现有 `wifi connect` 联网，确认 `ifconfig` 得到非零 IP。

```text
ftpd start
ftpd status
```

启动时输入两遍临时 FTP 密码（8–63 字符，输入不回显）；不会写入项目、配置文件或镜像。
FTP 用户名固定为 `openvela`，默认端口 `21`。需要换端口时使用 `ftpd start 2121`。
默认不自启动，重启后需重新开启。SSH 原有自启动方式不变。

FTP 的 `/` 对应开发板 `/data/models`，不能访问 `/data/ssh`、Wi-Fi 配置、固件分区或其他目录。文件传输采用固定大小缓冲，不会把整个模型读进 RAM。
最多 4 个并发客户端；不要让多个客户端同时覆盖同一个文件。

关闭：

```text
ftpd stop
ftpd status
```

停止后不再接受新连接，已开始的传输允许结束；空闲连接约 60 秒超时。显示 `draining` 时不能立即再启动，关闭电脑 FTP 客户端后再检查。
不要用 `kill` 强杀监听任务，正常退出需要等待工作线程释放资源。

**安全：这是普通 FTP，不是 SFTP/FTPS。密码和文件在网络中不加密。只在可信局域网使用临时密码，不要复用路由器、SSH 或其他重要密码；不要对公网映射 FTP 端口。**

## Windows 图形客户端

WinSCP / FileZilla：选择 FTP，普通 FTP（不启用显式 TLS），被动模式，主机为开发板当前 IP，端口 21，用户名 openvela，密码为本次启动设置的临时密码。
传输模式必须选“二进制”，尤其是 `.nb`、`.a7pm`、`.bin`、`.onnx`、`.gguf` 等模型。
目录中 `transfer-check.txt` 可用来先做小文件下载。

本版主动模式不开放，避免 FTP bounce；请不要使用 Windows 内置的旧式 `ftp.exe` 主动模式客户端。

## Windows curl.exe 传输与续传

以下示例中的 IP 和本地文件名按实际替换。`--user openvela` 会提示密码，不把密码放在命令行。

上传（先用 `.part` 文件名，完整校验后再重命名）：

```powershell
curl.exe --fail --show-error --ftp-pasv --user openvela -T .\model.bin ftp://192.168.31.27/model.bin.part
```

网络中断后，从板端已经收到的位置续传：

```powershell
curl.exe --fail --show-error --ftp-pasv --user openvela -C - -T .\model.bin ftp://192.168.31.27/model.bin.part
```

续传的本地文件必须与首次上传完全相同；不要对不同模型继续写同一个 `.part`。
下载回电脑做端到端校验（不依赖板端是否提供 sha256sum）：

```powershell
curl.exe --fail --show-error --ftp-pasv --user openvela -o .\model.roundtrip.bin ftp://192.168.31.27/model.bin.part
Get-FileHash .\model.bin -Algorithm SHA256
Get-FileHash .\model.roundtrip.bin -Algorithm SHA256
```

两者必须相等。之后使用图形客户端把 `model.bin.part` 重命名为 `model.bin`；或者在 NSH 执行：

```text
mv /data/models/model.bin.part /data/models/model.bin
sync
```

掉电/拔卡时不能保证文件系统一致性；上传完成后仍需正常关机。文件占满磁盘时应报告失败，残留 `.part` 不可直接交给 NPU。

## wget：HTTP 下载

NSH 的 wget 不等于 Linux GNU wget。它使用 webclient，本板没有为这个调用接入 TLS；HTTPS 请使用 curl。

```text
wget -o /data/models/wget-test.html http://example.com/
ls -l /data/models/wget-test.html
```

修复后会检查短写、写入中断、零字节写入、HTTP 非 2xx 状态、fsync 和 close 失败。webclient 原有 Content-Length 不匹配检查保留。
失败时可能留下不完整输出文件；不要因为文件存在就认为下载成功。大模型建议使用 curl 的续传功能，并校验哈希。

## curl：HTTP / HTTPS / 外部 API

```text
curl --version
wifi time
date
ls -l /data/curl/ca-certificates.crt
curl -fL --connect-timeout 10 --max-time 30 -o /data/models/curl-http.html http://example.com/
curl -fL --connect-timeout 10 --max-time 30 -o /data/models/curl-https.html https://example.com/
```

CA 包路径固定为 `/data/curl/ca-certificates.crt`；本轮打包检查该文件非空。HTTPS 还需要系统时间正确、DNS 可用、服务器证书链有效；`wifi time` 异步校时，需确认 `date` 合理后再测试。
不要把 `-k/--insecure` 作为解决办法。

HTTP 大文件续传（服务器必须支持 Range）：

```text
curl -fL --connect-timeout 10 --max-time 600 -C - -o /data/models/model.part http://你的可信文件服务器/model.bin
```

外部 AI API 可使用 curl 的 POST、请求头和请求体功能，但需要真实接口地址、授权和有效 TLS。不要把 API Key 存入源码、镜像或测试日志；本轮没有代你调用收费 API。
API 的具体模型名称、接口、鉴权由所用服务决定，本版未预设供应商。

## 错误记录与实现

1. FTP 原先单次 send 未处理短发送：改为循环发送，并处理 EINTR/EAGAIN/超时/零进展。
2. FTP 原先把每次 TCP recv 当成完整命令：改为按行读取，覆盖分包及多命令合包。
3. APPE 错误附加 O_TRUNC：只允许无 REST 的 STOR 截断。
4. REST 偏移未在传输结束后清除，会污染后续下载：改为一次传输消耗；拒绝负数、溢出、非法字符；返回 350。
5. 上传原先可能在实际刷盘前返回 226：现在 fsync/close 成功后才确认；短写、零写和磁盘写满均报错。
6. 保留未完成上传供续传，不自动把失败的新文件删除；使用 `.part` + 校验 + 重命名流程。
7. 原库关闭服务时可能先释放仍被工作线程使用的帐号：新增计数/条件变量，关闭监听后等待会话退出。
8. 限制 4 会话、60 秒无进展超时，被动连接校验来源地址，拒绝符号链接路径，禁用主动数据连接。
9. 不使用自带示例的匿名/root/固定密码；单独提供 ftpd 管理命令，仅内存保存临时密码，释放前擦除副本。
10. wget 原先忽略短写和 HTTP 失败状态：修复后返回错误，不将损坏下载默认为成功。

## 已做与待做的验收

已做（WSL 主机，实际 FTP 库 + POSIX 适配，不是开发板）：空文件、1 字节、4095 字节、16 KiB、1 MiB、64 MiB 上传下载 SHA256；REST 上传/下载；APPE；重命名；非法偏移；目录越界；四并发；错误密码；正常停止等待客户端退出；分包/合包命令。单元故障注入覆盖 wget/FTP 短 I/O、EINTR、零写、ENOSPC、发送超时。
另已通过主机 curl EPSV 上传/下载、符号链接越界拒绝。完整 1898 步及最终增量构建成功；镜像 GPT/ext4/FAT 检查通过、内核一致，原始数据分区所有文件逐个比较一致。测试不会打印用户凭据。

板端必须补验：

- 小文件上传下载；64 MiB 和至少一个数百 MiB 的真实模型往返 SHA256。
- 传输中断后续传，再校验哈希；不要直接拿正在上传的模型执行。
- 4 客户端不同文件并发，观察 `free`，退出后内存恢复。
- `ftpd stop` 后最终 stopped，再启动可用，无端口占用残留。
- wget HTTP、curl HTTP、curl HTTPS 均完成，并检查文件内容；404、错误证书必须失败。
- Wi-Fi 断开重连后重新建立 FTP 会话，原 `.part` 可续传。
- 关机重启后的文件仍正确；SSH 功能不回退。

复现脚本：`build-a733-ftp-v53.sh`、`package-a733-ftp-v53.sh`、`test-a733-ftp-v53.py`、`test-a733-download-v53.py`。
构建继续使用现有 WSL Ubuntu 和项目预置工具链，没有替换 openvela 环境。
