# A733 v55：可选 SSH/FTP 自启动与上次 Wi-Fi 自动连接

## 状态与范围

用户已确认 v54 原中文 PDF 上传成功。本版沿用 v54，新增开机服务管理和板端
持久配置，不改 NPU、Wi-Fi 射频/USB 数据通路或分区容量。
“首次 LAN 访问可能仍需板子先 ping 电脑”的独立问题未在本版宣称修复。
自启动不是自动端口映射，也不对公网开放防火墙。

## 使用方法

开机策略总览：

```sh
service status
```

SSH / FTP 默认关闭自启动，即使旧版留下认证文件，也必须明确开启。
Wi-Fi 默认允许记住下一次成功连接的网络；没有有效记录时不连接，也不问密码。

### SSH

若已有 `/data/ssh/sshd.auth` 或 authorized_keys，不必重设密码、不要重生成主机密钥。
首次没有认证配置时，先在串口执行：

```sh
sshd --setup openvela
```

然后选择开关：

```sh
sshd --autostart on
sshd --autostart status
sshd --autostart off
```

等价命令：`service ssh on` / `service ssh off`。
默认服务端口 22。开关仅决定下次启动，关闭不会踢掉当前 SSH 会话。
当前手动启动仍用 `sshd`（前台），运行状态用 `sshd --status`。
开机服务复用原主机密钥，正常重启不应引发主机身份变化。

### FTP

先通过 `ftpd status` 确认状态；若正在运行，执行 `ftpd stop` 并等待 stopped，
再设置密码。设置期间禁止更改正在服务客户端的校验数据。

```sh
ftpd setup
ftpd autostart on
ftpd autostart status
```

setup 两次隐藏输入，口令 8–63 字符；只保存随机盐和 PBKDF2-HMAC-SHA256
120000 次迭代的校验值，不保存 FTP 明文密码。`ftpd setup 2121` 可持久设置其他端口。
setup 不立即启动、不自行开启自启动。`ftpd start` 复用已保存认证，不再问密码；
尚未 setup 时仍兼容临时密码启动。已保存认证损坏时拒绝启动，需重新 setup。

```sh
ftpd start
ftpd status
ftpd stop
ftpd autostart off
```

等价策略命令：`service ftp on` / `service ftp off`。用户仍为 openvela，根目录
`/data/models`，最多 4 客户端，被动模式。FTP 本身不加密，密码/文件会在网络上
明文传输，只应在可信局域网开启，不能拿重要账号的密码复用。

### Wi-Fi

升级后第一次手动连接一次（不把密码放在命令行）：

```sh
wifi autoconnect on
wifi connect "你的SSID"
wifi autoconnect status
```

仅在 WPA2 和 DHCP 都成功后保存这个 SSID、频段偏好及重连密码。
`wifi connect "你的SSID" 5` 记住 5 GHz 偏好；`2` 记住 2.4 GHz；不指定时
开机双频扫描选择匹配 SSID。不保存 BSS 序号，每次重启重新扫描。
错误口令、扫描失败、超时和取消不覆盖上一次成功的记录。

```sh
wifi autoconnect off
wifi reconnect
wifi forget
wifi cancel
```

- off：关闭下次启动自动连接，同时停止后续手动连接自动保存；保留现有记录。
- reconnect：手动使用保存的记录，不询问密码，即使自动连接已关闭也可调用。
- forget：删除保存记录并关闭自动连接，不断开现有连接。
- cancel：请求正在进行的扫描/连接结束；Ctrl+C 也走清理路径。
- 当前仅保存一个“上次成功网络”，不是多个网络配置列表。没有持续后台漫游/
  掉线守护；开机尝试包含最多 3 次扫描，认证/DHCP 等待有限，失败后可手动 reconnect。
- 开机连接与手动扫描/连接串行，忙时明确提示，不同时操作同一 Wi-Fi 控制面。

## 启动顺序与板端记录

SD 和 `/data` 挂载、Wi-Fi 硬件初始化后，板级代码启动 `service --boot` 调度器。
按持久开关分别启动 SSH/FTP 监听器和 Wi-Fi 自动连接任务，彼此不等待，标准输入
输出重定向到 `/dev/null`，启动过程不会读取串口密码。结果通过 dmesg 查看。
监听器可先绑定 0.0.0.0，Wi-Fi 获取 IP 后即可通过该地址连接；这不代表绕过未解决的
LAN 可达性问题，仍需实际从 Windows 测试。

文件位于 `/data/system`：ssh.auto、ftp.auto、wifi.auto、ftp.auth、wifi.profile。
记录包含类型/长度与 CRC，临时文件写完并刷盘后再替换；损坏记录拒绝使用。
CRC 仅用于检测损坏，不提供防篡改认证。FAT/VFS 替换不是断电事务，写入时不要拔电；
突然断电后可能需要重新配置。off 不会停止已运行的服务；重启后按新策略生效。

Wi-Fi 重连密码以设备可读取形式保存在 wifi.profile（非加密）。普通 Wi-Fi 口令
不能只存不可逆哈希便实现重连。该文件不位于 FTP 根目录，不写进源码、文档、日志
或发布镜像。FAT 不可靠地实施 Unix 0600 权限，持有 TF 卡或板端 shell 的人可读取。
forget 是文件删除，不保证闪存介质上的取证级擦除。

SSH 认证文件和主机密钥继续位于 `/data/ssh`，不与 FTP 密码共用。

## 实板验收清单

1. 在新固件连接一次网络，查看 wifi autoconnect status，确认记录匹配。
2. 设置 SSH/FTP 认证并开启两个自启动，正常 reboot。
3. 不在串口输入任何密码；查看 ifconfig、service status、sshd --status、ftpd status。
4. Windows 用实际 DHCP 地址直接 SSH 22、WinSCP FTP 21（或 setup 保存的端口）。
   不以“任务创建成功”代替“端口实际监听”，不先手动 ping 掩盖首次 LAN 问题。
5. 两个 SSH 客户端、两个 FTP 客户端并行，传输原中文 PDF，确认下载 SHA256 一致。
6. 分别关闭 ssh/ftp 策略重启，确认相应服务不启动；再开启并重启确认恢复。
7. 关闭 Wi-Fi 策略重启，确认未自动关联；reconnect 应仍可用。forget 后重启不连接。
8. 尝试错误 Wi-Fi 密码，确认不会覆盖上次成功配置；测试 Ctrl+C/cancel 后仍可再连接。
9. 若路由器不在场，NSH 仍可输入，自动连接失败有日志，不无限阻塞开机。

## 构建、回归与交付

使用现有 WSL/预置工具链，不重新安装 openvela 环境。
`build-a733-services-v55.sh`；增量参数 `--incremental`。
`package-a733-services-v55.sh` 只复制新的常规镜像文件并替换内核，不操作物理卡。
镜像沿用 2 GiB 数据布局，不包含任何实际连接凭据。

`test-a733-services-v55.py`：实际配置文件读写、真实 Mbed TLS PBKDF2 与 Python
向量比较、缺失/损坏拒绝、并发写、8 种策略组合；Wi-Fi 连接流程以模拟传输验证
成功才保存/后台不交互/失败保留/取消；实际 FTP 回调认证与 64 MiB/中文/4 并发回归。
这些不等于真实板端重启、FAT 断电、无线连接稳定性或 Windows 兼容性验收。
其他回归脚本 test-a733-ftp-v53.py、test-a733-ftp-v54-path.py、test-a733-download-v53.py
继续保留。源码、有效配置和最终哈希存档到 archives/a733-services-v55-candidate。

整卡重刷会清除卡上用户上传文件、密钥和自动连接记录，请先备份；能按现有流程仅
替换启动分区 Image 时可保留 `/data`。脚本不会读取或打包实板的秘密文件。

### 最终产物校验（2026-09-03）

完整 1901 步构建和最终增量构建通过，上述 v55 主机回归通过。实板待验证。

- 镜像：`openvela-a733-cubie-a7z-sd-services-v55-candidate.img`，2147483648 字节。
- 镜像 SHA256：`a56c825c7c7c32efe5475b1c988c014e1e41acef2e88f42e6243b8baf0cb1577`。
- 内核：1572776 字节，SHA256：`51fcc9cd790e33d2248343d79183f82aa8b23e75317edecc8e9e48c4b790ef88`。
- GPT 与 ext4 只读检查通过；内核与构建产物一致，启动分区以外与 v54 逐字节一致。
- GPT 保留原第 4 分区末尾非 2048 扇区对齐提示；没有新增分区布局变化。
- 打包挂载已卸载，回环设备已释放；未操作物理 TF 卡。
