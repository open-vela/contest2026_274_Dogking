# A733 SSH / LAN / NTP v52：错误记录与验收

日期：2026-09-03。针对 v51 实板反馈；NPU 阶段继续搁置，不修改 NPU、模型和 RV1103 实现。

## 状态边界

v51 已实测重启成功，但 SSH 自动启动失败。v52 修复以下源码缺陷；主机测试不能代替真实开发板的启动、无线收发、SSH 多客户端和公网校时验收。不能将“创建任务”“USB 传输提交成功”“程序无报错”写成端到端通过。

## 问题 1：首次设置密码要求输入四遍，提示还有乱码

位置：`apps/external/libssh/libssh/examples/ssh_server.c` 的 `setup_auth_file()`。

- 原因：显式调用“新密码”“确认密码”两次，同时每次 `ssh_getpass(..., verify=1)` 又内部确认一次，合计四次。
- 两个栈缓冲区未初始化。libssh 的 `ssh_gets()` 将非空缓冲区当作已有默认值，以 `%s` 显示，产生乱码并可能读取越界内存。不是串口波特率问题。
- 修复：缓冲区清零，两次调用均 `verify=0`，保持关闭回显。空/过长用户名、密码不一致、短密码和输入失败拒绝保存。认证文件先刷新再替换；保留随机盐与 PBKDF2-HMAC-SHA256，不保存明文。
- 首次配置仍要求两遍；后续开机不得再向串口索取 SSH 密码。
- `/data` 使用 FAT，不能假定 chmod 等同于 Linux ext4 的访问隔离。不要向不可信用户开放整卡或将板上私钥、认证文件打入公共镜像。

## 问题 2：必须互相 ping 后才可访问

已知证据：Windows 首次收到本机的“目标主机不可达”，后来 ARP/TCP SYN 到达板端，ping 恢复。此时的“0% 丢包”仅表示收到了错误报文，不是 ICMP Echo 成功。

代码审计发现一个确定的并发缺陷：`g_wifi_data_tx` 同时供 DHCP 线程的地址公告、网络栈发送以及 EAPOL 使用。旧 USB 锁只保护进入 EHCI 后的传输，不能保护锁前的 `memset`、描述符及载荷构造；一个发送者等待/执行 USB 传输时，另一个可能改写同一 DMA 缓冲区。

v52 修复：

1. 新增专用 data-TX mutex，保护构造、USB 提交及结束状态更新。锁顺序为 network（若持有）→ data-TX → USB，不在持有 data-TX 后获取 network。
2. DHCP 后保留三次地址公告，但改为标准 ARP announcement request，间隔 1 秒，避免集中在 60 ms 内；连接/授权丢失时停止发送。RX 线程继续独立工作。
3. 保留已经验证的 station PS=0 和 USB 轮询策略，不把恢复省电作为此次修复的一部分。

边界：这是确定的代码风险修复与邻居发现加固，不是已经证明它就是本次首次 ping 失败的唯一原因。`garp=3/0` 只表示发送函数成功，不证明 Windows 已收到。不得通过脚本要求用户先 ping，也不硬编码用户电脑 IP、静态 ARP 或关闭 Windows 防火墙。

## 问题 3：Wi-Fi 成功后 NTP 超时

- 板端 WPA2/DHCP 均已成功，警告来自额外校时步骤。
- 本地 NTP 实现用 `netlib_check_ipconnectivity(NULL,1,1)` 作为采样门槛。该函数 ping 当前 DNS 服务器；ICMP 不响应时，即使 DNS 和 NTP UDP 可用，也不发送 NTP 请求。
- v52 添加 `NETUTILS_NTPCLIENT_CHECK_CONNECTIVITY` 配置，默认保留原行为，仅 A733 禁用该前置条件，直接尝试标准 NTP 请求。
- 原 DNS 单次超时 30 秒、重试 3 次，比 Wi-Fi 命令固定等待的 30 秒还长。A733 改为收发超时 3 秒、重试 2 次。
- `wifi connect` 仅启动/复用后台 NTP，不等待 30 秒，更不会把尚在 DNS 查询阶段的任务宣布为超时。新增 `wifi time` 查看系统 UTC；时间未设置则启动/复用 NTP 并返回 EAGAIN。
- `ntpcstatus` 是采样证据，`wifi time` 显示有效年份不等于已证明该时钟由 NTP 设置。保留证书验证，不用 curl -k 绕过，不伪造编译时间为真实时间。
- UDP/123 被网络封锁、DNS 故障等外部原因依然可能导致校时失败；不能保证任意网络下 NTP 必定成功。

## 问题 4：SSH port 22 Connection refused

- 确定错误：加载 `sshd.auth` 哈希后，旧条件仍因 `username` 非空、明文 `password` 为空而调用交互密码提示，发生在 `ssh_bind_listen()` 之前。无人值守任务可能退出或等待输入。
- 修复：只有显式旧式参数模式且尚无哈希时允许提示。默认服务模式/`--service` 永不读取密码；无有效认证时明确失败并记录日志。
- 旧板级日志仅凭 posix_spawn 成功就宣布服务在监听。现在只记“SSH task created”；实际 listen 成功后由服务输出 `SSHD: listening ... 0.0.0.0:22`。
- 增加 `sshd --status`，显示 pid、listening 和 sessions，不读取/输出密码或密钥。
- 防止重复启动在 bind 之前清空共享认证全局变量；重复启动会明确拒绝，不影响已有服务。运行中更改认证须先正常停止服务。
- 最大四个并发会话，各自工作线程；超限拒绝新会话。正常停止时关闭会话传输、等待线程释放后再 finalize，防止现有会话使用已清理的库或认证状态。
- 板级 reboot/poweroff 在交给 PSCI 前调用 sync，降低刚保存配置或生成密钥后立即重启造成未刷新数据丢失的风险；它不是完整的服务停机管理，也不能保证正在并发写入的应用数据一致性。
- `Connection refused`、`Connection timed out`、主机密钥变化、密码拒绝是不同层次的问题，不能再混用解释。

## 构建与回归

- 脚本：`build-a733-ssh-lan-v52.sh`，使用当前 WSL Ubuntu、原有 AArch64 工具链，新输出目录 `cmake_out/cubie-a7z_nsh_v52_ssh_lan`。
- 增量构建使用同一脚本的 `--incremental` 参数；直接调用 cmake 未设置脚本中的 PATH 曾因找不到 ccache 失败。不要因此安装/替换工具链，重新使用封装脚本即可。
- 镜像脚本：`package-a733-ssh-lan-v52.sh`，核验 v45 基线哈希后生成独立 v52 镜像，不碰物理 SD 卡、不覆盖 v45/v51。
- `test-a733-v52.py` 从真实源码提取函数后用宿主 C 编译执行。测试密码提示次数、缓冲区初始化、输入失败/不匹配/短密码/用户名拒绝、认证文件不含明文；两个普通发送线程加一个 EAPOL 发送线程各 500 次验证 DMA 缓冲区不被改写，并检查 USB 失败后仍可继续发送。
- USB 和密码派生在这些主机测试里是桩实现：不能据此声称已验证真实 NPU/无线/密码学运算。完整 AArch64 构建另外验证真实依赖和接口。
- 最终结果：完整及增量构建通过；3 组宿主回归通过；镜像内核逐字节一致；ext4、GPT、FAT 检查通过。使用 `verify-a733-ssh-lan-v52.sh` 重跑只读 GPT/FAT 验证，避免 Windows/WSL 内联命令的引号与变量传递错误。

## 用户验收步骤

### 首次配置或全卡刷写导致 /data 被替换

完整镜像刷写会替换数据分区。先保留自己的文件；项目不会复制用户的 SSH 私钥或认证数据到镜像。

在串口执行一次：

```sh
sshd --setup openvela
reboot
```

应只询问“新密码”“确认密码”两遍。已有有效认证文件且只更新内核时可跳过设置；不要反复删除主机密钥。

### 不经过 ping 的首次登录

```sh
sshd --status
wifi connect Xiaomi_47C7_5G 5
ifconfig
sshd --status
dmesg
```

连接密码在板端交互输入，不要粘贴到项目、文档或测试日志。应看到 `listening=1`，联网后用 ifconfig 的实际 DHCP 地址在 Windows 直接运行：

```powershell
ssh openvela@192.168.31.27
```

这里地址是本次日志示例，不是写死的目标。先不要 ping 或运行 Test-NetConnection，以验证首次 SSH 本身。第一次提示主机密钥时核对是自己的板子；刷机改变私钥时，确认新指纹后再更新该主机的 known_hosts，不能禁用全局严格校验。

### 并发、重启、校时

1. 用两台设备或两个 Windows 窗口同时登录，分别执行 `uname -a`、`pwd`。`sshd --status` 应显示两个会话，退出其中一个不影响另一个。
2. 再启动一次 `sshd` 应明确提示已运行；不能因此破坏既有登录。
3. 重启 3 次，每次联网后直接 SSH；每次 Wi-Fi 密码仍需手动输入（本项目不持久化 Wi-Fi 密码）。不应再设置/确认 SSH 密码。
4. `wifi time`、`ntpcstatus` 检查 UTC 与实际采样；然后验证 HTTPS。若失败，保留 DNS/NTP 日志，不关闭 TLS 验证。
5. `reboot` 已由 v51 实板日志证明；`poweroff` 仍需真实板验证，本轮没有远程执行关机。

若仍失败，提供 `sshd --status`、`ps`、`dmesg`、`wifi status` 和 Windows 精确错误。不要发送 `/data/ssh/sshd.auth` 内容或任何私钥。
