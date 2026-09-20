# Allwinner A733 / Radxa Cubie A7Z openvela 适配状态

## 2026-09-03：SSH/FTP 可选自启动与 Wi-Fi 记忆连接 v55 候选版

用户已确认 v54 原中文 PDF 上传成功。v55 新增 `service <ssh|ftp|wifi> on|off|status`，
SSH/FTP 自启动默认为 off，Wi-Fi 默认允许保存下一次成功连接；策略仅控制下次开机。
兼容 `sshd --autostart`、`ftpd autostart`、`wifi autoconnect`，新增 `ftpd setup` 保存
随机盐和 PBKDF2 校验值，开机启动不交互；Wi-Fi WPA2/DHCP 成功才更新单个板端记录，
重新扫描后自动连接，不依赖旧 BSS 索引。支持 reconnect/forget/cancel。

Wi-Fi 口令需以设备可读形式存于 `/data/system/wifi.profile`；当前 FAT 不提供加密或
可靠 Unix 文件权限。实际凭据不写项目、日志和镜像，FTP 只在可信 LAN 使用。
原“先由板端 ping 才首次可达”问题仍独立待查，v55 未改底层无线/ARP。

完整 1901 步构建（6:57）及最后增量构建通过；主机真实 PBKDF2 向量、记录损坏拒绝、
并发写、8 种启动策略、Wi-Fi 成功才保存/取消/无密码提示，以及实际 FTP 认证回调/
中文/64 MiB/4 并发回归通过。板端无人值守重启与 Windows 多客户端仍需复测。
内核 1572776 字节，SHA256 `51fcc9cd790e33d2248343d79183f82aa8b23e75317edecc8e9e48c4b790ef88`。
操作说明 `A733_SERVICES_V55.md`；源码快照 `archives/a733-services-v55-candidate`；
构建/打包 `build-a733-services-v55.sh` / `package-a733-services-v55.sh`。

v55 镜像 `openvela-a733-cubie-a7z-sd-services-v55-candidate.img`，2147483648 字节，
SHA256 `a56c825c7c7c32efe5475b1c988c014e1e41acef2e88f42e6243b8baf0cb1577`。
GPT/ext4 检查通过，启动分区之外与 v54 一致，未操作物理 TF 卡。

## 2026-09-03：FTP 中文文件名与错误恢复 v54 候选修复

v53 实板已确认 FTP 下载成功，上传中文 PDF 报 Can not open file。
定位原文件名 UTF-8 为 41 字节，旧 VFS NAME_MAX=32 在 open 前拒绝；FTP 550
之后负返回值又关闭会话。v54 提升路径/文件名限制，补充 UTF8 能力声明，文件
错误返回 errno 并保留有效控制连接。测试、构建和实板验收边界详见
`A733_FTP_V54_FILENAME_ERRORS.md`。首次 LAN 连接仍依赖板端 ping 的问题未关闭。

完整 1898 步构建和三个宿主回归通过，等待实板复测。2 GiB 候选镜像
`openvela-a733-cubie-a7z-sd-ftp-v54-candidate.img` SHA256：
`f0f5ba8dce357115c40e779cb7745e051c57fef00b68c8818885f7ef3be9fe8c`。
内核 1564504 字节，哈希见 `v54-image-sha256.txt`；源码快照
`archives/a733-ftp-v54-candidate`。未修改的启动/数据区域已与 v53 比较一致，
GPT/ext4 检查通过，未改物理 TF 卡。

## 2026-09-03：FTP 大小文件传输与下载检查 v53 候选版

基于 v52，新增 `ftpd start [port] / stop / status`：默认端口 21、用户 openvela、
根目录 `/data/models`、仅内存临时密码、被动模式、最多 4 客户端、正常停止等待
工作线程退出。FTP 未加密，仅可信 LAN 手动开启，未配置自动启动。
修复 FTP 短发送、TCP 命令分包/合包、APPE 截断、REST 偏移未清除、上传提前
确认成功、停止时帐号内存提前释放等问题。wget 修复短写/零写/HTTP 非 2xx/
刷盘失败处理，明确 HTTPS 使用 curl。curl 的 TLS 校验保持开启。

完整 1898 步构建及最终增量构建通过；主机实际 FTP 库的空文件至 64 MiB
SHA256 往返、续传、追加、4 并发、目录隔离、认证、退出、curl EPSV 兼容通过；
短 I/O 与 ENOSPC 等故障注入通过。尚未收到 v53 板端 FTP/HTTP/HTTPS 测试，
不能称实板完成；v52 的 SSH/LAN 修复同样仍需用户复测。

新镜像 2 GiB，数据分区约 1.56 GiB，基线数据文件逐字节比较、GPT/ext4/FAT
检查通过，所有回环设备已释放；未改动物理 SD、未删除旧镜像，NPU 保持暂停。
本配置文件偏移仍为 32 位，单文件需小于 2 GiB 且低于分区实际可用空间。

- 镜像：`openvela-a733-cubie-a7z-sd-ftp-v53-candidate.img`
- 镜像 SHA256：`e4335eec4f4ed6e3901102a4c123979d6372d88b74c7406a558dee137b6f72ab`
- 内核：1563768 字节，SHA256 `53b397ff629455b503ded1d216c1b21ea0d8732a707795cbacb1574f38534ae1`
- 使用、错误记录与板端验收：`A733_FTP_DOWNLOAD_V53.md`
- 构建/打包：`build-a733-ftp-v53.sh` / `package-a733-ftp-v53.sh`
- 主机回归：`test-a733-ftp-v53.py` / `test-a733-download-v53.py`

## 2026-09-03：SSH 自启动、LAN 发送并发与校时修复 v52

v51 实板确认 reboot 成功；密码提示四遍、乱码、服务创建后未监听以及 NTP
警告仍存在，不能将 v51 自动启动视为完成。v52 修复已确认源码缺陷：两次显式
密码输入且缓冲区清零；服务加载认证后不再交互；真实监听日志与 `sshd --status`；
拒绝重复服务实例，最多四个独立会话并在正常停止时回收；Wi-Fi 发送缓冲区从
构造到 DMA 完成的互斥保护；DHCP 后分散发送 ARP 公告；取消 A733 NTP 的 DNS
服务器 ping 前置条件，缩短 DNS 超时，并将校时与联网成功分离为后台执行。

不持久化用户 Wi-Fi 密码，不改动 NPU 阶段，不关闭 TLS 或 SSH 主机密钥验证。
首次 LAN 不可达的唯一根因尚不能由日志确定，v52 的发送竞态修复必须实板验证。
详细根因、宿主回归测试范围和无预先 ping 的验收步骤见
`A733_SSH_LAN_V52_ERRORS_AND_TESTS.md`。构建输出和镜像校验结果记录到
`archives/a733-ssh-lan-v52-candidate/README.md`；无用户实板结果前均标为候选版。

v52 完整构建、最终增量编译、3 组宿主回归、镜像内核逐字节比较和 GPT/ext4/FAT
检查通过。镜像 512 MiB；SHA-256：
`0932233cb87e733e8b2f716edd6f08acf21d774bfbe7d4448ffe885d80c2b91f`。
内核 1543240 字节；SHA-256：
`d6307507da7591449207e850732782821ec9baf6ba4dfeaf48d82f13b545f1d5`。

## 2026-09-02：持久化 SSH 服务、并发会话与系统电源命令 v51 候选版

v45 已完成实板 SSH 交互登录。v51 将前台测试方式封装为标准服务模式：无参数
执行 `sshd` 时使用 `/data/ssh/ssh_host_ed25519_key`、监听 `0.0.0.0:22`，并在
板级初始化结束时自动拉起。监听不依赖 wlan0 当时是否已有 IP；Wi-Fi 后续完成
关联和 DHCP 后，SSH 会直接变为可达，无需再次启动服务。现有 accept 循环和
每连接独立 pthread 保留，因此不同电脑/手机可以同时建立独立 SSH 会话。

镜像不包含默认密码或通用私钥。首次仅需在串口运行一次
`sshd --setup openvela`：密码关闭回显输入，保存为 16 字节随机盐、120000 轮
PBKDF2-HMAC-SHA256 哈希，明文不会写入命令历史、项目或数据分区。配置完成后
重启，后续每次启动会检测持久认证文件并自动启动服务；也支持
`/data/ssh/authorized_keys` 公钥认证。

A733 已启用 TF-A BL31 的 SMC PSCI 通道并实现板级 `board_reset()` 与
`board_power_off()`，NSH 因而提供 Linux 风格的 `reboot`、`poweroff`。全量
AArch64 构建、链接、配置与镜像 ext4 检查均通过；PSCI 和电源符号已在最终
System.map 中确认。实板仍需验证首次配置、两客户端并发、重启和关机行为。

- v51 内核 SHA-256：`1d3633fd2985661cb0891869bf40ce8b24a2a559c6809b113d58c8d30a8e456a`
- v51 镜像 SHA-256：`13d8f7a5f56a0e02ff8f1a4ef0c9fa053824830670fb24f8c807ae08540f2a82`
- 详细验收：`A733_SSH_SERVICE_POWER_V51.md`
- 存档清单：`archives/a733-ssh-service-power-v51-candidate/README.md`

## 2026-09-01：SSH 主机密钥绑定与联网自动校时 v41 候选版

v40 实板确认 CE TRNG、随机设备、Ctrl+C、Wi-Fi 和 DHCP 已通过。Windows SSH
超时的根因是板端服务没有监听：`-k` 在主机密钥首次生成前尝试导入，生成后未
重新绑定，导致 `ssh_bind_listen()` 报无主机密钥并退出。v41 改为先确保密钥
存在再导入，并增加只在监听成功后输出的 `SSHD listening` 明确检查点。

HTTPS 的 `BADCERT_FUTURE` 被定位为系统时间未设置。v41 在 Wi-Fi 完成 DHCP 后
自动执行一次 NTP 校时，并加入 `ntpcstart/ntpcstatus/ntpcstop` 手动诊断命令。
全量构建、配置/字符串核验、GPT、ext4、FAT、嵌入内核与 CA 哈希均通过。

内核 SHA-256：
`00b35c9b9e232fe66a05384da59dc31908217885d60cfefab4455d12aa48090e`

镜像 SHA-256：
`4fe4edfc61031501b757a21e53bde84e066a543d370c1561a84d471a7e881d02`

实板 SSH 登录和 HTTPS 回归仍待验收；详见 `A733_SSH_NTP_V41.md`。

首次实板启动已经确认 Ed25519 密钥生成和 `SSHD listening` 检查点通过。随后
Windows 超时不是监听回归：该启动周期尚未执行 Wi-Fi 关联和 DHCP，旧 IP 地址
并不在板端。需按“Wi-Fi 连接及地址确认 → 启动 sshd → Windows 连接”的顺序
完成最终登录验收。

## 2026-08-31：Wi-Fi 数据稳定性与可直接使用的 SSH v37 候选版

v36 双频扫描、WPA2、DHCP、DNS 与 HTTP 实板通过，但长 ping 出现间歇丢包，
且 DHCP 下发的路由器 DNS 响应偏慢。v37 将 USB 数据 RX 空闲间隔从 250 us
缩短为 25 us、轮询窗口调整为 3 ms、错误退避降为 1 ms，并关闭尚无完整
DTIM/U-APSD 主机队列支撑的 station PS/U-APSD。新增 `wifi dns <IPv4>`，
`wifi disconnect` 现在同步清除 IP、路由和 DNS。

libssh 内置命令已修复帮助解析和 NuttX 共享地址空间中的重复初始化。`sshd`
支持在 `/data` 首次生成板级 ED25519 主机密钥，并在未给命令行密码时关闭回显
交互读取；无认证配置时拒绝启动。没有加入默认账号、密码或通用私钥。

v37 全量与最终增量构建均通过，候选镜像通过 GPT、ext4、FAT 和内核哈希校验。
内核 SHA-256 为 `320985ea701c4e858e89680db82e61d0ad0a74d0fd487cda1d1f3e87430e2f49`，
镜像 SHA-256 为 `6f586f73a090947141941bced3906e86c1d860f95403cb682fc04cc0e9468611`。
说明见 `A733_WIFI_SSH_V37.md`，存档为 `archives/a733-wifi-ssh-v37-candidate/`。
实板验收前 v50 仍是正式稳定基线。

## 2026-08-31：FCU760K 双频扫描修复与原生 Wi-Fi 管理器 v36 候选版

实板回归发现 FCU760K 已完成 USB、固件、LMAC、RF 校准、信道表和 station VIF
初始化，但连续 `scan5` 均出现固件确认成功而 BSS indication 为零。故障被定位在
D80-U02 的扫描请求粒度/时序，而不是 USB、供电、天线或固件启动。

v36 将扫描拆分为 2.4G、5G 低频非 DFS、5G 高频非 DFS及可选 DFS 小组，每组
空结果自动重试三次，`scanall` 在一次操作中合并双频结果。同时新增 NuttX 原生
`wifi` 命令，提供 `scan/list/connect/disconnect/status`；支持明确选择 2.4G 或
5G。连接密码仅交互读取并关闭回显，不接受命令行密码，也不写入项目。Linux
`nmcli` 未直接移植，因为它依赖 NetworkManager、D-Bus 和 systemd。

全量及增量构建均通过，`wifi`、`ssh`、`scp`、`sshd` 已一起链接。候选镜像已
通过 GPT、ext4、FAT 和嵌入内核哈希校验。内核 SHA-256 为
`af98d4d1da6749ad3eefd3477d9127abe8882f264549dd7ac7299778f009f4b9`，镜像
SHA-256 为 `3bb1b6c6334a04800f1b6ad635ab6b48b786f279bceb0c3508b3d2961f736aa1`。
详细说明见 `A733_WIFI_MANAGER_V36.md`，存档为
`archives/a733-wifi-manager-v36-candidate/`。实板通过前，v50 仍是正式稳定基线。

## 2026-08-31：暂停 NPU，进入联网工具与 SSH v35 候选阶段

NPU v50 已完成存档并冻结，当前稳定镜像不做覆盖。新候选内核启用 NuttX
webclient、mbedTLS、zlib 和项目现有 libssh，封装 `wget`、`ssh`、`scp`、
`sshd`，并继续保留 `ping`、`iperf`、`netcat`、DHCP 与 DNS。SSH 服务端示例
原有的硬编码默认账号密码已删除，默认采用显式主机密钥和 authorized_keys。

v35 已完成 AArch64 全量编译和最终链接，但尚待独立候选镜像的实板 SSH、SCP
和网络回归验收，因此当前正式稳定基线仍是 v50。详细操作见
`A733_NETWORK_SSH_V35.md`，候选存档为
`archives/a733-network-ssh-v35-candidate/`。

## 2026-08-31：YOLOv8 独立全黑动态输入实板通过（v50，当前稳定基线）

在 v49 正式稳定基线之上加入 1,228,800 字节的全黑 640x640 RGB 输入，专门
验证动态输入并非只会重复内嵌 dog 图。文件 SHA-256 为
`3630e065eb7b4540fbab11dbfd2619e8500f211b9c404380a1867fdc44b77c0c`。
内核和 A7PM v3 均未改变；更新后的镜像完成逐字节校验，并已完成实板
动态推理。结果为 `status=0 hw-status=0 verified=1 golden=0`，动态输入
大小为 1,228,800 字节，六路输出 CRC 均与 dog 金标不同；IRQ 为 `0x10`，
idle 为 `0x7fffffff`，MMU、fault、recovery、tail 和 guards 全部正常。
全黑图像解码得到 `candidates=0 detections=0`，符合预期。实板仍保有约
4.24 GB 可用堆，运行结束后系统稳定。

六路全黑输入输出 CRC32 为 `edf19a97`、`25ca6506`、`f0cdd200`、
`027d5105`、`a49f35c9`、`5d1e0cab`。因此 v50 正式取代 v49 成为当前稳定
基线；v49 保留为不含独立全黑测试文件的稳定回退点。正式存档为
`archives/a733-npu-v50-yolov8-black-input-verified`，实板证据为其中的
`real-board-v50.log`。

更新镜像 SHA-256 为
`82032d6eccefa4165cc02cb34e1eb6b44e2e4ba07a7384e72ce3b32631d0a0b3`。

## 2026-08-31：YOLOv8 动态输入、DFL 解码与 NMS 实板通过（v49）

v48 六输出硬件金标保持为稳定回退基线。v49 新增 A7PM v3 输入描述符，允许
在不重新转换或重新追踪模型的情况下，用 `/data/` 下恰好 1,228,800 字节的
640x640 RGB 文件覆盖已捕获输入。固定输入模式仍严格比较六路 Debian 金标
CRC；动态模式不错误比较固定 CRC，而是要求六路输出均被 VIP2 更新，且
IRQ、MMU、命令尾页、内存 guard 全部正常。

运行时已内置 YOLOv8n 六头后处理：三尺度 80x80、40x40、20x20，64 通道
DFL 回归、80 类 sigmoid，score 阈值 0.25、class-aware NMS IoU 0.45，最多
保留 32 个检测框。Linux 实板输出的参考解码结果为 3 个框：class 16
score 0.909660，class 1 score 0.906821，class 2 score 0.653580；这组结果
用于校准 openvela 实现。

- v49 内核 SHA-256：`90457db323ebd2390179cace3b90188e4aac8e98cccdc773c0aeb6c375257884`
- A7PM v3 SHA-256：`3c2891ef77ce9a44dd02cd32bdd7db5ec92d14185014e6de8db5803a6cbecce2`
- 动态 dog 输入 SHA-256：`07a1654c992b1a2e38ccc595a571994889134df9594eef09b5a85d70f4820c60`
- v49 候选镜像 SHA-256：`48db5db8743b8446899feb19b1b26942451c850af909b00fb6df5b770f76b01e`

候选镜像通过 GPT、ext4、FAT、内核及全部 NPU 文件的逐字节校验后，又完成
了实板默认金标和同图动态覆盖验收。两次运行均为 `status=0 hw-status=0
verified=1`，六路 CRC 和 3 个检测框完全一致；固定路径为 `golden=1`，动态
路径为 `active=1 golden=0`，输入大小 1,228,800、raw CRC `2d95eaba`。
MMU/fault/recovery、tail、guards 均正常，随后 LeNet、selftest、apitest 也全部
通过。因此 v49 已升级为当前稳定基线，正式存档为
`archives/a733-npu-v49-yolov8-dynamic-verified`，实板证据为其中的
`real-board-v49.log`。

## 2026-08-30：YOLOv8n 六输出 openvela 实板通过（v48）

已验证 Allwinner 官方 `ubuntu-npu:v2.0.10.2` 工具镜像和 model-zoo
归档完整可用。使用 ACUITY 6.30.22 将官方 model-zoo 的
`yolov8n_6.onnx` 导入，并分别完成 PCQ INT8 与 INT16 量化、六输出仿真和
A733 CID `0x1000003b` NBG 导出。PCQ 仿真约 34.05 ms、模型 3,144,744
字节；INT16 仿真约 54.30 ms、模型 5,887,544 字节。六个对应输出的
PCQ/INT16 余弦相似度均为 0.9853 以上，因此 PCQ 作为主路径，INT16 保留为
精度回退。

- PCQ NBG SHA-256：`f6e44af09ba41cdd99fe9829167d09e4bf568e3347a5f6ca203914e838e28edf`
- INT16 NBG SHA-256：`7185393c0b26bc6a995c5435c9fd1101e93bba347178d7f0b24ff1d8351d185f`
- 固定 640x640 RGB 输入 SHA-256：`07a1654c992b1a2e38ccc595a571994889134df9594eef09b5a85d70f4820c60`

openvela 的 A7PM 运行时和 `pack-a733-viptrace.py` 已从最多四输出扩展为
最多八输出，可容纳 YOLOv8n 的六个输出。`cubie-a7z_nsh_v34` 已用项目自带
AArch64 工具链完整编译、链接并生成最终 v48 内核。

Cubie A7Z 官方 Debian/VIPLite 实板追踪已经通过。硬件报告 151 层、一个
1,228,800 字节输入和六个输出，NPU profile 为 12,604 us / 12,565,727
cycles；六输出 CRC32 分别为 `55d0becd`、`ead06611`、`ba209c57`、
`2637657c`、`9e7f743a`、`2eb4d5ce`。追踪得到 13 个真实 VIP 区域，分布在
4 个 16 MiB 窗口，总声明内存约 12.8 MiB，现有 24 MiB prepared-model
池足够。

追踪采集包为：

`yolov8-openvela/a733-yolov8n-pcq-trace-bundle.tar.gz`

其 SHA-256 为
`e69a1c42a20adcb2d6338b02062a783f1dabf62235c2945b2f07228eb0d7b315`。
实板轨迹 `viptrace-yolov8n-pcq-20260830-153436.tar.gz` 已转换成六输出
`prepared-models/yolov8n-pcq.a7pm`。A7PM 为 8,016,960 字节、13 区域，
SHA-256 为
`f758654d1b61ad82b1b42a8e55b2c91d127eb1baa99e4b752d131e4d13c7bb41`；
其六个金标准 CRC 与 Debian 实板逐一一致。运行时区域上限从 12 扩展至
16，完整交叉编译通过。

v48 openvela 实板候选已经写入最小 SD 镜像并通过 GPT、ext4、FAT、嵌入
内核、Wi-Fi 固件、LeNet/YOLOv5/YOLOv8 A7PM 逐字节校验：

- v48 内核 SHA-256：`8ae653ee37944330be25a314088f27b72e2f420ca39a55352c2fa4cb8c8e72e2`
- v48 最小镜像 SHA-256：`0808ab2cead39a6c440d37091ad6244907ddbed5d4c07e6a1c18c2549516475a`

openvela 实板验收已经通过：`prepared status=0 hw-status=0 verified=1`，
13 个区域、6 个输出，命令地址 `0x0100b038`，IRQ `0x10`，idle
`0x7fffffff`，polls 165；MMU、fault、recovery 和 guards 全部为零。命令尾页
`0x01012000` CRC 为 `5402566b` 且 `tail-changed=0`。六输出 CRC 与 Debian
VIPLite 金标准逐一一致，changed 分别为 1,635,051、2,045,933、408,777、
511,458、102,199 和 127,852。

随后固定 LeNet 输出 CRC 为 `d50f5d79`，`selftest=0`、`apitest=0`，无超时、
取消或护栏损坏；SDMMC、4 GiB 内存和 FCU760K 启动路径也保持正常。v48
因此正式取代 v47 成为当前 NPU 稳定基线。正式存档为
`archives/a733-npu-v48-yolov8-six-output-verified`，完整串口证据为其中的
`real-board-v48.log`。

## 2026-08-30：YOLOv5s 命令尾页修复实板通过（v47）

v46 已经用官方 Debian VIPLite 金标准证明 YOLOv5s 三个输出完全正确，但在
EVENT `0x10` 之后仍记录到尾部 MMUv2 异常。实板 fault address 为
`0x01014000`；A7PM 命令区位于 `0x01011000`、长度 11864 字节，其最后一个
映射页止于 `0x01013fff`，而模型清单在下一页没有任何合法区域。因此该异常
符合 VIP 命令预取越过精确映射边界的特征，不是输出张量或模型映射损坏。

v47 在命令末端下一页动态加入独立 4 KiB 映射，整页填充安全的 VIP `END`
命令对，并拒绝它与任意 A7PM 声明区域重叠。运行前后都会校验该页 CRC；若
硬件改写尾页，即使三个输出 CRC 正确也不会通过。v46 的严格三输出金标准、
guard、EVENT 和复位恢复校验全部保留。

- v47 内核 SHA-256：`36a82f429e06621edd41e27ccf54f7e824cb82de67c57444e228bd2978b1c419`
- A7PM SHA-256：`3feb08593d743bb5ca239110013954fe08ad8a864c81fd719b65b2630dbc4472`
- 最小镜像 SHA-256：`1ab3a860edf2f27ebc8c198c4fe9cb42119d544ff5069e4dcaf3a290bcbd92ce`
- 回退基线：`archives/a733-npu-v46-yolov5-golden-verified`
- 正式存档：`archives/a733-npu-v47-command-tail-verified`

实板验收命令：

```text
echo prepared=/data/npu/yolov5s-zero.a7pm > /dev/npu0
cat /dev/npu0
echo lenet > /dev/npu0
echo selftest > /dev/npu0
echo apitest > /dev/npu0
cat /dev/npu0
dmesg
```

实板已经得到 `prepared status=0 hw-status=0 verified=1`、IRQ `0x10`、
`mmu=0`、`fault=0`、`recovery=0`，尾页为 `0x01014000/0x4175a000`、CRC32
`5402566b` 且 `tail-changed=0`。三个输出 CRC/changed 与 v46 金标准完全一致。
随后 LeNet 返回 `d50f5d79`，`selftest=0`、`apitest=0`、guards 为零。因此
命令预取越界问题已经闭环，v47 正式取代 v46 成为 NPU 当前稳定基线。

## 2026-08-29：YOLOv5s 三输出实板金标准通过（v46）

v45 实板已经完成真正的 640x640 YOLOv5s NPU 计算。三个输出张量的原始
CRC32 分别为 `53127e9c`、`ddb5675e`、`6e01633e`，逐一等于官方 Debian
VIPLite 金标准；changed 分别为 `6500490`、`1626091`、`406957`，内存护栏为
零。随后固定 LeNet、`selftest`、`apitest` 全部通过，证明跨五个 MMU 窗口的
十区域映射、三输出回写、页表恢复与回归安全均已由实板验证。

旧运行时仍报告 `prepared status=-14`，原因是 EVENT `0x10` 与尾部 MMUv2
异常 `0x40000000` 同时到达；该异常发生在三个完整金标准输出写回之后。v46
将计算结果与硬件尾部状态分开：只有全部输出 CRC/changed、护栏与复位恢复
同时通过才报告 `status=0 verified=1`，并原样保留 `hw-status=-14`、MMU status、
fault address 和 recovery，绝不把普通 MMU 映射错误误判为成功。

- v46 内核 SHA-256：`93c963ff2aeb805e611a434b8904b9bace40a8eeff9d39dddbb747c609ff0264`
- A7PM SHA-256：`3feb08593d743bb5ca239110013954fe08ad8a864c81fd719b65b2630dbc4472`
- 最小镜像 SHA-256：`e24fc543394ccfbc2ae4eaaae97fee1552091faad48f053722e2e9192cffe831`

v46 复验命令：

```text
echo prepared=/data/npu/yolov5s-zero.a7pm > /dev/npu0
cat /dev/npu0
echo lenet > /dev/npu0
echo selftest > /dev/npu0
echo apitest > /dev/npu0
cat /dev/npu0
dmesg
```

预期 `prepared status=0`、`verified=1`，三个 CRC 保持不变。请回传包含新增
`mmu=` 与 `fault=` 的 prepared 行；它将用于消除尾部异常本身，而不会阻塞已
经由金标准证明正确的 YOLOv5 NPU 推理里程碑。

## 2026-08-29：YOLOv5s A7PM v2 实板候选

官方 A733 Debian VIPLite 已成功运行 `yolov5s_rt_uint8_a733.nb` 并产生完整
授权轨迹。openvela 打包器和运行时现已支持 A7PM v2 多输出包、最多四个输出，
以及动态 16 MiB VIP MMU 窗口。该 YOLOv5s 轨迹包含跨五个窗口的十个区域和
三个输出张量，预期原始 CRC32 分别为 `53127e9c`、`ddb5675e`、`6e01633e`。

运行时使用 24 MiB prepared-model 池和 16 个独立 STLB，低 32 位 DMA arena
扩展为 32 MiB；每次运行后恢复原 MTLB 项，因此保留固定 LeNet 和通用 ABI
路径。交叉编译及链接已通过。当前最小 SD 镜像已包含新内核以及
`/data/npu/yolov5s-zero.a7pm`，文件系统、内核和模型包逐字节校验均通过。

- 内核 SHA-256：`cf764ee50f6df2e64da10ab56817fae5308c966f97f359badbc84d37580495f5`
- A7PM SHA-256：`3feb08593d743bb5ca239110013954fe08ad8a864c81fd719b65b2630dbc4472`
- 镜像 SHA-256：`092f91a2a9760e747fde1d7b4d906beac88dfa3acdd3d923118364ac0e370199`

实板验收：

```text
echo prepared=/data/npu/yolov5s-zero.a7pm > /dev/npu0
cat /dev/npu0
```

预期 prepared 状态为 `0`、regions 为 `10`、outputs 为 `3`、command 为
`01011038`，三个输出 CRC 与上述值完全一致，changed 均非零，VIP 正常完成且
guards 为 `0`。随后重跑 `lenet`、`selftest`、`apitest`，验证页表恢复和回归安全。

更新时间：2026-08-24

## 当前推荐版本（v33.1）

- 构建目录：`quickly-openvela/cmake_out/cubie-a7z_nsh_v33`
- 内核归档：`openvela-a733-stage1/Image.v33.1-fcu760k-auth-window-fair-data`
- 配置归档：`openvela-a733-stage1/config.v33.1-fcu760k-auth-window-fair-data`
- 内核 SHA-256：`5da4dee9ea7cc95c8b042c5e3ad5a43e6558ea52a298988f99e2522babd2d232`
- 最小 SD 镜像：`openvela-a733-cubie-a7z-sd-minimal.img`
- 最小 SD 镜像 SHA-256：`4bcf1965832af349c98ccdc59088127683d32aa3db617efce4fa67b437568a70`

v32.2 已由实板证明 FCU760K 固件启动、2.4/5 GHz 扫描、WPA2、DHCP、DNS、
ICMP、TCP 和 UDP 全链路可用。实测 TCP 为约 0.61 Mbit/s，UDP 为约
0.53 Mbit/s 且 1381/1381 数据报无丢包。该速度不是射频能力，而是主机 USB
调度造成的固定上限：RX 工作线程持有全局 USB 锁进行 20 ms 空轮询，TX 每约
21.7 ms 才获得一次发送机会，与实测约 46 包/秒完全吻合。

v33 完成以下收尾优化：

- 把串行 USB RX 的单次空轮询从 20 ms 缩短到 1 ms；
- 增加 TX-pending 调度提示与 250 us 空闲让步，使 RX 不再长期压住 TX；
- 保留同一 USB 互斥锁作为真正的并发正确性边界，没有引入双提交风险；
- 扩充 IOB、TCP/UDP write buffer，并启用 NewReno、窗口扩大和 SACK；
- NuttX `iperf` UDP 客户端发送标准负 ID 结束报文，Windows iperf2 服务端不应再以超时结束；
- 去掉每次启动应用都会输出的调试标记 `T0/T1`；
- 未伪造 VHT/HE 能力。官方接口要求完整 VHT/HE capability、MCS/NSS 与 PPE
  数据，当前先保留已验证 HT40，避免只翻一个标志导致固件 ABI 不一致。

工程和镜像没有保存 SSID 或密码。密码只通过 NSH 临时命令传入；分享串口
日志前仍须删除包含密码的命令行。

v33 首次实板测试发现关联成功但 `wpa2 state=1 m1=0`。根因不是密码或 AP，
而是 1 ms EHCI 轮询只在延时前读取一次 ACTIVE，延时结束后没有最后一次读取，
使短轮询实际上接近 0 ms，丢失异步 EAPOL M1。v33.1 修正最终完成位检查，并
采用两档接收窗口：WPA 建链期间保留已验证的 20 ms，受控端口打开后切换为
2 ms 公平数据轮询。这样恢复握手可靠性，同时保留明显高于旧 20 ms 固定上限
的吞吐空间。

v33.1 已完整编译，并通过 GPT、ext4、FAT、内核逐字节对比及五个
AIC8800D80 固件逐字节对比。2026-08-24 实板验收进一步确认：5 GHz 扫描、
关联、WPA2 四次握手、PTK/GTK 安装、受控端口、DHCP、DNS、网关与公网 ICMP
全部成功；`wpa2 checkpoint=0 state=3 m1=1 m3=1 ptk=1 gtk=1 port=1`，
DHCP 获得有效地址、掩码、网关与 DNS。

iperf2 实板结果如下：

- TCP A7Z → Windows：30.25 秒传输 14.9 MiB，平均 4.13 Mbit/s；相比旧版
  约 0.61 Mbit/s 提升约 6.8 倍；
- UDP A7Z → Windows：发送端平均 11.47 Mbit/s，Windows 实收 10.8 Mbit/s；
  29372 个数据报丢失 1661 个（5.7%），前约 22 秒为 0% 丢包，随后出现短时
  接收拥塞；
- UDP 结束报文生效，Windows iperf2 正常输出最终汇总，不再以
  `recvmsg failed: Connection timed out` 结束。

v33.1 至此正式标记为实板通过并作为当前推荐版本。UDP 的 5.7% 是无流控
满速发送超过当前主机接收余量后的性能边界，不是关联、加密或驱动功能失败。
如需零丢包，应降低 UDP 目标速率或继续实现独立 EHCI 队列/异步完成机制。

FCU760K v33.1 已冻结到：

```text
A733-A7Z-ALL-Files/archives/fcu760k-v33.1-hardware-passed
```

该存档包含可恢复源码、defconfig、内核、System.map、五个 D80-U02 固件、
实板记录和 SHA-256 清单，不重复复制 512 MiB 最小镜像。后续工作切换为 NPU
优先，计划见 `A733_NPU_PLAN.md`；Bluetooth HCI 保留为无线后续阶段。

## 当前硬件与启动条件

- SoC：Allwinner A733（sun60iw2p1）
- 开发板：Radxa Cubie A7Z
- DRAM：4 GiB LPDDR4，Boot0 实测识别 `4096 M`
- 当前启动介质：32 GB SD 卡，SD High Speed、50 MHz、4-bit
- 暂未安装 UFS；启动日志中的 UFS `Device not present` 属于预期现象
- 启动链：Boot0 → ATF/BL31 → 32-bit U-Boot → `booti` → AArch64 openvela
- openvela 装载/入口地址：`0x40200000`
- BL31 地址：`0x48000000`
- 控制台：UART0，115200 8N1

## 当前使用的镜像

- 完整 SD 镜像：`openvela-a733-cubie-a7z-sd-stage1.img`
- 完整镜像 SHA-256：
  `e2df052ab8e67e6342b3cc639b518a2b7dbad6a9afd3385ccd71be0caf518a71`
- 内嵌 openvela `Image` SHA-256：
  `075e04444eb7b87993d44d945b55c222f9b51e2197eddf39dd52023be4706ec6`
- 构建目录：`quickly-openvela/cmake_out/cubie-a7z_nsh_v4`

## 2026-08-18 实板结果

完整镜像可由 SD 卡启动，U-Boot 菜单中的
`openvela A733 (SD stage 1 diagnostics)` 可正确读取 openvela Image 和官方
Cubie A7Z DTB。BL31 确认下一阶段地址为 `0x40200000`，并成功进入 EL1。

上一版首次调度诊断的串口最后输出：

```text
A7Z0
A7Z1
N0
N1
H0
H1
H2
H3
H4
S0
S1
H5
N2
N3
N4
N5
B0
B1
B2
B3
A0
A1
A2
B4
N6
N7
C0
C1
C2
C3
C4
T0
T1
C0
C1
C2
C3
C4
T0
X0
X1
C0
C1
C2
C3
C4
N8
```

随后没有出现 `NuttShell (NSH)` 或 `nsh>`。

## 已由标记确认通过的路径

- AArch64 Image 入口及 C 运行环境
- MMU/基础内存初始化
- 文件系统核心初始化
- GICv3 初始化
- ARM generic timer 初始化函数返回
- UART0/16550 设备注册
- 硬件初始化完整返回
- CPU0 IDLE 任务启动记录
- IDLE 任务的 stdin/stdout/stderr 建立完成
- 系统状态切换到 `OSREADY`
- `nx_bringup()` 进入并完整返回
- 环境初始化
- 工作队列创建路径返回
- NSH 初始化任务的 `task_spawn()` 返回
- `CONFIG_16550_SUPRESS_CONFIG=y` 已生效，避免首次打开控制台时改写
  U-Boot 已建立的 UART 配置

## 当前精确结论与修复

日志证明首次上下文切换成功，hpwork 与 NSH 任务都已运行；`X0/X1` 证明
`nsh_main()` 和 `nsh_initialize()` 已返回。NSH 随后在控制台输出路径阻塞，CPU
切回 IDLE（`N8`）。轮询式启动标记始终可输出，因此问题不是 UART 基址、波特率
或 UART0 IRQ 编号，而是 UART 从轮询模式切换到中断模式后收不到 SPI 中断。

官方 Cubie A7Z 设备树确认 UART0 为 `GIC_SPI 2`，对应 NuttX IRQ 34。通用 GICv3
驱动在启用 SPI 时曾把 IROUTER 写成数值 CPU index 0，而不是实际启动核 MPIDR。
本版启用 `CONFIG_ARM64_GICV3_SPI_ROUTING_CPU0=y`，使所有 SPI 路由到实际启动核
affinity，并避免启用 UART IRQ 时被错误覆盖。继续保留
`CONFIG_16550_SUPRESS_CONFIG=y`，不改写 U-Boot 已配置的 UART。

## 2026-08-18 GICv3 SPI 路由修复版

已从干净的 `cubie-a7z_nsh_v3` 目录完成全量构建，并把新 `Image` 写入完整
SD 镜像。镜像内文件哈希与外部测试包一致。预期修复后在 `X1` 之后显示
`NuttShell (NSH)` 与 `nsh>`；仍保留启动诊断标记，便于实板复核。

实板复测 v3 后标记仍原样停在 `N8`，说明仅修正 GICv3 IROUTER 不足以恢复
16550 中断控制台。v4 因此新增独立 `/dev/a7zconsole` 轮询 UART0 设备，并通过
`CONFIG_NSH_ALTCONDEV` 将 NSH 的 stdin/stdout/stderr 全部映射到该设备。此路径
直接复用已验证的 `arm64_lowputc()` 和 UART0 LSR/RBR，不依赖 UART SPI 中断；
原 `/dev/console` 和 16550 驱动继续保留，供进入 NSH 后进一步诊断。

## 2026-08-18 v4 实板通过

完整 SD 镜像已在 4 GiB Cubie A7Z 上实测启动成功。关键尾部日志为：

```text
S0
S1
P1
...
X0
X1

NuttShell (NSH)
nsh>
```

`P1` 证明 `/dev/a7zconsole` 注册成功；NSH greeting 和交互提示符证明轮询 UART0
的输入输出路径可用。至此已验证 SD 启动链、BL31 切换到非安全 EL1、AArch64
入口、MMU、基础内存、GICv3 初始化、调度器、任务切换、文件描述符建立以及
交互式 NSH。当前轮询控制台是可靠的 bring-up 基线；UART0 中断模式尚未闭环，
后续应在不影响该基线的前提下单独调试。

首次 v4 实测确认 NSH greeting 与提示符可输出，但输入命令后无响应。双通道版本
进一步出现了 `RI` 和 `RX`：这证明 NSH read 已执行且 UART0 收到了首字符，排除
基址、引脚、波特率和终端设置；但命令仍无法完成，说明通用 16550 接收环与直接
读取 RBR 会把同一行拆给两个消费者。当前镜像在 `/dev/a7zconsole` 打开时清零
UART0 IER，使轮询设备独占 RX，并同时检查 LSR.DR、USR.RFNE、RFL 后读取 RBR。
新增诊断标记：`PR` 表示已切换到 polling raw RX，`RI` 表示 NSH 已进入 read，
`RX` 表示首次收到字符。

独占轮询版实测继续出现 `RI/RX`，但按 Enter 仍不提交。源码核查确认
`readline_common()` 只用 LF（`0x0a`）结束命令，而串口终端通常发送 CR
（`0x0d`）。标准 TTY 依靠 termios `ICRNL` 转换，但 `/dev/a7zconsole` 是原始
字符设备且没有 termios ioctl。当前镜像已在 raw RX 层把 CR 转成 LF，使 Enter
能够结束并提交 NSH 命令；普通字符和其他控制字符保持原样。

## 2026-08-18 v4 CR→LF 版实板通过

完整 SD 镜像 SHA-256 为
`7b6935f2d62651bdfb9dd531fc87ba248362a3689de0b2f4c69600006071bda8`。
4 GiB Cubie A7Z 实板确认可以进入交互式 NSH，并成功执行：

```text
nsh> uname -a
NuttX 0.0.0  Aug 18 2026 21:55:39 arm64 cubie-a7z
nsh> uptime
00:01:08 up  0:01, load average: 0.00, 0.00, 0.00
nsh> time "sleep 5"
5.0003 sec
```

这确认了 SD 启动链、BL31 AArch64/EL1 切换、C 运行环境、MMU 基线、GICv3
初始化、调度器、任务上下文切换、ARM generic timer、UART0 轮询收发及 NSH
命令解析全部工作。日志中的 `PR` 三次分别对应 NSH 打开 stdin/stdout/stderr；
`RI/RX` 分别证明 readline 已进入读取和 UART 已收到字符。

当前 `free`、`ps` 报 `/proc` 未挂载，`dmesg` 报 `/dev/kmsg` 不存在。这属于下一
阶段的基础系统服务缺项，不影响上述启动闭环：需要启用并挂载 procfs、注册
syslog/kmsg，再验证 4 GiB 内存布局。启动诊断标记仍保留，待基础服务稳定后清理。

## 2026-08-18 基础服务与 4 GiB 内存联合验证版

本版把能够一起验证且故障边界清晰的三项工作合并：

- 启用 `CONFIG_NSH_ARCHINIT`，在 `board_app_initialize()` 中自动把 procfs
  挂载到 `/proc`，恢复 `free`、`ps` 和 `/proc/meminfo`。
- 启用 16 KiB RAMLOG syslog 缓冲，提供 `/dev/kmsg` 和 `dmesg`。
- 将 4 GiB LPDDR4 拆成两个 NuttX heap/MMU 区域：第一段保持
  `0x40200000..0x47ffffff`，第二段使用
  `0x49000000..0x13ffffffff`；中间的
  `0x48000000..0x48ffffff` 继续为 BL31 保留，绝不加入分配器。

完整构建已通过，最终 `Image` 为 166392 字节。镜像分区内反向读取所得 SHA-256
与外部测试包相同，均为
`075e04444eb7b87993d44d945b55c222f9b51e2197eddf39dd52023be4706ec6`。
本节镜像已完成构建与离线校验，随后也完成了以下实板联合验证。

建议实板启动后依次执行：

```text
uname -a
mount
ls /proc
free
ps
ls /dev
dmesg
cat /proc/meminfo
time "sleep 5"
mkrd -m 1 -s 512 524288
free
ls /dev
```

其中 256 MiB RAM disk 大于第一段 126 MiB heap，可迫使分配器使用第二内存区。
若系统仍稳定、`/dev/ram1` 出现且 `free` 的总量约为 4 GiB（扣除保留区及内核
占用），即可判定 procfs、RAMLOG 与跨 BL31 洞的 4 GiB 分段内存同时通过。

### 联合实板验证结果

上述合并功能已经在 4 GiB Cubie A7Z 上全部通过：

- `/proc` 自动挂载，`free`、`ps`、`/proc/meminfo` 均正常。
- `/dev/kmsg` 已注册。
- `free` 报告总 heap 为 `4275782408` 字节，证明第二内存区已经加入分配器。
- `mkrd -m 1 -s 512 524288` 成功创建 256 MiB `/dev/ram1`。
- 创建 RAM disk 后剩余内存为 `4007330064` 字节，NSH 与定时器保持正常。
- `time "sleep 5"` 实测 `5.0004 sec`。

这确认 4 GiB DRAM 可在跳过 BL31 16 MiB 保留洞的前提下安全使用。基础系统服务
阶段至此完成。

## 2026-08-18 512 MiB openvela 专用 SD 镜像

已生成不含 Debian 内核、initrd、rootfs 和用户空间的最小镜像：

`openvela-a733-cubie-a7z-sd-minimal.img`

- 镜像大小：536870912 字节（512 MiB）
- SHA-256：
  `78d4e806acbdd55f0632bd02ef2994d8d2ef784536dfaef5c5d70b15acd6a654`
- 内嵌 `Image` SHA-256：
  `075e04444eb7b87993d44d945b55c222f9b51e2197eddf39dd52023be4706ec6`
- 内嵌官方 A7Z DTB SHA-256：
  `c550d8957a9a29489386c982181e6e1b85a90f310897ecaac2b79177914f91c5`

该镜像原样保留已验证镜像从 GPT 后到第三分区前的官方启动负载、`config` 和
`efi` 分区，重新创建适配 512 MiB 磁盘的 GPT，并把第三分区替换为约 180 MiB
的兼容 ext4。第三分区只包含 openvela `Image`、官方 DTB 和单一 extlinux 启动
项，不再依赖 Debian 文件树或其 UUID。GPT 主备表、ext4 及镜像内文件哈希均已
通过离线检查；最小镜像尚待实板首次启动验证。

## 2026-08-18 v5 板级基础外设合并适配版

在已经实板通过的 512 MiB 最小 SD 镜像和 4 GiB 分段内存基线上，本版一次合并
了下列互不冲突、可独立判定结果的功能：

- Cubie A7Z 用户 LED：按照官方原理图及设备树使用 `PM2`，注册
  `/dev/userled`，由通用 `gpio` 命令控制。
- A733 v103 看门狗：注册 `/dev/watchdog0`。初始化时主动停止 U-Boot 遗留的
  计数，避免系统刚启动便意外复位；只有应用显式启动看门狗后才会运行。
- A733 THS 温度传感器：按 sun60iw2 官方换算公式注册
  `/dev/uorb/sensor_temp0`。若启动链没有交接 THS 时钟，驱动只记录并跳过，
  不会阻塞系统启动或伪造温度。
- `/dev/a733-hw` 只读硬件诊断节点：一次报告 LED、WDT、THS、SDMMC0、
  TWI0/2/5/7、SPI0、PWM0/1、GMAC0 和 UFS 的关键寄存器快照。
- 内置 `gpio`、`sensortest` 命令，刷入后无需再制作测试固件。

这里要严格区分“已注册驱动”和“硬件检查点”：LED、WDT、THS 是可供 openvela
上层调用的设备驱动；SDMMC/I2C/SPI/PWM/GMAC/UFS 在本版仅建立安全、只读、
非破坏性的 SoC/时钟/引脚/控制器状态检查点，尚不等价于完整数据通路驱动。
未安装 UFS 时，UFS 检查结果为 absent/idle 属于预期，不得作为启动失败处理。

最终构建目录：

`quickly-openvela/cmake_out/cubie-a7z_nsh_v5`

最终 openvela `Image`：195216 字节，SHA-256：

`4d9372a42f6e194be365611898ae678085451613b424ac654de13d4231911029`

上一版实板稳定内核已保存在：

`openvela-a733-stage1/Image.stage1-stable`

最终 512 MiB 可烧写镜像 SHA-256：

`709bb93ab441eeaa46744d984bd0c8594bccb8f3b5932aefe15afe5740a865d7`

封装过程中已自动通过 GPT 校验、ext4 只读 `e2fsck` 和镜像内文件哈希校验。
首次实板验收执行：

```text
uname -a
free
ps
ls /dev
dmesg
cat /dev/a733-hw
gpio /dev/userled
gpio -o 0 /dev/userled
sleep 1
gpio -o 1 /dev/userled
sensortest -n 3 temp0
```

若 THS 时钟未由官方启动链启用，`sensortest` 找不到 `temp0` 是本版定义的安全
降级路径；此时应以 `dmesg` 和 `/dev/a733-hw` 中的 THS 原始状态继续定位时钟，
不能通过常量或虚假温度绕过。看门狗首次验收只确认 `/dev/watchdog0` 存在，
不要在尚未确认喂狗命令和复位恢复路径前启动它。

生成脚本为 `make-a733-minimal-image.sh`。后续更新 openvela `Image` 后可以重复
执行该脚本重新生成最小镜像，不需要手工挂载或修改分区。

首次实板测试发现 U-Boot 只扫描 `mmc 0:2`，随后转入 PXE，没有扫描第 3 分区。
对比 GPT 后确认，原成功镜像第 3 分区带有属性 `0x4`，即 legacy bootable bit 2；
初版最小镜像重建 GPT 时遗漏了该位。修正版已把第 3 分区名称恢复为 `primary`，
设置 `Attribute flags: 0000000000000004`，生成脚本也已永久包含此设置。修复后
GPT 主备表再次通过检查；上面的 SHA-256 是修正版镜像哈希。

### 512 MiB 最小镜像实板通过

修正版已经在 Cubie A7Z 实板冷启动通过。U-Boot 关键日志为：

```text
Scanning mmc 0:2...
Scanning mmc 0:3...
Found /boot/extlinux/extlinux.conf
1:      openvela A733 (minimal SD image)
Retrieving file: /boot/openvela/a733/Image
166392 bytes read
Retrieving file: /boot/dtb/sun60i-a733-cubie-a7z.dtb
193692 bytes read
Starting kernel ...
```

openvela 随后进入交互式 NSH，并再次确认：

- `uname -a`：AArch64 `cubie-a7z`。
- `/proc` 自动挂载。
- `free` 总 heap：`4275782408` 字节。
- `ps` 正常显示 IDLE、hpwork 和 nsh_main。
- `/dev/kmsg` 存在，`dmesg` 可读取 RAMLOG。
- `time "sleep 5"`：`5.0001 sec`。

至此已经完全脱离 Debian 内核、initrd、rootfs 和 Linux 用户空间。当前仍保留
官方 Boot0、SCP、BL31、U-Boot 和官方 A7Z DTB，作为已验证的早期启动链；它们
不属于 Linux rootfs。512 MiB 最小镜像现为后续 A733 外设适配的正式基线。

## 原始实板日志

本次原始日志保存在 Codex 附件：

`C:\Users\Lenovo\.codex\attachments\700ae17f-6f8d-43f7-859e-b0e2e98d7879\pasted-text.txt`

## 2026-08-18 v5.1 THS 实板修正

v5 首次实板测试确认 LED、看门狗、统一诊断节点和全部基础系统服务正常，但
`/dev/uorb/sensor_temp0` 没有注册。日志中的 `code=0` 证明 THS 没有产生采样；
与此同时，旧版 `/dev/a733-hw` 错误地把无效值 0 代入换算公式并显示
`167.265C`，该温度无效。

重新逐行对照官方 sun60iw2 Linux 5.15 BSP 后确认有三处实现错误：

- `SUN50I_H616_THS_PC` 的实际偏移是 `0x08`，不是 `0x40`。
- `TEMP_PERIOD(28)` 位于寄存器的 bits 31:12，必须写成 `28 << 12`。
- 官方设备树要求同时启用 THS0 总线门控、释放 THS0 reset，并开启
  GPADC0 24 MHz 叶时钟；这些均为专用叶节点，不会修改共享 PLL。

v5.1 已按官方顺序完成 reset assert、GPADC 24 MHz gate、THS reset deassert、
THS bus gate 和采样寄存器初始化。诊断节点也增加了 `gpadc_clk`、`bgr` 状态，
并在 code/data-ready 无效时明确输出 `temp0=unavailable`，不再生成虚假温度。

修正版内核 SHA-256：

`f708165951670c9edb8c3d721bbcd74e0bc698e3c27dce1b1136e7c15ce610f5`

修正版 512 MiB 镜像 SHA-256：

`60c7a0c032d9fbf4950eb56f8f0bb147990a93568c025e8aa81386fac56347c6`

镜像内核哈希已反向确认，ext4 已通过只读 `e2fsck`。增量更新脚本保存在
`update-a733-minimal-kernel.sh`，以后只修改 openvela 内核时无需重新复制整个
官方启动负载。

实板复测重点：

```text
dmesg
ls /dev/uorb
cat /dev/a733-hw
sensortest -n 3 temp0
```

预期 `dmesg` 出现真实非零 code 和合理温度，`/dev/uorb/sensor_temp0` 存在，
`sensortest` 连续返回三组温度。即使硬件仍暂时没有采样，诊断也必须显示
`unavailable`，而不能再出现由 code 0 换算出的 `167.265C`。

## 2026-08-19 v5.2 THS 设备注册时序修正

v5.1 实板日志进一步证明 THS 硬件初始化已经正确：启动初期尚无样本，但稍后
`/dev/a733-hw` 能稳定读到 `code0=2262`、`temp0=34.720C`，同时
`DATA_INTS=0x1f`。因此 `/dev/uorb/sensor_temp0` 缺失不是时钟、复位、寄存器或
温度换算问题，而是首次转换与传感器节点注册之间的时序竞争。

v5.2 将初始化策略改为：硬件初始化后立即注册 `sensor_temp0`；如果首个样本尚未
产生，仅记录 `conversion pending`，读取接口在样本就绪前返回 `-EAGAIN`，而不再
放弃节点注册。这与异步传感器的正常驱动模型一致，也避免因启动负载和温度转换
时间细微变化造成节点偶现缺失。

v5.2 内核 SHA-256：

`cb6272ab3079b5fe987a72129ae7e6b1cb8ea542617685a38943638334c31c9e`

v5.2 512 MiB 镜像 SHA-256：

`d29f1a87be196d59c0caccc37648d31cc83c5497ec5b5203ea525a9d83a4b50f`

镜像内 `/boot/openvela/a733/Image` 已反向校验为上述内核哈希，ext4 已通过
`e2fsck -fn`。实板验证命令：

```text
dmesg
ls /dev/uorb
sensortest -n 3 temp0
cat /dev/a733-hw
```

预期 `/dev/uorb/sensor_temp0` 始终存在；若启动瞬间尚未完成第一次转换，日志会
显示 `temp0 registered; conversion pending`，待进入 NSH 后 `sensortest` 应能连续
得到合理温度。启动链中的 `MMC Device 2 not found` 是厂商固件探测未安装的另一
个 MMC 实例产生的非致命提示，与当前 SD 启动、THS 和 openvela 运行均无关。

## 2026-08-19 v6 SDMMC0 / MMCSD / FAT 合并适配版

v5.2 已由实板确认 `/dev/uorb/sensor_temp0` 稳定存在，`sensortest` 连续返回
约 34 摄氏度的真实采样值，THS 阶段正式闭环。v6 将存储路径中可以在同一次
刷写中判定的内容合并完成：

- 新增 sun60iw2p1 SMHC0 标准 `sdio_dev_s` 下半层，基址 `0x04020000`。
- 使用官方 BSP 的 CCU `SMHC0_CLK/BGR`（`0x02001d00/0x02001d0c`）、
  PF0..PF5 4-bit 引脚交接和 PF6 卡检测拓扑。
- 通过 NuttX `mmcsd_slotinitialize()` 注册 `/dev/mmcsd0`，不使用固定扇区或
  私有伪块设备。
- 第一版采用可靠性优先的 PIO：识别阶段 400 kHz，传输阶段 12 MHz，4-bit；
  DMA、UHS 和热插拔 IRQ 留到基础读写闭环之后。
- 同时启用标准 MMC/SD 块层、最多 16 块的多块传输、FAT/VFAT、UTF-8 长文件名
  和 `mkfatfs`，从而一次验证枚举、原始块读、分区访问、文件系统挂载及写回。
- 写命令声明 `SDIO_CAPS_DMABEFOREWRITE`，确保 MMC/SD 上层在 CMD24/CMD25 前
  提供并准备 PIO 缓冲区；这不是宣称已经启用 DMA。
- 修正上游 FAT 长文件名代码中 `offset` 未初始化的 GCC 13 构建告警，初始化为
  0，不关闭 `-Werror`，不改变有效目录项的处理逻辑。

完整构建目录：

`quickly-openvela/cmake_out/cubie-a7z_nsh_v6`

最终 openvela `Image` 大小 232376 字节，SHA-256：

`4dff4e2c54370df3901b9302ca3a1a9c32cbf286dead06e9c6b8abe28d77e915`

最终 512 MiB 镜像 SHA-256：

`0455da7b7f59daf7ca18929bf00f0525e153d824714e986f89c36a84a70fb816`

镜像内核已反向校验为上述哈希，ext4 已通过 `e2fsck -fn`。v5.2 实板稳定内核
另存为 `openvela-a733-stage1/Image.v5.2-ths-stable`，可无损回退。

首次实板验收先不要格式化或覆盖启动卡上的任何分区，依次执行：

```text
dmesg
ls /dev
cat /dev/a733-hw
dd if=/dev/mmcsd0 of=/dev/null bs=512 count=1
dd if=/dev/mmcsd0 of=/dev/null bs=16384 count=64
```

确认 `/dev/mmcsd0` 存在且两次只读测试无错误后，再执行 `ls /dev` 查看通用
MMCSD 层解析出的分区节点。挂载时只挂载已确认的 FAT 分区；不要对承载 Boot0、
BL31、U-Boot、openvela Image 的分区运行 `mkfatfs`。I2C/SPI/PWM 没有在本版
强行初始化：官方 DTS 中 `s_twi0/s_twi1` 属于 PMIC/Type-C 系统总线，PD22、PI12
等候选引脚又与 PWM/PCIe/UART 复用，必须在 SD 基线通过后按连接器原理图选择
安全实例，不能为了“合并阶段”而破坏电源管理或启动介质。

## 2026-08-19 v6.1 SDMMC 多块读取停止流程修正

v6 实板确认 `/dev/mmcsd0` 注册成功且 512 字节单块读取正常，但 16 KiB 多块
读取返回 `EIO`。根因是下半层给 CMD18 设置了 SMHC `AUTO_STOP`，控制器先自动
发送 CMD12；NuttX 通用 MMC/SD 层在数据完成后又按标准流程显式发送 CMD12，
形成双重停止。该行为与 NuttX `mmcsd_readmultiple()` 的下半层契约不一致。

v6.1 移除下半层的 `AUTO_STOP`，数据阶段只等待 `DATA_OVER`；CMD12 完全交由
通用 MMC/SD 层发送一次。未采用 `CONFIG_MMCSD_MULTIBLOCK_LIMIT=1` 的规避
方案，因此仍保留真正的多块读取能力。单块读、4-bit、12 MHz、PIO 以及
`SDIO_CAPS_DMABEFOREWRITE` 均保持不变。

有问题的 v6 内核另存为：

`openvela-a733-stage1/Image.v6-autostop-buggy`

v6.1 `Image` 大小 232376 字节，SHA-256：

`f682938395b6f05145e0c0f89119a8a88fd0d5d3b3c3d5424aaa1c3b7bf6fe93`

v6.1 512 MiB 最小镜像 SHA-256：

`a341298ef212f0191f2aa96701d1f6aef7322b88ba54f928ef7a4d5f6ee2afd9`

镜像内 `/boot/openvela/a733/Image` 已反向校验为上述内核哈希，ext4 已通过
完整 `e2fsck`。实板先只读复测：

```text
dd if=/dev/mmcsd0 of=/dev/null bs=512 count=1
dd if=/dev/mmcsd0 of=/dev/null bs=16384 count=64
dd if=/dev/mmcsd0 of=/dev/null bs=65536 count=16
dmesg
```

三项读取全部无错误后，SDMMC 读路径才算闭环；在此之前仍不要执行原始块写入、
格式化或覆盖启动卡分区。

## 2026-08-19 v7 GPT / FAT32 / 写回合并适配版

v6.1 已由实板确认 512 字节单块、16 KiB 多块和 64 KiB 请求读取全部通过，
SDMMC PIO 读路径正式闭环。v7 将同一存储链中可一次验收的后续工作合并：

- 启用 NuttX 标准 GPT 解析器，并使用 GPT entry index 建立稳定且唯一的节点：
  `/dev/a7z-firmware`、`/dev/a7z-efi`、`/dev/a7z-openvela`、
  `/dev/a7z-data`。
- 厂商 GPT 的前三个文本名称均为 `primary`，不能使用默认名称注册，否则节点
  会冲突；索引映射保留原 GPT 名称和 U-Boot 兼容性，同时解决节点冲突。
- 前三个启动相关节点使用只读意图权限且永不自动挂载；仅第 4 个专用数据分区
  允许写入并自动挂载到 `/data`。
- 自动挂载前强制验证 512 字节扇区、起始 LBA `917504` 和最小容量，布局不符
  就拒绝挂载，防止把其他 SD 卡或启动分区误认为可写数据区。
- 注册只读整卡字符节点 `/dev/sdcard`，用于诊断；正常文件操作统一走
  `/data`，不要直接写整卡节点。
- 512 MiB 镜像的第 3 分区保持原起始 LBA `679936`、legacy bootable 属性和
  ext4 启动内容，但缩至 116 MiB；第 4 分区为约 64 MiB FAT32，GPT 名为
  `data`，卷标为 `VELA_DATA`。
- FAT32 中预置 `README.TXT`，可据此区分真正挂载成功和仅存在空 `/data`
  目录。生成器同时验证 GPT、ext4、FAT32 和镜像内核哈希。

v6.1 实板稳定内核保存在：

`openvela-a733-stage1/Image.v6.1-sd-read-stable`

完整构建目录：

`quickly-openvela/cmake_out/cubie-a7z_nsh_v7`

v7 `Image` 大小 236472 字节，SHA-256：

`0c5106d98fc3ee4de037ea1ac3e8b5796999579b3626be71e5f17cbe81ba35a3`

v7 512 MiB 镜像 SHA-256：

`c9bb93387ed0b12eeba8aa1e1a262af98c33c6831b1089287663b058828529f6`

永久离线校验脚本：

`A733-A7Z-ALL-Files/verify-a733-minimal-image.sh`

`sgdisk` 关于第 4 分区结尾不是 2048-sector boundary 的提示只影响部分磁盘加密
工具；该分区起点严格 1 MiB 对齐、GPT 主备表有效，不影响 SD、FAT32 或 openvela。

实板验收先确认 GPT 和自动挂载，再进行只在专用数据分区内的文件写回：

```text
dmesg
ls /dev
mount
ls /data
cat /data/README.TXT
dd if=/dev/zero of=/data/a733-a.bin bs=16384 count=128
dd if=/data/a733-a.bin of=/data/a733-b.bin bs=16384 count=128
cmp /data/a733-a.bin /data/a733-b.bin
umount /data
mount -t vfat /dev/a7z-data /data
cmp /data/a733-a.bin /data/a733-b.bin
dmesg
```

两个 `cmp` 均无输出且 `dmesg` 无 SDMMC/FAT 错误，才代表 GPT、FAT 文件读写、
多块写、缓存落盘及卸载重挂全部闭环。测试文件总计约 4 MiB，不接触前三个启动
分区；不要对 `/dev/mmcsd0`、`/dev/a7z-firmware`、`/dev/a7z-efi` 或
`/dev/a7z-openvela` 执行写入或格式化。

### v7 实板验收结论（已通过）

2026-08-19 在 4 GiB Cubie A7Z 实板及 SD 启动介质上完成全部验收：

- GPT 成功解析并注册 `/dev/a7z-firmware`、`/dev/a7z-efi`、
  `/dev/a7z-openvela`、`/dev/a7z-data`，同时保留 `/dev/mmcsd0` 和只读
  诊断节点 `/dev/sdcard`。
- 第 4 分区成功以 FAT32 读写方式自动挂载到 `/data`，`mount` 显示
  `/data type vfat`，预置的 `/data/README.TXT` 可正常读取。
- 两个 2 MiB 文件通过 16 KiB 请求完成 SD 到 FAT、FAT 到 FAT 的多块读写；
  首次 `cmp` 无输出。
- `umount /data` 后重新执行
  `mount -t vfat /dev/a7z-data /data`，第二次 `cmp` 仍无输出，证明缓存写回、
  FAT 元数据持久化及重新挂载后的数据一致性均正常。
- 最终 `dmesg` 未出现 SDMMC、GPT、块层或 FAT 的读写错误。

因此，A733 SDMMC0 的 PIO 单块/多块读写、CMD12 停止流程、GPT 分区、FAT32
文件系统、卸载与重挂持久性已经全部闭环。当前 v7 可作为后续板级外设适配的
稳定存储基线，内核归档名为：

`openvela-a733-stage1/Image.v7-gpt-fat-stable`

启动和运行期间反复出现的 `C0`～`C4`、`T0`、`N8` 等字符属于早期移植阶段的
串口调试路标，并非存储错误；它们会显著污染日志和影响性能计时，后续应统一
改为可配置的早期调试选项或在稳定版中关闭。

## 2026-08-19 v8 扩展接口与风扇 PWM 合并适配版

v8 以实板通过的 v7 GPT/FAT/SDMMC 为基线，一次合入彼此独立、不会占用系统
启动总线的 40-pin 扩展接口和板载风扇控制。实现依据为 Cubie A7Z v1.11 原理图、
官方 sun60iw2 pinctrl 表、CCU 时钟定义及 Linux DTS。没有改动 Boot0、SCP、
BL31、U-Boot、PMIC 总线、Type-C 总线、UART0 控制台或 SDMMC0 引脚。

### 本版实现

- `/dev/i2c2`：TWI2，PD16=SCL、PD17=SDA，function 6，100 kHz 默认，
  标准 NuttX I2C master lower-half，轮询传输。
- `/dev/i2c7`：TWI7，PJ22=SCL、PJ23=SDA，function 6，100 kHz 默认，
  标准 NuttX I2C master lower-half，轮询传输。
- `/dev/spi1`：SPI1，PD10..PD13，function 6，mode 0 默认，1 MHz 默认、
  最高限制 12 MHz，8-bit，FIFO 分段轮询传输。
- `/dev/pwm0`：PWM1_CH9/PJ27，function 3，作为 4-pin 风扇 PWM 输出；
  节点注册时保持停止，只有应用明确启动后才产生波形。
- 内置 `i2c`、`spi`、`pwm` 三个 NSH 测试程序，不再需要另编测试固件。
- `/dev/a733-hw` 已改为显示上述三个节点的真实控制器状态，并明确记录
  UART2_M0 与 TWI7 的引脚冲突。
- 任一扩展控制器注册失败只记录错误，不再阻断其余扩展控制器初始化。

### 引脚所有权与禁止组合

| 功能 | 引脚 | v8 状态 | 说明 |
|---|---|---|---|
| TWI2 | PD16/PD17 | 启用 | `/dev/i2c2`，扩展口 |
| TWI7 | PJ22/PJ23 | 启用 | `/dev/i2c7`，扩展口 |
| SPI1 | PD10..PD13 | 启用 | `/dev/spi1`，扩展口 |
| PWM1_CH9 | PJ27 | 启用 | `/dev/pwm0`，风扇 |
| UART2_M0 | PJ22/PJ23 | 仅保留备用方案 | 与 TWI7 冲突，不能同时启用 |
| S-TWI0 | PL0/PL1 | 禁止接管 | PMIC 系统总线 |
| S-TWI1 | PL12/PL13 | 禁止接管 | Type-C 系统总线 |
| UART0 | 官方控制台引脚 | 禁止接管 | 当前 NSH 控制台 |
| SDMMC0 | PF0..PF6 | 禁止接管 | 当前 SD 启动与数据介质 |

所谓“UART2 备用”是已经记录并验证其复用关系，但默认固件绝不把 PJ22/PJ23
从 TWI7 切走。以后若确实需要 UART2，必须制作互斥配置，先关闭
`CONFIG_A733_HEADER_I2C` 中的 TWI7，再切换 pinmux，不能在运行中同时使用。

### 构建与离线校验

完整构建目录：

`quickly-openvela/cmake_out/cubie-a7z_nsh_v8_final`

归档内核：

`openvela-a733-stage1/Image.v8-header-peripherals`

v8 `Image` 大小 257120 字节，SHA-256：

`533ba2e85bd8fa86c6ef86feaa8b38866984675cc2a1a1d0099f44b86a2003b0`

v8 512 MiB 精简镜像 SHA-256：

`a10114ecc837380cc64aa4d6c64afe731a5d2beff8bfebe899798cd024c09429`

最终完整 CMake 构建成功；镜像内核与归档内核反向 SHA-256 完全一致。GPT 主备
表校验通过，ext4 通过完整只读 `e2fsck`，FAT32 通过 `fsck.fat`。v7 稳定内核
`Image.v7-gpt-fat-stable` 保留不变，可随时回退。

### v8 实板验收顺序

先确认基线没有回归：

```text
uname -a
free
ps
ls /dev
dmesg
cat /dev/a733-hw
mount
cat /data/README.TXT
dd if=/dev/mmcsd0 of=/dev/null bs=65536 count=16
```

预期新增 `/dev/i2c2`、`/dev/i2c7`、`/dev/spi1`、`/dev/pwm0`；原有
`/dev/mmcsd0`、GPT 分区节点、`/data`、温度、LED、watchdog 和控制台必须仍在。

I2C 无外设也可先验证总线节点：

```text
i2c bus
i2c dev -b 2 03 77
i2c dev -b 7 03 77
```

扫描只允许用于空扩展口或已确认可安全扫描的设备；未知设备可能对探测写头产生
副作用。若总线外接设备，按其数据手册再用 `i2c get/set/dump` 验证。

SPI 先检查节点：

```text
spi bus
spi exch -b 1 -f 1000000 -m 0 -x 4 55aa1234
```

没有 MISO 回环或 SPI 从设备时，接收值没有判定意义；若将 SPI1 MOSI 与 MISO
短接，返回的四字节应与发送数据一致。不要把 3.3 V SPI 引脚接到 5 V 逻辑。

风扇 PWM 的短时验收命令：

```text
pwm -p /dev/pwm0 -f 25000 -d 50 -t 3
cat /dev/a733-hw
dmesg
```

预期风扇控制输出运行约 3 秒后停止。若没有连接支持 PWM 的 4-pin 风扇，仅确认
命令无错误并读取诊断即可；不要用 PWM 引脚直接给风扇电机供电。

v8 当前状态是“编译、链接、镜像封装和离线文件系统校验已通过，等待实板外设
验收”。在上述测试通过前，不删除早期串口调试路标，也不开始 GMAC/UFS 等会
改变系统资源所有权的下一阶段。

## 2026-08-20：v8 实板基础验收记录（基本通过，暂停于此）

本轮只记录实板结果，不继续开发下一阶段。启动和基础外设验收所使用的是 v8
归档内核 `openvela-a733-stage1/Image.v8-header-peripherals`；串口日志已由用户
保存为本轮原始验收记录。

### 已确认通过

- 512 MiB 精简 SD 镜像能够完成官方 Boot0、U-Boot、TF-A 到 openvela/NSH 的
  完整启动链路；v8 `Image` 为 257120 字节。
- `uname -a` 正确识别为 `arm64 cubie-a7z`。
- 4 GiB 分段内存堆工作正常，`free` 显示约 4.275 GiB 可管理堆空间。
- 原有基线功能未回归：串口控制台、procfs、温度传感器、用户 LED、watchdog、
  SDMMC0、GPT 分区节点及 FAT `/data` 均正常。
- `/data/README.TXT` 可读取，64 KiB 块大小的 SD 读取测试通过。
- 扩展口控制器节点注册成功：
  - TWI2：`/dev/i2c2`，PD16/PD17；
  - TWI7：`/dev/i2c7`，PJ22/PJ23；
  - SPI1：`/dev/spi1`，PD10..PD13；
  - PWM1_CH9：`/dev/pwm0`，PJ27。
- `i2c bus` 正确列出 2、7 两条总线；两条空总线扫描均正常完成，未出现控制器
  传输错误。
- `spi bus` 正确列出 SPI1。
- `pwm -p /dev/pwm0 -f 25000 -d 50 -t 3` 能正常启动并按时停止，PWM 软件控制
  链路通过。
- `/dev/a733-hw` 能报告控制器、引脚复用和冲突信息；UART2_M0 与 TWI7 的
  PJ22/PJ23 互斥关系仍按设计保留。
- 本轮 `dmesg` 未发现新增错误。

### 尚未宣称通过的物理层项目

- I2C 扩展口没有连接实际从设备，因此当前证明的是节点、时钟、pinmux 和空总线
  事务路径可用，不等同于已验证某个具体 I2C 外设。
- SPI1 没有进行 MOSI/MISO 外部回环，也没有连接 SPI 从设备，因此当前只确认
  控制器节点和工具路径；收发数据完整性留待有接线条件时验证。
- PWM 命令链路已通过；若本轮没有观察实际风扇或示波器波形，则不把负载侧电气
  行为列为已验证项。

### 当前冻结点

v8 状态由“等待实板外设验收”更新为“实板基础验收基本通过”。当前工作在此
暂停：不进入 GMAC、UFS 或其他下一阶段，不修改 v8 源码、内核、精简镜像和
启动配置。恢复开发时应从本节继续，并先保留 v7 回退内核及现有 v8 验收基线。

## 2026-08-22 v9 板载 FCU760K 无线 USB1 硬件检查版

### 板型更正与适配边界

根据 Radxa Cubie A7Z 官方规格和 v1.11 原理图，本板没有板载 RJ45 或以太网
PHY；网络硬件是板载 FCU760K/FCU760KAAMD Wi-Fi 6 + Bluetooth 5.4 模组。
该模组通过 A733 的专用 USB1 host 连接，不使用 SDIO，也不应继续实现或测试
此前诊断输出中假设的 GMAC0。`/dev/a733-hw` 已改为明确报告“无板载以太网”。

原理图和官方 DTS 给出的无线硬件关系如下：

- USB_DP/USB_DM 连接 A733 USB1；
- PM0 为 WIFI_3V3 负载开关，高电平有效；
- PM1 为 WL-REG-ON，高电平有效；
- EHCI1 基址 `0x04200000`，OHCI1 companion 基址 `0x04200400`；
- 模组调试 UART 与 PCM 信号在本板未连接，不能把它们当作 Wi-Fi/BT 主通道。

### 本版实现

新增 `CONFIG_A733_WIFI_USB_CHECKPOINT` 和 `/dev/a733-wifi`，启动时依次执行：

1. 把 PM0、PM1 配置为输出并拉高，等待模组上电稳定；
2. 按 sun60iw2 官方时钟定义打开 USB1 shared AHB、USB reference、PHY、EHCI
   和 OHCI 时钟并释放复位；
3. 清除 USB PHY SIDDQ，设置官方 host pass-by/burst 位；
4. 以 polling-only 方式复位并运行 EHCI1，打开 root-port 电源，等待连接后执行
   port reset；
5. 读取 EHCI capability/version/port 状态，并在 `/dev/a733-wifi` 中提供完整
   快照。

这个检查点只证明“模组供电 + USB1 PHY + 高速根端口”链路。USB descriptor
控制传输、VID/PID 和接口/端点解析、FCU760K 固件加载、Wi-Fi netdev、扫描与
关联、DHCP、蓝牙 HCI 均属于后续阶段，不能在本版提前宣称完成。无线模组缺席
或尚未稳定时只记录警告，不能阻断 openvela、SD、FAT 或 NSH 启动。

### 构建、归档与离线校验

为了保留已经实板通过的成果，v8 内核仍冻结为：

`openvela-a733-stage1/Image.v8-header-peripherals`

v9 使用现有已验证的 A733 构建配置增量构建，但输出另行归档为：

`openvela-a733-stage1/Image.v9-fcu760k-usb-checkpoint`

v9 `Image` 大小 261216 字节，SHA-256：

`a255f548a502b25f7c92140f617afaaa6a3b34889bfe996620ab6ffa07977667`

配置和符号表分别归档为：

- `openvela-a733-stage1/config.v9-fcu760k-usb-checkpoint`
- `openvela-a733-stage1/System.map.v9-fcu760k-usb-checkpoint`

更新后的 512 MiB 精简 SD 镜像 SHA-256：

`7b6e4fa0f192789270f334359c56905af18651a102e04fae474634529ce1858c`

完整链接通过。镜像 GPT 主备表无错误，ext4 完整只读检查通过，FAT32 检查
通过；镜像内 `/boot/openvela/a733/Image` 与 v9 源内核 SHA-256 完全一致。
GPT 第 4 分区未按 2048 sector 结束是沿用的 64 MiB 数据分区布局警告，不是
文件系统错误。

### v9 实板验收

烧写更新后的精简镜像后执行：

```text
dmesg
ls /dev
cat /dev/a733-wifi
cat /dev/a733-hw
free
```

理想结果是出现 `/dev/a733-wifi`，日志包含：

```text
A733 WIFI: FCU760K USB1 root port connected at high speed (0)
```

并且 `/dev/a733-wifi` 中 `state=connected-high-speed checkpoint=0`、PM0/PM1
data 位为 1、`siddq=0`。若节点存在但显示 `checkpoint pending`，需保留上述五条
命令的完整输出；这说明 openvela 已启动但无线 USB 硬件检查仍未通过，不应直接
进入固件或网络协议适配。

### v9 实板验收结论（通过）

2026-08-22 实板日志确认 v9 全部达到预期：PM0/PM1 均为高电平，USB PHY
`siddq=0`，EHCI1 根端口以 high-speed 连接，`version=0100`、
`port=00001005`、`checkpoint=0`，并成功生成 `/dev/a733-wifi`。同时
`/dev/mmcsd0`、GPT 分区、FAT `/data`、I2C2/I2C7、SPI1、PWM0、温度传感器和
4 GiB heap 均正常，未发现回归。因此 v9 正式冻结为“无线模组供电、USB1 PHY
和高速根端口通过”的可靠回退点；它不代表 USB 描述符、Wi-Fi 固件或网络已经
完成。

## 2026-08-22 v10 FCU760K USB 枚举版

### 本版目标与实现

v10 从已实板通过的 v9 增量开发，增加 polling-only EHCI control transfer，
没有引入中断、USB 通用 host 栈或 Wi-Fi 协议栈。实现内容包括：

1. 使用 32-bit 可寻址、cache-line 对齐的静态 QH、qTD 和 DMA 缓冲区；
2. 在地址 0 读取前 8 字节设备描述符，并据此取得 EP0 max-packet；
3. 发送 `SET_ADDRESS(1)`，再读取完整 18 字节设备描述符；
4. 读取配置描述符头和完整配置描述符（上限 512 字节）；
5. 解析 interface、alternate setting、endpoint 地址、类型和 max-packet；
6. 发送 `SET_CONFIGURATION`，把 VID/PID、设备类和端点表写入日志及
   `/dev/a733-wifi`；
7. 任一枚举步骤失败只留下 `checkpoint pending (<errno>)`，不会阻断 NSH、
   SDMMC、FAT 或其他板级外设。

这一步只完成 USB 枚举。如果根设备是 USB hub，下一版必须先枚举 hub 下的
FCU760K 子设备；如果根设备本身就是无线复合设备，则可按解析出的 vendor
interface/bulk endpoint 继续固件与消息协议。Wi-Fi netdev、扫描、关联、DHCP
及蓝牙 HCI 仍未完成。

### 构建与归档

活动内核和回退归档：

- `openvela-a733-stage1/Image`
- `openvela-a733-stage1/Image.v10-fcu760k-usb-enumeration`
- `openvela-a733-stage1/config.v10-fcu760k-usb-enumeration`
- `openvela-a733-stage1/System.map.v10-fcu760k-usb-enumeration`

v10 `Image` 大小 261216 字节，SHA-256：

`0c7917864b49545f0bf7faf670281a4c148725b2d8cfed1e9e05f2ac0927abc3`

最终 512 MiB 精简 SD 镜像 SHA-256：

`ed4914cb6e460339f9ce7dc8e3bdb4ae6ef967db15da4aa2720139ed61274a55`

完整编译、链接、GPT 主备表检查、ext4 `e2fsck`、FAT `fsck.fat` 全部通过；
镜像内核与活动内核哈希完全一致。第 4 分区结束位置的 2048-sector 对齐提示是
既有布局警告，不是数据损坏。

### v10 实板验收

烧写最终精简镜像后执行：

```text
dmesg
cat /dev/a733-wifi
ls /dev
free
```

成功标准是日志出现 USB address、VID:PID、interface/endpoint 列表和：

```text
A733 WIFI: USB enumeration passed config=<n>
```

同时 `/dev/a733-wifi` 的 enumeration checkpoint 应为 0，并列出端点映射。
若显示 descriptor checkpoint pending，应提交上述四项完整输出；不要反复修改
供电或 PHY，因为 v9 已经证明这一层可靠。若 v10 在 NSH 前发生回归，可直接用
`Image.v9-fcu760k-usb-checkpoint` 恢复。

### v10 实板结论

2026-08-22 实板日志确认 v10 完整通过。FCU760K 并非 hub 下级设备，而是直接
挂在 USB1 的 AIC8800D80 BootROM：

```text
VID:PID=a69c:8d80
interface=0 alt=0 class=ff/ff/ff endpoints=2
endpoint=01 attr=02 maxpacket=512
endpoint=82 attr=02 maxpacket=512
USB enumeration passed config=1 interfaces=1 descriptors=1 endpoints=2 total=32
```

因此后续路径确定为 AIC BootROM Bulk 协议、官方固件下载、运行态重新枚举、
Wi-Fi netdev，最后才是蓝牙 HCI；不需要 USB hub 分支。

## 2026-08-22 v11 FCU760K AIC BootROM Bulk 协议检查版

### 官方依据

从 Radxa 官方 Bullseye r6 镜像的只读 rootfs 中确认并提取了对应组件：

- DKMS 源码：`/usr/src/aic8800-usb-5.0+git20260123.5f7be68d-5`；
- BootROM loader：`aic_load_fw_usb.ko`，modalias 明确匹配 `a69c:8d80`；
- Wi-Fi：`aic8800_fdrv_usb.ko`；
- Bluetooth：`aic_btusb_usb.ko`；
- 芯片族：AIC8800D80；加载态 PID `8d80`，运行态 PID `8d81`；
- 固件：`/lib/firmware/aic8800_fw/USB/aic8800D80`。

项目内只保存约 4 MiB 的必要官方参考，位于
`fcu760k-official-reference/`，没有复制整套 45 MiB DKMS 树。核心参考包括
`aic_load_fw`、`aic8800_fdrv`、`aic_btusb` 以及 D80/D80X2 固件参考目录。

官方 `aicwf_set_cmd_tx()` 发送格式为 4 字节 USB header、4 字节 dummy、8 字节
LMAC header 和参数；RX 路径则为 4 字节 USB header 后紧跟 `ipc_e2a_msg`。
v11 严格复用了该布局以及：

```text
DBG_MEM_READ_REQ = 0x0400
DBG_MEM_READ_CFM = 0x0401
TASK_DBG         = 1
DRV_TASK_ID      = 100
chip-id address  = 0x40500000
```

### 实现边界

v11 在已有 polling-only EHCI 控制传输上增加单 qTD Bulk 传输：

1. 从 v10 描述符结果动态选择唯一 bulk OUT 和 bulk IN，不硬编码完整端点拓扑；
2. 两个端点均以 SET_CONFIGURATION 后的 DATA0 开始；
3. OUT 发送 20 字节官方 `DBG_MEM_READ_REQ`；
4. IN 最多接收 512 字节，并用 short packet 得到实际长度；
5. 核验 USB 消息类型、packet length、`0x0401` response ID、参数长度以及回显
   地址 `0x40500000`；
6. 仅在全部匹配时记录 protocol checkpoint=0；
7. 所有失败均为非致命检查点，不影响 NSH、SDMMC、FAT、THS 或其他外设。

本版只读一个安全寄存器，不执行 system config、不写 RAM、不下载固件、不发送
START_APP。这样先独立闭环 EHCI 双向 Bulk 和 AIC 协议，避免 USB 层问题与约
800 KiB 固件下载问题混在一次实板测试中。

### 构建与镜像

- 活动内核：`openvela-a733-stage1/Image`
- 归档内核：`openvela-a733-stage1/Image.v11-fcu760k-bulk-protocol`
- 配置：`openvela-a733-stage1/config.v11-fcu760k-bulk-protocol`
- 符号：`openvela-a733-stage1/System.map.v11-fcu760k-bulk-protocol`
- 内核大小：265312 字节
- 内核 SHA-256：
  `4c7cbd036c43f4f395ce11e4476732ae535bc72bc369adf8316782f7cc970222`
- 512 MiB 精简 SD 镜像 SHA-256：
  `48829b7e888bc33d31fd72d5e46e0c0fc9e306c3596c207c33d43d7bb9f35b57`

完整编译和链接通过；镜像 GPT 主备表、ext4 `e2fsck`、FAT `fsck.fat` 均通过，
镜像内嵌内核哈希与活动内核完全一致。第 4 分区结束未落在 2048-sector 边界是
既有布局警告，不是文件系统错误。

### v11 实板验收

烧写 `openvela-a733-cubie-a7z-sd-minimal.img` 后只需执行：

```text
dmesg
cat /dev/a733-wifi
ls /dev
free
```

完全通过时日志应出现：

```text
A733 WIFI: AIC BootROM read32 [40500000]=<非零芯片值> bulk=20/<长度> response=0401 (0)
```

`/dev/a733-wifi` 应同时显示：

```text
bulk: out=01 in=02 maxpacket=512 command=20 response=<长度>
protocol: read32[40500000]=<值> response-id=0401 checkpoint=0
scope: AIC BootROM bulk protocol complete; ...
```

若 checkpoint 非 0，请保留上述四项完整输出。此时不要改供电、PHY、地址或描述
符代码；v9/v10 已分别证明这些层。实板返回 `fb078820`，协议检查点为 0。重新
核对官方 `modules.alias` 后确定该设备必须走 D80 而非 D80X2 分支：`8d80` 由
loader 匹配，`8d81` 由 Wi-Fi/BT 运行态驱动匹配。下一版合入官方 D80 U02 固件、
1024-byte block download、patch table、START_APP 和运行态 USB 重枚举。

## 2026-08-22 v12 FCU760K D80 官方固件启动版

v11 实板确认 `read32[0x40500000]=0xfb078820`，其中芯片修订字节为 `0x07`，
应使用 AIC8800D80 U02 普通版固件。v12 从 Radxa 官方 Bullseye rootfs 提取且仅
打包以下五个文件，运行时路径为 `/data/aic`：

- `fw_adid_8800d80_u02.bin`（1708 字节，下载到 `0x00201940`）；
- `fw_patch_8800d80_u02.bin`（32700 字节，下载到 `0x001e0000`）；
- `fw_patch_8800d80_u02_ext0.bin`（16136 字节，下载到 `0x0020b43c`）；
- `fw_patch_table_8800d80_u02.bin`（1384 字节）；
- `fmacfw_8800d80_u02.bin`（358072 字节，下载到 `0x00120000`）。

实现按官方 `aic_load_fw_usb` 源码复刻：每块 1024 字节固定结构并补零，连续
bulk 传输保持 DATA toggle；patch table 的 INF 元数据只执行前四对，BTMODE
注入默认参数，PWRON 后延时 100 ms，版本块不写入；FMAC 加载后读取其导出的
config/patch 地址，写入三对 D80 patch，最后发送 `HOST_START_APP_AUTO`。
START_APP 后复位 polling EHCI host，从地址 0 重新枚举，只有
`VID:PID=a69c:8d81` 才把 firmware checkpoint 标为 0。

固件初始化已移动到 SDMMC/GPT/FAT 之后；缺文件、文件大小错误、协议错误或
运行态 PID 错误均只记录检查点，不阻断 NSH 和其余外设。文件大小校验可防止把
D80X2、H 或 IPC 变体误写入芯片。

构建、链接、GPT、ext4、FAT 以及内核/五个固件逐文件 `cmp` 全部通过：

- `Image.v12-fcu760k-d80-firmware`：265312 字节；
- 内核 SHA-256：`ae100cdfff4bc42390f7be765b40e58d2ecfa0b18d007d9090289fbfee809a16`；
- 最小镜像 SHA-256：`e58f3029faadf588bc08d83ad2c649586b132b8663b103ce1c680647dd814d45`。

实板验收命令：

```text
dmesg
cat /dev/a733-wifi
ls /dev
free
```

完整成功标准为日志依次出现五段 uploaded、D80 patch config、firmware started，
随后第二次 `USB device ... a69c:8d81`，最终：

```text
A733 WIFI: D80 firmware bytes=408616 runtime=a69c:8d81 (0)
```

`/dev/a733-wifi` 的 `firmware ... checkpoint=0` 也必须成立。此阶段尚不创建
Wi-Fi 网络接口，也不创建 Bluetooth HCI；这两项在运行态 USB 通过后继续。

### v12 首次实板结果与 v13 修正

v12 实板完整通过三个 block 固件段，累计 50544 字节；随后在 patch table 的
`DBG_MEM_WRITE_CFM (0x0403)` 处返回 `-EIO`。日志证明 OUT/IN 均完成 24 字节且
response ID 正确，故并非 USB、DATA toggle、文件或芯片写入失败。根因是 v12
额外要求 CFM 回显值必须逐位等于请求值，而官方 `rwnx_send_dbg_mem_write_req()`
明确把 CFM 缓冲区传为 `NULL`，只等待正确消息 ID，不检查回显内容。v13 已与
官方行为对齐：保留帧长和 `0x0403` ID 校验，移除非官方回显比较。

v13 编译、链接、GPT、ext4、FAT 及五个固件哈希复验通过：

- 内核：`91eda72f4700e592abb69243bc13eb7c696e989767a6028397f10d8fff942e0c`；
- 最小镜像：`3d8bd3681881ab6815664e5fdd2499dfb57fcc5ab8ea66f2cf08ccdb91921e3f`。

### v13 实板结果与 v14 数据缓冲修正

v13 成功执行全部 408616 字节 block 命令，但 FMAC 导出的三个关键字均读回 0，
设备在 START_APP 后仍为 `8d80`。对照官方固件确认文件偏移 `0x198/0x1a0/0x1c`
应分别为 `0x00177158/0x00177c00/0x06090101`。代码审计发现上传函数把
`g_wifi_aic_block` 同时作为源数据和临时目标，构造请求前的 `memset` 因别名关系
把刚读出的固件清零，因此此前实际向芯片发送的是零块。v14 改为直接在独立的
1032 字节参数缓冲区补零并复制有效数据，不再修改源缓冲区；同时在 START_APP
前强制读回上述三个官方关键字，任何不匹配立即返回 `-EILSEQ`。

v14 已完成编译、链接和镜像复验：

- 内核：`903c929aa564c15307810c9cfcb27ef995b622b0bb23382d75f503f4c5a5ad19`；
- 最小镜像：`515779d7292370a39f59d427a9a90e8dce4dd51a33155bf711f8a2e8b300afda`。

### v14 实板验收结论

v14 已在 4 GiB Cubie A7Z 实板完整通过。关键闭环为：

```text
read32[40500000]=fb078820
uploaded ADID + patch + ext0 + FMAC = 408616 bytes
D80 patch config version=06090101 cfg=00177158 struct=00177c00 pairs=0017d318
START_APP
runtime VID:PID=a69c:8d81
firmware checkpoint=0
```

运行态配置描述符总长 222 字节，包含 3 个逻辑接口、8 个 interface descriptor
和 19 个 endpoint descriptor：

- interface 0，class `e0/01/01`：Bluetooth HCI control，interrupt IN `83`、
  bulk IN `84`、bulk OUT `04`；
- interface 1，class `e0/01/01`，alt 0..5：Bluetooth isochronous audio，
  endpoint `85/05`，包长随 alternate setting 变化；
- interface 2，class `ff/ff/ff`：AIC Wi-Fi vendor interface，bulk data
  `01/81`、bulk message `02/82`，maxpacket 512。

因此 D80 BootROM/固件阶段正式结束。下一阶段不得沿用报告中为兼容枚举而选择的
第一对 bulk `04/84` 作为 Wi-Fi 通道；必须显式绑定 interface 2，并分别保存
data `01/81` 与 message `02/82`。之后依次实现运行态 AIC command/event、Wi-Fi
netdev、扫描/关联/DHCP，再绑定 interface 0/1 的 Bluetooth HCI。
# v15 FCU760K runtime transport checkpoint

- Preserves the verified D80-U02 firmware upload and `a69c:8d81` runtime re-enumeration path.
- Separates runtime endpoint ownership: WLAN data `01/81`, WLAN messages `02/82`, Bluetooth event `83`, Bluetooth ACL `04/84`.
- Sends the official non-destructive `MM_VERSION_REQ` over the WLAN message endpoints and validates `MM_VERSION_CFM` before higher-level network binding.
- Hardware validated on Cubie A7Z: LMAC `06090101`, MAC-HW `0002fdfb/00014047`, PHY `5ee24111/01020000`, features `01e877d7`, 32 stations and 4 VIFs; checkpoint returned `0`.
- Build archive: `openvela-a733-stage1/Image.v15-fcu760k-runtime-transport`.
- Minimal SD image contains the same v15 kernel and all five verified firmware files.
# v16 FCU760K LMAC stack/identity checkpoint

- Builds on the hardware-verified v15 runtime transport without changing the firmware loader.
- Adds a reusable request/confirmation engine over WLAN message endpoints `02/82` with independent DATA toggles and asynchronous-message filtering.
- Sends the official D80 `MM_SET_STACK_START_REQ` and records 5 GHz/vendor capability confirmation.
- Reads and validates the module eFuse MAC through `MM_GET_MAC_ADDR_REQ` before any `wlan0` registration.
- Archive: `openvela-a733-stage1/Image.v16-fcu760k-lmac-stack`.
- Minimal SD image SHA256: `12dbcc3868e6f1f7f5d0f25a69a27983c366bcc06ab5e51ebc7bf492b3af8e3e`.
- Hardware verification pending; expected log markers are `LMAC stack start passed`, `efuse MAC`, and `LMAC stack checkpoint (0)`.

## v25 FCU760K station association control checkpoint

The preceding v24 image is hardware-verified through D80-U02 runtime startup,
RF calibration, CN 2.4/5 GHz channel configuration, station VIF creation,
MAC/PHY start and active wildcard scan.  Repeated scans may omit an individual
5 GHz BSS in one pass; firmware/result counters agree, so this is scan dwell
variation rather than a host-side truncation or loss of 5 GHz capability.

v25 adds the next official full-MAC control-plane boundary:

- scan results retain the beacon privacy bit and exact RSN/legacy WPA IE;
- each BSS reports `security=` and `assoc-ie=` for safe target selection;
- `SM_CONNECT_REQ`, `SM_CONNECT_CFM` and `SM_CONNECT_IND` follow the official
  AIC8800 D80 ABI, including the 320-byte aligned request layout;
- protected BSS requests set `CONTROL_PORT_HOST | WPA_WPA2_IN_USE`, preserve
  the RSN/WPA bytes, and use network-order EAPOL ethertype `0x888e`;
- `SM_DISCONNECT_REQ/CFM` is available as an independent recovery command;
- association state, firmware CFM status, IEEE 802.11 status code, VIF/AP/
  channel indexes and BSSID are exposed through `/dev/a733-wifi`.

This checkpoint intentionally does not accept or store a passphrase.  A
successful protected-BSS association proves authentication/association only;
the controlled port remains closed until the following host netdev, EAPOL,
key-installation and DHCP stages are complete.

Board test sequence (always use the BSS index printed by the latest scan):

```text
echo scan > /dev/a733-wifi
cat /dev/a733-wifi
echo associate=<bss-index> > /dev/a733-wifi
cat /dev/a733-wifi
dmesg
echo disconnect > /dev/a733-wifi
```

Success is `association checkpoint=0`, `cfm=0`, `status=0` and
`associated=1`.  Any negative echo result must be accompanied by the complete
`cat /dev/a733-wifi` and `dmesg` output; the saved 802.11 status code determines
whether the next correction belongs to request ABI, authentication policy or
the later EAPOL stage.

- Archive: `openvela-a733-stage1/Image.v25-fcu760k-association-control`
- Kernel SHA-256: `f66424da881c884806f2e6d7b0c778e2e3f48e440df77225982e49e928b636cf`
- Minimal SD image SHA-256: `0a1d18fc73b0e958610aa85cc27905e91468786389f3e7cb848d4c5e7ee63ccd`
- Full build, GPT check, ext4/FAT checks, embedded-kernel comparison and all
  five official firmware comparisons passed offline.

## v26 FCU760K 5 GHz reassociation fix

Hardware testing of v25 proved that a direct 2.4 GHz association succeeds.
The subsequent immediate 5 GHz attempt was accepted by `SM_CONNECT_CFM`, but
USB1 lost its port-enable bit before `SM_CONNECT_IND`. This was a host state
transition bug, not evidence of missing 5 GHz support or an AP password error.

v26 aligns the transition with the official FCU760K driver:

- `sm_connect_req.chan.tx_power` remains zero exactly as in the vendor
  `zalloc`-based request instead of carrying a regulatory-maximum override;
- disconnect now waits for both `SM_DISCONNECT_CFM` and asynchronous
  `SM_DISCONNECT_IND`, in either arrival order;
- the command endpoint gets the vendor-equivalent 100 ms settling window after
  firmware confirms that the old link has gone away;
- `/dev/a733-wifi` reports `disconnect: checkpoint/cfm/ind/reason`, so a
  new-band join is not attempted on an ambiguous old VIF state.

Regression sequence (reboot before testing because v25 left USB1 disabled):

```text
echo scan > /dev/a733-wifi
cat /dev/a733-wifi
echo associate=<2.4G-index> > /dev/a733-wifi
cat /dev/a733-wifi
echo disconnect > /dev/a733-wifi
cat /dev/a733-wifi
echo associate=<5G-index> > /dev/a733-wifi
cat /dev/a733-wifi
dmesg
```

Do not reuse old scan indexes blindly. Success requires disconnect
`checkpoint=0 cfm=1 ind=1` followed by 5 GHz association
`checkpoint=0 cfm=0 status=0 associated=1`.

- Archive: `openvela-a733-stage1/Image.v26-fcu760k-5g-association-fix`
- Kernel SHA-256: `fde0afda86f68398aaae7740115bf5cf6cbfecfb43afc4cea5d1d9c6c690bb9a`
- Minimal SD image SHA-256: `c3ee23a3b90e64a03c7d3eed55a818d1ccaea1e7c1332a8b69a5e4e5f9f17173`
- Build, GPT, ext4, FAT, embedded-kernel and five firmware comparisons all
  passed offline.

## v27 FCU760K association state guard

The v26 hardware log proves its first 2.4 GHz association succeeded with
`checkpoint=0 cfm=0 status=0 associated=1`. USB1 was disabled only after a
second `associate=1` was sent to the already-associated VIF, before the first
disconnect command. Consequently, the later disconnect and 5 GHz attempts
could not communicate with firmware.

v27 makes association control state-safe:

- repeating `associate=<current-index>` is idempotent and does not send a
  second `SM_CONNECT_REQ`;
- selecting a different BSS while associated automatically performs the full
  CFM/IND disconnect handshake before the new connect request;
- the v26 exact vendor connect layout and disconnect settling fix remain.

After reboot, the shortest 5 GHz validation is:

```text
echo scan > /dev/a733-wifi
cat /dev/a733-wifi
echo associate=<5G-index> > /dev/a733-wifi
cat /dev/a733-wifi
dmesg
```

The transition guard can then be tested by selecting the current 2.4 GHz
index and immediately selecting the current 5 GHz index; no manual disconnect
is required in v27.

- Archive: `openvela-a733-stage1/Image.v27-fcu760k-association-state-guard`
- Kernel SHA-256: `f8489f732dfdd5b1337d531b0e14d63b00d895c4a5254c747b46645752526259`
- Minimal SD image SHA-256: `c1833ccd36565946004eba20cd294610b8d698c97c2b0787654b4b6745ab676e`
- Full offline image verification passed.

## v28 FCU760K WLAN data endpoint and EAPOL checkpoint

The protected-BSS disconnect reason 15 observed after v27 is the expected
four-way-handshake timeout: association control succeeds, but the host had not
yet consumed the independent WLAN data endpoint.  The official D80 USB driver
confirms that runtime endpoints `01/81` carry WLAN data and `02/82` carry LMAC
messages.  It also confirms the receive ABI: a 60-byte `hw_rxhdr` followed by
an 802.11 frame and RFC1042 LLC/SNAP payload.

v28 adds a bounded, synchronous data-plane checkpoint without introducing a
concurrent USB worker before the polling EHCI transport is serialized. Run:

```text
echo scan > /dev/a733-wifi
cat /dev/a733-wifi
echo datacheck=<bss-index> > /dev/a733-wifi
cat /dev/a733-wifi
dmesg
```

`datacheck` associates to the selected BSS, immediately reads endpoint `81`,
validates the official packet length, decodes ToDS/FromDS addresses, searches
the bounded 802.11 prefix for RFC1042 LLC/SNAP and records the EtherType. The
success criterion is `data: checkpoint=0`, `ethertype=888e`, a non-zero
`eapol` count, and a `WLAN data endpoint passed` log.

This proves that WPA EAPOL Message 1 reaches openvela. It does not yet claim
that the WPA four-way handshake, CCMP key installation or IP networking is
complete; those form the next implementation slice.

- Archive: `openvela-a733-stage1/Image.v28-fcu760k-wlan-data-eapol`
- Kernel SHA-256: `19aaa9dc1fe64b86f732c0d655d3a21a01b38a85de29cd6b2e6df1364c12b6b6`
- Minimal SD image SHA-256: `d077085d4515c530b7c1f514d93045eef20c425cae8247bb616de95e46438107`
- Full offline image verification passed.

## v29 FCU760K WLAN TX and EAPOL-Start checkpoint

The v28 hardware log is a complete RX success: `actual=193` equals the
official `60-byte hw_rxhdr + 133-byte packet`, `fc=0288` is a From-DS QoS data
frame, the BSSID/station addresses are correct, and EtherType `888e` proves
that WPA EAPOL Message 1 arrived on endpoint `81`.

v29 adds the matching transmit path using the official build's
`CONFIG_USB_TX_AGGR=n` layout: four-byte USB data header, 28-byte full-MAC
`hostdesc`, then Ethernet payload without a duplicated Ethernet header. The
checkpoint sends the harmless IEEE 802.1X EAPOL-Start frame and requires a
new Message 1 from the AP, proving endpoint `01` TX and endpoint `81` RX.

```text
echo scan > /dev/a733-wifi
cat /dev/a733-wifi
echo txcheck=<bss-index> > /dev/a733-wifi
cat /dev/a733-wifi
dmesg
```

Success requires `data-tx: checkpoint=0`, `actual=36`, `frames=1`, and the
`WLAN TX endpoint passed` log. The command is intentionally key-free and
cannot complete or corrupt a WPA session.

- Archive: `openvela-a733-stage1/Image.v29-fcu760k-wlan-tx-eapol-start`
- Kernel SHA-256: `6d35fa8bf41cc2ec37d29d1cddfd99f241658e1931e1b9ecb54b695598287454`
- Minimal SD image SHA-256: `9eef240ce0c7da6cfb0edf797ac729d1846d07396bf60fd65eb993f0ef0eeb25`
- Full offline image verification passed.

## v30 FCU760K serialized USB transport and `wlan0`

The v29 board log completes the bidirectional hardware proof. Endpoint `81`
delivered WPA EAPOL Message 1, endpoint `01` accepted the exact 36-byte
non-aggregated EAPOL-Start transfer, and the AP returned a second Message 1.
Therefore the USB transport and official D80 packet ABI are no longer open
questions.

v30 converts those bounded checkpoints into the first persistent NuttX
network binding:

- one mutex serializes the shared EHCI queue-head and qTD state across control,
  LMAC-message, WLAN RX and WLAN TX traffic;
- a dedicated `a733-wlan-rx` kernel thread polls endpoint `81` with a short
  timeout instead of blocking NSH;
- received D80 `hw_rxhdr + 802.11 + LLC/SNAP` records are converted to Ethernet
  frames and dispatched to NuttX ARP/IPv4/IPv6 input;
- NuttX outbound Ethernet frames are converted to the official
  `usb4 + hostdesc28 + Ethernet-payload` D80 format;
- the FCU760K MAC is registered as `wlan0`, with carrier following firmware
  association state;
- the defconfig now contains the real NuttX IPv4, ARP, ICMP, TCP and UDP stack.

The fresh build directory is `cmake_out/cubie-a7z_nsh_v30`; it is intentionally
separate from v8 so stale configuration cannot hide missing network symbols.
On hardware, first validate registration without attempting WPA yet:

```text
ifconfig
ps
cat /dev/a733-wifi
echo scan > /dev/a733-wifi
cat /dev/a733-wifi
```

Expected results are a `wlan0` interface, an `a733-wlan-rx` task, and
`host-netdev=wlan0-rx-worker`. WPA2 four-way handshake, CCMP key installation
and DHCP intentionally remain the next phase; v30 does not claim IP
connectivity yet.

- Archive: `openvela-a733-stage1/Image.v30-fcu760k-wlan0-rx-worker`
- Kernel SHA-256: `fda8f9648fa429ec0e322c0683130872872198bcb9ce374805270abc108d134e`
- Minimal SD image SHA-256: `d7c9d05e9642ca70996bee17d02af0ad344e7f592d6ef9ed6f511c981ce70dfd`
- Fresh v30 compile and full offline image verification passed.

### v30.1 scheduler-marker cleanup

The first v30 hardware boot exposed a diagnostic-only regression: the new RX
thread sleeps periodically, and five old A733 bring-up markers in the generic
ARM64 syscall/context-switch path printed `C0` through `C4` on every sleep.
This was not a WLAN receive loop or a reboot, but it flooded the console.
v30.1 removes those syscall hot-path markers while retaining the serialized
RX worker and all v30 network functionality.

- Archive: `openvela-a733-stage1/Image.v30.1-fcu760k-wlan0-console-fix`
- Kernel SHA-256: `5d930cbad54c3ba994281face1395e2693cef1a703517f253a202a980b4e8ea3`
- Minimal SD image SHA-256: `042fe5bb291761a094a565b57164ae70376da154b2d08c9edfcb7a50f3259c64`
- Incremental compile and full offline image verification passed.

## v31 FCU760K host WPA2-PSK/CCMP four-way handshake

The v30.1 hardware log completes the persistent transport boundary: `wlan0`
is registered, the serialized `a733-wlan-rx` worker remains alive, scans run
while the worker is active, and the old scheduler markers no longer flood the
console.

v31 implements the protected-station step in the A733 driver itself, following
the official AIC full-MAC message ABI:

- PBKDF2-HMAC-SHA1 derives the 256-bit PMK from the WPA2 passphrase and SSID;
- a fresh SNonce and the WPA pairwise-key-expansion PRF derive the 48-byte PTK;
- received EAPOL-Key M1 produces M2 with the scanned AP's association RSN IE;
- M3 replay counter and HMAC-SHA1 MIC are verified before any key is accepted;
- RFC 3394 AES unwrap (using the public-domain Rijndael primitive) extracts the
  GTK KDE;
- official `MM_KEY_ADD_REQ/CFM` messages install CCMP PTK and GTK;
- M4 is transmitted before `ME_SET_CONTROL_PORT_REQ/CFM` opens the controlled
  port;
- passphrase working storage is erased immediately after PMK derivation, and
  PMK/PTK/SNonce are erased on disconnect.

The fresh build directory is `cmake_out/cubie-a7z_nsh_v31`.  This stage is
compiled and fully packaged, but the four-way handshake still requires the
following hardware acceptance test.  Use a temporary hotspot password, or
redact the command containing the passphrase before sharing a serial log:

```text
echo scan > /dev/a733-wifi
cat /dev/a733-wifi
echo wpa2=<bss-index>,<temporary-passphrase> > /dev/a733-wifi
sleep 8
cat /dev/a733-wifi
dmesg
ifconfig
```

Success requires all of the following:

- `wpa: checkpoint=0 state=complete`;
- non-zero M1/M3 counters and zero MIC failures;
- `ptk=1 gtk=1 port=1`;
- the log `WPA2 four-way handshake passed; CCMP PTK/GTK installed and
  controlled port open`;
- `wlan0` remains UP without a reason-15 disconnect.

Do not judge v31 by DHCP or `ping`: automatic IPv4 configuration is the next
stage after this cryptographic acceptance test.

- Archive: `openvela-a733-stage1/Image.v31-fcu760k-wpa2-ccmp`
- Kernel SHA-256: `d5d3bf5b2e3a487eada2ab694341f5c96c529ab2d5b9b7073e7c200dab6de5b2`
- Minimal SD image SHA-256: `24d12f040e5093e37b05458edd7083b3be8b03c8967d84c3f6ecb209c266d11f`
- Fresh compile, symbol inspection and full offline image verification passed.

### v31.1 WPA2/WPA3 transition-BSS RSN selection fix

The first v31 hardware run associated successfully and received three EAPOL
frames, but rejected every first EAPOL-Key frame with `-EPROTO` before M1 was
counted.  The target Xiaomi BSS advertised a 54-byte transition-mode security
IE.  v31 had incorrectly copied the AP beacon's complete RSN/WPA advertisement
into `SM_CONNECT_REQ`; the official Linux AIC path instead forwards the
station/supplicant-generated association IE (`sme->ie`).  Reflecting an AP IE
that advertises both PSK and SAE can negotiate a WPA3 descriptor while the
current openvela host state machine implements WPA2-PSK only.

v31.1 now parses the advertised RSN IE and constructs a 22-byte station IE
that explicitly selects CCMP group/pairwise encryption and the PSK AKM.  The
same selected IE is sent in EAPOL M2, so association and key confirmation
cannot disagree.  SAE-only, TKIP or PMF-required networks fail explicitly
instead of entering a misleading partial handshake.  A repeated `wpa2=`
request also performs a full disconnect/reassociation, ensuring the new RSN
choice is applied.  Non-secret EAPOL version/type/descriptor/key-info fields
are retained in `/dev/a733-wifi` and logged on protocol rejection.

Hardware acceptance remains:

```text
echo scan > /dev/a733-wifi
cat /dev/a733-wifi
echo wpa2=<bss-index>,<temporary-passphrase> > /dev/a733-wifi
sleep 8
cat /dev/a733-wifi
dmesg
```

Expected: `rsn-ie=22`, `checkpoint=0`, M1/M3 non-zero, zero MIC failures and
`ptk=1 gtk=1 port=1`.  If it still fails, preserve the new `eapol:` status line
and `unsupported/malformed EAPOL-Key` log; neither contains the passphrase.

- Archive: `openvela-a733-stage1/Image.v31.1-fcu760k-wpa2-rsn-select`
- Kernel SHA-256: `0a61f149654ef88080422843970227744e745d76c511895f3b1455654a9b318e`
- Minimal SD image SHA-256: `ab9f6338efadec7298513fc54cf932063e25bbb99309e3dda0ddb36ec16778aa`
- Incremental WSL compile and full GPT/ext4/FAT/firmware offline verification
  passed.

### v31.2 accumulated 5 GHz scan and mixed-group-cipher support

The v31.1 hardware log showed that 5 GHz remained enabled (`5g=1`, CN table
14/25) and even found a 5240 MHz BSS, but the target 5745 MHz result was
intermittent. Each scan previously erased the last result set. v31.2 keeps a
bounded BSSID union across scans and adds `scan5`, which sends a dedicated
13-channel 5 GHz request. Use repeated `scan5` commands until the target is in
the cached list; its index then remains stable.

The same log proved that the Xiaomi BSS was rejected before association with
`-ENOTSUP`, not during EAPOL. Its 54-byte mixed security advertisement selects
CCMP for unicast but may retain TKIP for the broadcast group key. v31.2 accepts
this valid WPA2 combination, unwraps the full 32-byte TKIP GTK and installs it
with the official `MAC_CIPHER_TKIP` value while the pairwise PTK remains CCMP.
Pure CCMP group keys still use the existing 16-byte path.

```text
echo scan5 > /dev/a733-wifi
echo scan5 > /dev/a733-wifi
cat /dev/a733-wifi
echo wpa2=<current-index>,<temporary-passphrase> > /dev/a733-wifi
sleep 8
cat /dev/a733-wifi
dmesg
```

Expected status includes `rsn-ie=22 group=TKIP/32` (or `CCMP/16`) followed by
M1/M3, `checkpoint=0`, `ptk=1 gtk=1 port=1`.

- Archive: `openvela-a733-stage1/Image.v31.2-fcu760k-scan5-tkip-group`
- Kernel SHA-256: `2a74acde8a4d51cb67acd7c38d0d2afb0ca692a373732ae2d9a9f73cfe79d104`
- Minimal SD image SHA-256: `2ccb15ff2835464cc7c58ea0b93518a06d951a90328733c601992b48ad40446e`
- WSL compile and complete GPT/ext4/FAT/AIC-firmware verification passed.

## v34 A733 VIP2 NPU hardware checkpoint

The official A733 product configuration and Linux 5.15 device tree were audited
before implementation. The active NPU driver family is `AW_NNA_VIP` with
`NNA_VIP2`; galcore and VIP1 are not the correct source branches for this board.

The openvela board now performs a bounded, non-blocking NPU hardware checkpoint:

- powers PCK600 domain 4 using the official delay values and a 10 ms timeout;
- releases the NPU core/AXI/AHB resets and enables protected AHB/MBUS gates;
- selects `pll-peri0-600m`, sets the NPU clock to the official 600 MHz / 800 mV
  OPP, and does not change regulator voltage or enable DVFS;
- reads the VIP2 identity registers at offsets `0x20`, `0x24`, `0x28`, and
  `0x30` from the NPU base `0x03600000`;
- registers read-only `/dev/a733-npu` even on failure so the exact power, clock,
  reset and identity snapshot remains available;
- preserves NSH startup and all existing SD, FAT, Wi-Fi, temperature, LED and
  watchdog initialization if the NPU is absent or times out.

Board validation:

```text
dmesg
ls /dev
cat /dev/a733-npu
free
dd if=/dev/mmcsd0 of=/dev/null bs=65536 count=16
sensortest -n 3 temp0
```

Pass criteria are: NSH remains responsive, `/dev/a733-npu` exists,
`checkpoint=0`, power domain state is on, clock/reset gates are enabled, and the
four VIP identity values are not all zero or all `ffffffff`. Preserve the full
`cat /dev/a733-npu` and `dmesg` output if any criterion fails; the status node is
designed to make the next register-level correction possible without another
diagnostic firmware.

- Archive: `openvela-a733-stage1/Image.v34-a733-vip2-hw-checkpoint`
- Kernel SHA-256: `ec5864c5d2f12672250574b1ab8ccdffb16b29aad982e2f1103d74f4c4b636`
- Minimal SD image SHA-256:
  `01ed1a24e3e2eb4baafbaea93a2b22051676075b1632d0026d60d01f90b5a013`
- Fresh 1443-step WSL build passed; the packaged kernel hash matches the build
  output and `e2fsck -fn` passed all filesystem phases.

### v34.1 CCU base correction

The first v34 board log showed PCK600 domain 4 on, but every CCU snapshot and
VIP identity register remained zero. The official `sun60iw2p1.dtsi` places the
main A733 CCU at `0x02002000`; v34 had incorrectly used `0x02001000`. v34.1
corrects only this physical base and retains the verified official NPU offsets,
gate bits, reset bits, 600 MHz parent selection and bounded power sequence.

- Archive: `openvela-a733-stage1/Image.v34.1-a733-npu-ccu-base-fix`
- Kernel SHA-256: `4831fe65a4e138406ee9ee4047f7338e577ac4344ddbf1d70cf63873f663940d`
- Minimal SD image SHA-256:
  `d237703b4d2a11db4bcc134e31f9c91ae4255d2cd2a076752d4082af450de9fe`
- Incremental WSL build passed. Full GPT, ext4, FAT, embedded-kernel and all
  five FCU760K firmware comparisons passed.

## v35 A733 VIP2 bounded reset and IRQ attachment

v34.1 passed on real hardware with identity
`00009000/00009202/20230518/1000003b`, NPU clock `82000000`, BGR `000f0001`,
and checkpoint 0. v35 therefore advances to the official VIP2 reset and IRQ
boundary without submitting an unknown command buffer.

The new checkpoint performs the official bounded software-reset sequence,
requires two consecutive valid observations, validates all-module idle
`7fffffff`, reset control bits `30000`, and disabled MMU state. It then drains
the read-to-clear IRQ ACK at `0x10`, attaches NuttX level-high IRQ 97, enables
the official event mask at `0x14`, and exposes initial/last ACK and interrupt
count in `/dev/a733-npu`. Actual interrupt delivery is intentionally deferred
until the first known-safe command is submitted.

- Archive: `openvela-a733-stage1/Image.v35-a733-vip2-irq-reset`
- Kernel SHA-256: `f50036ada624ae9643bd9bb913a44dd5191c593b49a36b001d3963e383e556c1`
- Minimal SD image SHA-256:
  `2555f3e83518c6d57c07c969954014d8205fc878d7a86c07d82d4f89de16c244`
- WSL build and complete GPT/ext4/FAT/kernel/FCU760K verification passed.

## v36 A733 VIP2 low32 coherent arena

v35 passed on hardware: bounded reset, all-module idle, disabled MMU, clean ACK
and IRQ 97 attachment all reported checkpoint 0. v36 reserves a 2 MiB,
page-aligned NPU arena in the identity-mapped stage-1 BSS rather than allocating
from the 4 GiB split heap, whose second region can exceed the NPU's 32-bit DMA
view.

The allocator enforces at least 64-byte alignment, rounds payloads to whole
cache lines, adds independent head/tail guards, rejects physical truncation,
and provides ARM64 clean/invalidate operations. Boot performs a 64 KiB
deterministic cache/guard self-test, then rewinds the arena for future VIP2 MMU
tables and command buffers. Link output places the arena at
`4028a000..4048a000` and the complete image ends at `404a2000`, safely below
the `48000000` BL31 carveout.

- Archive: `openvela-a733-stage1/Image.v36-a733-vip2-low32-arena`
- Kernel SHA-256: `48b6c66c5dca963a3b15a2b545fbbe29adb386c60b14a8e15006f04bf3cc0715`
- Minimal SD image SHA-256:
  `d178e2138dbf562e8eb23a2ac6ac336adb9a747310806e8ad173a8324add3e6e`
- WSL build and full GPT/ext4/FAT/kernel/FCU760K verification passed.

## v37 A733 VIP2 EVENT+END command and IRQ delivery self-test

v37 uses the exact minimum stream from the official VIP2 driver:
`EVENT(08010e01,00000044)` followed by `END(10000000,00000000)`, submitted
through official registers `0x654/0x3a4`. It is intentionally manual, so boot
still reaches NSH without executing an NPU command.

`echo submit > /dev/a733-npu` allocates one guarded low-32 command buffer,
performs ARM64 cache clean, keeps the VIP MMU disabled, and waits at most
100 ms for a real IRQ 97 increment. It then validates event bit 4, final
`7fffffff` idle and all arena guards. Failure disables IRQ sources, drains ACK,
runs the proven bounded soft reset and restores the IRQ mask.

Board test:

```text
cat /dev/a733-npu
echo submit > /dev/a733-npu
cat /dev/a733-npu
dmesg
free
dd if=/dev/mmcsd0 of=/dev/null bs=65536 count=16
```

Success requires `submit: checkpoint=0`, an increased IRQ count, IRQ `value`
containing `00000010`, `idle=7fffffff`, `guards=0`, and `recovery=0`.

- Archive: `openvela-a733-stage1/Image.v37-a733-vip2-event-irq-selftest`
- Kernel SHA-256: `17015d5309452c1f9bb1928263ddf7342e27f8e954b03a0733b27f043035d7df`
- Minimal SD image SHA-256:
  `49cf7bcbd547fcc95d38d846e793b12f2e9b37ac92541138614d4b5b1b4dd3f4`
- WSL build and full GPT/ext4/FAT/embedded-kernel/five-firmware verification
  passed. Arena remains `4028a000..4048a000`; image end is `404a2000`, below
  the `48000000` carveout.

## v38 A733 VIP2 Page-Descriptor MMU translated command fetch

v37 passed on real hardware, including the EVENT IRQ, final idle state and
low32 command-buffer guards. v38 now adds a manual Page-Descriptor MMU test
using the official `mmu_pd_mode=1` sequence for the observed
`CID/PID=0x1000003b`.

The test allocates guarded MTLB, 4K-STLB and command buffers from the low32
arena, maps VIP virtual address `0x01000000` to the physical command page,
enables MMU control `0x21`, and fetches the known-good EVENT+END stream through
that translated address. It rejects MMU-fault IRQ bit `0x40000000`, bounds all
waits, checks all guards and always soft-resets back to the v37 MMU-disabled
state. Boot itself remains command-free.

Board validation:

```text
cat /dev/a733-npu
echo submit > /dev/a733-npu
echo mmutest > /dev/a733-npu
cat /dev/a733-npu
dmesg
free
dd if=/dev/mmcsd0 of=/dev/null bs=65536 count=16
```

Acceptance requires `mmutest: checkpoint=0`, `control=00000021`, distinct
physical and virtual command addresses, an increased IRQ count,
`value=00000010`, `idle=7fffffff`, `guards=0`, and `recovery=0`. Compilation,
packaging and offline filesystem verification are complete; real-hardware MMU
acceptance remains pending until this log is captured.

- Rejected archive: `openvela-a733-stage1/Image.v38-failed-stlb-alignment-do-not-use`
- Kernel SHA-256: `973f2404f6370ba197756a159b301adcf5cc2632adbf5beea6661a6a3590243c`
- Minimal SD image SHA-256:
  `0269037c278aa4c4cc1ebf08aa9c13850f03f790459b5140948800f069da5212`
- WSL build and full GPT/ext4/FAT/embedded-kernel/five-firmware verification
  passed. The arena begins at `0x4028b000`, safely below the
  `0x48000000` BL31 carveout.

## v38.1 VIP2 STLB physical-alignment correction

The first v38 board run did not touch the MMU registers or submit a translated
command. It returned `-EFAULT` during preflight because the allocated STLB was
`0x4028f000`, which is not 16 KiB aligned. MTLB, command guards, direct v37
submit, NSH, SD and Wi-Fi remained healthy; this was an allocator arithmetic
bug rather than an NPU/MMU hardware failure.

The allocator previously aligned only the offset within a 4 KiB-aligned arena.
v38.1 aligns the actual CPU/DMA address, converts it back to an arena offset,
and adds a bounds check before address formation. The failed v38 binary is
retained only as `Image.v38-failed-stlb-alignment-do-not-use` so it cannot be
mistaken for the board-test candidate.

- Archive: `openvela-a733-stage1/Image.v38.1-a733-vip2-pdmmu-align-fix`
- Kernel SHA-256: `14adb56fa28c979a94d60464b61dd59b0c4409df409af51f472e9f13a0cb8169`
- Minimal SD image SHA-256:
  `4b97d8b99f303f3bf6d5f79203411c519e692c553a41c81aeb171a14a747185f`
- WSL build, GPT, ext4, FAT, embedded-kernel and all five FCU760K firmware
  checks passed.

Real-hardware validation also passed. The board reported MTLB `4028c000`,
16-KiB-aligned STLB `40290000`, physical command page `40295000`, VIP virtual
address `01000000`, PD entry `004028c1` and MMU control `00000021`. The
translated EVENT+END command increased IRQ count from 1 to 2 with ACK
`00000010`, final idle `7fffffff`, guards 0 and recovery 0. SD regression and
the 4-GiB heap remained healthy. The VIP2 Page-Descriptor MMU stage is now
complete; the next stage is the `/dev/npu0` runtime API and deterministic
operator execution.

## v39 A733 `/dev/npu0` first-stage runtime ABI candidate

v39 registers `/dev/npu0` after the proven v38.1 hardware checkpoint. Boot
allocates and maps a 1 MiB low32 driver-owned pool but does not submit work.
The public ABI provides per-open contexts, generation buffer handles, cache
clean/invalidate, translated submit, sync result, cancel, bounded reset,
device information and deterministic self-test operations. It deliberately
rejects arbitrary physical addresses and command streams without the proven
blocking EVENT 4 plus END contract.

Board validation:

```text
ls /dev
cat /dev/npu0
echo selftest > /dev/npu0
echo apitest > /dev/npu0
cat /dev/npu0
cat /dev/a733-npu
dmesg
free
dd if=/dev/mmcsd0 of=/dev/null bs=65536 count=16
```

Acceptance requires `/dev/npu0`, runtime `checkpoint=0`, `selftest=0`,
`apitest=0`, guards 0, no inflight task, at least one submitted/completed API
task, an event IRQ `00000010`, all-module idle `7fffffff`, zero live buffers
after `apitest`, and healthy NSH/SD/4-GiB memory. This is a runtime transport
checkpoint, not yet NBG/model inference.

- Public ABI: `nuttx/include/nuttx/npu/a733_vip2.h`
- Runtime: `vendor/allwinnertech/chips/a733/a733_npu_runtime.c`
- Archive: `openvela-a733-stage1/Image.v39-a733-npu0-runtime-abi`
- Kernel SHA-256: `900c3a16b682bacdecc2a4b8fed838c326515ea08ef481219e20fd375382c187`
- Minimal SD image SHA-256:
  `79fec7aa5d12bb4526078bdbf4c922762c7341b18b7ef51f2612f2a7fb2aaccf`
- WSL build and complete GPT/ext4/FAT/embedded-kernel/five-firmware checks
  passed. The low32 arena begins at `0x4028d000`, below the BL31 carveout.

Real-hardware acceptance passed. `/dev/npu0` used MTLB `4028e000`, aligned
STLB `40290000`, pool `40295000`, VIP base `02000000` and PD `004028e1`.
Both `selftest` and `apitest` returned 0; the API task completed with IRQ
`00000010`, idle `7fffffff`, guards 0, no timeout/cancel/inflight residue, and
all temporary buffers/pages were reclaimed. The 4-GiB heap and 1-MiB SDMMC
read regression remained healthy. v39 is the frozen runtime-transport baseline;
the next work item is official model/container parsing and CPU-vs-NPU numeric
validation of a deterministic operator.

## v40 A733 v3 NBG metadata probe candidate

The runtime ABI is now v2 and adds a bounded, streaming NBG probe without
claiming proprietary graph preparation or inference. `modelcheck=/data/X.nb`
validates the `VPMN` magic, v3 format value `00010020`, A733 target CID
`1000003b`, minimum header length and printable model name, while calculating
the full-file CRC32 in 512-byte chunks. Paths are confined to `/data` and the
model is not retained in RAM. The existing v39 allocation, cache, translated
submission, self-test and API-test paths are unchanged.

- Public ABI: `nuttx/include/nuttx/npu/a733_vip2.h` (ABI v2)
- Runtime: `vendor/allwinnertech/chips/a733/a733_npu_runtime.c`
- Optional user-model installer: `install-a733-nbg-model.sh`
- Archive: `openvela-a733-stage1/Image.v40-a733-v3-nbg-probe`
- Kernel SHA-256: `5df79b35e7422feb27341209c100cae0e335aed8ec550dc8c24ebc079039229a`
- Minimal SD image SHA-256:
  `28380d4433e76872d243f4f1cab09ea88903e02c510247c23eb2b02cd13c2beb`
- WSL compilation and full GPT/ext4/FAT/embedded-kernel/five-firmware
  verification passed.

The external SDK reference confirms the standard VIPLite sequence and a v3
Lenet probe tuple of `444152` bytes, CRC32 `77b1738e`, CID `1000003b`, and name
`lenet_uint8_NCHW`. Its headers expressly mark the implementation proprietary
and the repository has no license, so none of its libraries or model binaries
are bundled into the minimal image. Real-board model-probe acceptance is the
next gate; real inference remains pending an authorized prepare/link solution.

Real-hardware v40 acceptance passed. The board's NuttX raw CRC32 is
`40bb2d73`; the earlier `77b1738e` value is the ZIP/zlib CRC convention, not a
different file. Header, size, CID and model name all match the local reference,
and the runtime ABI tests, IRQ/MMU path, guards, SD and 4-GiB heap remained
healthy.

## v41 checked low32 model/input staging candidate

v41 advances the public runtime ABI to v3 and maps a dedicated 512-KiB low32
staging allocation at VIP VA `0x02100000`, immediately after the existing
1-MiB task pool. It adds `A733_NPUIOC_STAGE_FILE`, plus the text controls
`modelstage=/data/X.nb`, `inputstage=/data/X.dat`, and `stageclear`.

Model staging first reruns every v40 header/target/path check, streams the file
into NPU-visible memory, and independently verifies the staged byte count and
raw CRC. Input staging is 64-byte aligned after the model. The staging mapping
is separate from context-owned command buffers and cannot be submitted through
the current command ioctl; this deliberately prevents an unprepared NBG from
being executed before the proprietary address-patch/link step exists.

The minimal image now contains the authorized test fixtures as
`/data/lenet.nb` and `/data/lenet.dat`. Board validation is:

```text
ls /data
echo modelstage=/data/lenet.nb > /dev/npu0
echo inputstage=/data/lenet.dat > /dev/npu0
cat /dev/npu0
echo selftest > /dev/npu0
echo apitest > /dev/npu0
cat /dev/npu0
free
dd if=/dev/mmcsd0 of=/dev/null bs=65536 count=16
```

Acceptance requires ABI v3, staged-model `444152/40bb2d73`, staged-input
`784/e892cab0`, both status 0, distinct nonzero physical/VIP addresses,
`selftest=0`, `apitest=0`, guards 0 and healthy regressions.

- Archive: `openvela-a733-stage1/Image.v41-a733-npu-low32-staging`
- Kernel SHA-256:
  `db615cf446e6ed1e49cf5bb9cf6629eeeabbb9691f23b881b09ec4fe7f2c9edf`
- Minimal SD image SHA-256:
  `386559f7e5fe46248e54036696b8c953620ed06da2f795bcb6c6b72f49b2c1e6`
- Model SHA-256:
  `e5ecb51bdbea030e3a2c21af7f03df702b0e89e55b15fb9115a054382077136b`
- Input SHA-256:
  `94914cb610401a7685447ec268c0e14d4c78b156997ab3fc8c44d27bd7221591`

WSL compilation and complete GPT/ext4/FAT/embedded-kernel/five-firmware/model/
input verification passed. `_image_end=0x404a6000` remains safely below the
`0x48000000` BL31 carveout.

Real-hardware v41 acceptance passed. The staged model is
`444152/40bb2d73` at physical/VIP `40396000/02100000`; the staged input is
`784/e892cab0` at aligned offset `0006c700` and physical/VIP
`40402700/0216c700`. Self-test and API-test both return 0, one task is submitted
and completed through IRQ `00000010`, idle is `7fffffff`, guards are intact,
and context/page/buffer accounting is clean. The 4-GiB heap, SDMMC regression
read and FCU760K boot path remain healthy.

The next gate is isolated to proprietary prepare/link semantics. The
`a733-viplite-golden/` tool calls the official VIPLite create/prepare/run API
for the same Lenet fixture and records tensor metadata, quantization, profiling
and binary golden outputs. This is a one-time evidence generator under the
official Debian VIP2 driver; it is not an openvela runtime dependency.

The source/binary characterization bundle builds successfully for AArch64 and
is archived as `a733-viplite-golden-bundle.tar.gz` (SHA-256
`c093241e568b5e3dd8086ab2d984c7ae503f5f24240bca280c7506c15b9c12bc`).
The archive now also carries the GLIBC-2.17-compatible AArch64
`libviptrace.bullseye.so`, its `viptrace.c` source and
`run-lenet-trace.sh`.  The observer interposes the exact `ioctl@GLIBC_2.17`
entry imported by the official HAL, records raw VIP2 ioctl payloads, and takes
bounded snapshots of live mappings at submit/wait without modifying the Linux
kernel driver.  Its `viptrace-lenet-*.tar.gz` result is the input to the next
prepared-resource importer step.
`analyze-viptrace.py` converts that archive into a deterministic JSON manifest
covering task properties, VIP addresses, snapshot hashes and memory changed
between submit and wait.  The observer layout is aligned with the official
`vpmdNBG_SIZE_OVER_4G=0` ABI, where `vip_size_t` is 32-bit.

The first real trace, `viptrace-lenet-20260828-120540.tar.gz`, validates the
control plane: task `0x80040000`, one subtask, command mem-id `352321547`, real
command size 1088, priority 0, timeout 20000 ms and need-schedule 1.  Five VIP
buffers were allocated at `0x01005000`, `0x02010000`, `0x01008000`,
`0x0100b000` and `0x0100e000`.  Their DMA VMAs carry `VM_IO/PFNMAP`, so the v1
observer's `process_vm_readv()` snapshots were rejected with `EFAULT` and are
empty.  The v2 observer adds a `write(2)`/`copy_from_user` fallback; one repeat
capture is required to obtain the command, weight and tensor bytes.
The official Bullseye image was verified to contain gcc and glibc 2.31, then
preloaded with `/root/a733-viplite-golden`. Its on-board script deliberately
rebuilds natively, avoiding the GLIBC_2.34 requirement of the newer WSL cross
toolchain. The first board attempt exposed that gcc was present but libc6-dev
headers were omitted. A Bullseye-sysroot prebuilt requiring only GLIBC_2.17 is
now included as `a733-viplite-golden.bullseye` (SHA-256
`5e4dfe18ad3d00d0dd8a26b3a3527ac14496c9d5455f4cf416d23fe64c02ac8f`),
and the run script prefers it without compiling. Model/input hashes and
executable permissions were read back from the image successfully.

The first native run then exposed an ELF loader detail: the executable's
`$ORIGIN/lib` DT_RUNPATH locates `libNBGlinker.so`, but RUNPATH is not
inherited while resolving its indirect `libVIPhal.so` dependency.  The run
script now exports its bundled `lib/` through `LD_LIBRARY_PATH`, preserves the
actual runner exit status instead of losing it through `tee`, and only hashes
outputs after a successful inference.

## Official VIPLite Lenet golden run passed

The one-time official Debian/VIPLite run has now passed on the real 4-GiB
Cubie A7Z.  VIPLite reports library version `0x00020003`, driver
`2.0.3.2-AW-2024-08-30` and hardware CID `0x1000003b`.  It prepared and ran
`lenet_uint8_NCHW` with five layers, one 28x28x1x1 UINT8/TF-asymmetric input
(`784` bytes, scale 1, zero point 0), and one ten-element FP16/unquantized
output (`20` bytes).  The input raw CRC32 is the expected `e892cab0`.

The repeated hardware run passed in 0.623 ms wall time; firmware profiling
reports 164 us and 64,285 cycles.  Output 0 has raw CRC32 `d50f5d79` and SHA-256
`4dccf1b724880df5d14916e9cfa1c5465332e5a76ddef6deb9a325c7526fd585`.
Its exact little-endian bytes are
`00 3c 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00`, which decode
as ten FP16 values `[1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0]`
and select class 0.  This is now the openvela element-by-element acceptance
fixture and closes the proprietary-Linux characterization gate.  The next
implementation step is the authorized prepared-resource importer/executor,
followed by real Lenet execution through the existing v41 MMU/IRQ runtime.

## v42 prepared-resource importer/executor built

The repeat archive `viptrace-lenet-20260413-072201.tar.gz` successfully
captured all five Linux VIPLite mappings.  It identifies a 1728-byte parameter
block at VIP `0x01005000`, 437760 bytes of prepared constants at
`0x02010000`, an 896-byte input allocation at `0x01008000`, a 128-byte output
allocation at `0x0100b000`, and a 1144-byte command allocation at
`0x0100e000`.  The actual 1088-byte command starts 56 bytes into that final
allocation, at `0x0100e038`.

openvela now reconstructs those fixed mappings in its existing VIP2 PD-MMU,
loads four resource files from `/data/npu`, checks exact size and CRC32,
poisons the output buffer, executes the captured hardware command, invalidates
the output cache and compares the first 20 bytes with the Linux golden output.
The control is `echo lenet > /dev/npu0`; `cat /dev/npu0` reports status, IRQ,
idle state, polls, changed-byte count, CRC and exact output bytes.  Generic
NBG preparation remains separate, but the model-specific execution path no
longer depends on Linux or its VIPLite libraries at runtime.

The first v42 hardware boot exposed a bounded-arena integration error before
`/dev/npu0` registration: a duplicate 428-KiB constants allocation exceeded
the existing 2-MiB low32 DMA arena.  The corrected build uses the already
linearly mapped runtime pool pages at VIP `0x02010000` and reserves them from
the public allocator.  This preserves every captured address without growing
the arena or duplicating weights.  Corrected kernel SHA-256 is
`09f3affd8c017dbe92169e3f772a459a7445a3f7fe3d852d92785030e5966850`;
minimal image SHA-256 is
`2bf6af5eea69e8d55d664ccde686b382052fb74dfe216dbf5c23594d492a4d6a`.
Real-board `/dev/npu0` registration and `lenet` execution are the remaining
acceptance gates for corrected v42.

The corrected-arena board run registered `/dev/npu0` and retained passing
MMU/IRQ self/API tests, but rejected the imported resources with `-EBADMSG`
before hardware submission.  The trace manifest hashes use conventional ZIP
CRC32, whereas NuttX `crc32part(..., 0)` and the existing model diagnostics
use the raw reflected polynomial state without initial/final XOR.  The four
expected raw CRCs are now `85f3779f`, `fae9ff62`, `b81ff05b` and `5f147d83`.
The golden output remains raw CRC `d50f5d79`.  This correction produced the
kernel/image hashes recorded above; the next board run can reach hardware
submission instead of stopping in the file loader.

## v43 official VIP2 init transaction built

The subsequent corrected-v42 board run reached and completed the real LeNet
hardware command (EVENT IRQ `0x10`, full idle, guards intact), but produced a
deterministic non-golden 20-byte result.  This proved the imported addresses,
PD-MMU, cache maintenance and command offset while exposing the one remaining
Linux dependency: `init_vip()` plus the one-time device init command.

The v43 openvela path now specializes that official sequence for A733 CID
`0x1000003b`, including the no-AXI-SRAM reorder-bypass setting and the
38-word NN/VIP-SRAM command.  Initialization and inference are submitted in a
single locked MMU session so the state is not erased by the normal recovery
reset.  Build, GPT, ext4, FAT, embedded-kernel comparison, Wi-Fi firmware and
all four prepared LeNet resource checks pass.

Kernel SHA-256: `ae07fcd07cf38e1f01b527cf1f3faf52db9c0c446cd81ee646bb484133929330`

Minimal image SHA-256: `833036d9b01882e3cbff1c57c9f649eb9d2cfbbca1d6c3833bc09660c15894dd`

Board acceptance command: `echo lenet > /dev/npu0`, followed by
`cat /dev/npu0`.  Expected output CRC is `d50f5d79` and exact output is
`003c000000000000000000000000000000000000`.

### v43 hardware acceptance passed

Real-board acceptance is complete.  The openvela executor produced the exact
Linux golden output and CRC (`d50f5d79`) with IRQ `00000010`, idle
`7fffffff`, one poll, 20 changed bytes and guards zero.  Running `selftest`
and `apitest` immediately afterward produced one successful generic task,
zero failures, zero timeouts and zero guard corruption.  This closes the
first real A733 neural-network inference milestone without Linux or VIPLite
shared libraries at runtime.  Generic proprietary NBG preparation remains a
separate future capability; v43 is now the stable regression baseline.

## v44 generic prepared-model package built

The first hardware-passed LeNet trace is now represented as one checked
`prepared-models/lenet.a7pm` image rather than only four model-specific
resource files.  `pack-a733-viptrace.py` extracts the prepared VIP address
layout and golden output from the trace manifest.  The openvela runtime adds
the `prepared=/data/...` control, bounded A7PM parser, raw CRC validation,
region-overlap and address-window checks, command/output validation, output
poisoning and post-run golden verification.  The fixed v43 LeNet executor is
retained for board regression.

The source builds cleanly under `-Werror`.  GPT, ext4 and FAT checks pass;
the source and embedded kernel match, as do the source and embedded A7PM
package.  This is a board-test candidate, not yet the stable baseline.

Kernel SHA-256: `3642f5be6976848b15648fb6a499602d57fca7b7d30156fdb30526a41fd2ed36`

Minimal image SHA-256: `658459b18d022563acaf4a4a4268ecf7e6dc2af2ae1d53b20bb787ffb747d902`

A7PM package SHA-256: `1e65fc966d5aabbda46f573e16c41c63e6ca3019d241feea12190b1cb38aa027`

Board acceptance command:

`echo prepared=/data/npu/lenet.a7pm > /dev/npu0`, then `cat /dev/npu0`.

Expected generic result: status 0, five regions, command `0100e038`, output
CRC `d50f5d79`, 20 changed output bytes, IRQ `00000010`, idle `7fffffff`, one
poll and guards zero.  A second converted/prepared model is still required to
prove that the package path is model-independent before beginning YOLO model
import.

### v44 hardware acceptance passed

Real-board evidence promoted v44 from candidate to stable baseline.  The A7PM
path reported status 0, model `lenet_uint8_NCH`, five mapped regions, command
`0100e038`, output CRC `d50f5d79`, 20 changed bytes, IRQ `00000010`, idle
`7fffffff`, one poll and guards zero.  The fixed LeNet regression path matched
the full golden output, while selftest/apitest completed successfully with no
timeout or cancellation.  The generic package mechanism is therefore proven
for its first model; a distinct prepared model is the remaining evidence
needed before broad model import and YOLO integration.
## v38 Ctrl+C、curl/HTTPS 与 SSH 初始化修复候选

在 v37 Wi-Fi 稳定性实板通过后，v38 增加了 NSH 控制终端 SIGINT，使
`Ctrl+C` 能中止前台命令；加入完整 curl/libcurl、mbedTLS HTTPS 和镜像内
CA 根证书包；同时禁止 libssh 在 NuttX 早期启动阶段运行静态构造函数，改为
板级初始化完成后的显式、可回滚、可重试初始化。初始化失败现在会报告具体
阶段编号。工程和镜像中不保存 Wi-Fi 密码、SSH 密码或 AI API 密钥。

候选镜像 SHA-256：
`827603705ea17508074814ec3e472545b0b813466f77fa6c151161b5be4c5f7e`

内核 SHA-256：
`d00367b242bcaa88eadecd3ddc55b3504bcd2366f5b6cb7c6659fa650c249a70`

CA 包 SHA-256：
`0f119da204025da7808273fab42ed8e030cafb5c7ea4e1deda4e75f066f528fb`

构建、GPT、ext4、FAT、嵌入内核和嵌入 CA 哈希均已通过；Ctrl+C、HTTPS、
外部 API POST 以及 SSH 服务端仍需实板验收。详见 `A733_CURL_SSH_V38.md`。

## v39 控制台 RX、同步 DNS、SSH 线程与 Wi-Fi 重试修复候选

v38 实板验证暴露出四个相互独立的问题：A7Z 使用的 `/dev/a7zconsole` 是裸轮询
字符设备，NSH 阻塞等待前台任务时无法读取 `Ctrl+C`；curl 的 POSIX 异步 DNS
无法创建 resolver 线程；libssh 初始化阶段 1 因 mbedTLS 未启用 pthread 后端而
失败；`wifi connect` 在第一次扫描漏掉目标 SSID 时立即返回 `ENOENT`。

v39 为板级控制台增加永久 UART RX 线程、256 字节环形缓冲、阻塞和非阻塞读取、
`TIOCSCTTY`/`TIOCNOTTY` 以及面向当前前台 PID 的 `SIGINT` 投递。curl 在 NuttX
改为同步 DNS；mbedTLS 启用 pthread 线程后端；Wi-Fi 连接增加最多三轮扫描重试。
全部源码在 `-Werror` 构建中通过。

候选镜像 SHA-256：
`3488d590f1a1ae8383af9691bcc6dcf1d2c3ac9f5734f28638bdc2a83720bf6f`

内核 SHA-256：
`a0737cfd3cc98b4dc08074c77a2b21deba1b46c7921cd2586a5772c8312e276c`

CA 包 SHA-256：
`0f119da204025da7808273fab42ed8e030cafb5c7ea4e1deda4e75f066f528fb`

构建、GPT、ext4、FAT、嵌入内核和 CA 包均已离线验证。Ctrl+C、HTTPS 与 SSH
服务端仍需实板验收；详见 `A733_CONSOLE_CURL_SSH_V39.md`。

## v40 控制终端 termios 与 A733 CE 硬件随机数修复候选

v39 实板日志确认了两个剩余根因。首先，`/dev/a7zconsole` 没有实现 `TCGETS`，
导致 `isatty()` 返回假，NSH 不会用 `TIOCSCTTY` 把前台任务交给 UART RX 线程，
所以 `Ctrl+C` 仍不能中止 ping。其次，系统没有 `/dev/random`，curl 的 mbedTLS
CTR_DRBG 无法播种，sshd 也无法生成 Ed25519 主机密钥。

v40 为控制台补齐最小 termios ioctl，使现有控制终端和 SIGINT 路径真正生效；
同时依据 A733 官方 CE v5 驱动实现硬件 TRNG，配置 CE 模块时钟、复位和 MBUS，
用官方任务描述符执行 RNG 命令，并在注册 `/dev/random`、`/dev/urandom` 前完成
启动连续性/均匀值自检。自检失败时安全地不提供随机设备，不会降级到固定或伪随机
密钥材料。

候选镜像 SHA-256：
`495926c84f798e4edc6d07d9b974951ece4557cd784002d4fae1e47f2cacb1a6`

内核 SHA-256：
`f3c9345cc8e750e1117eb652c1c334e2f97c1530f1ff00c31cb9a7b82dacc596`

CA 包 SHA-256：
`0f119da204025da7808273fab42ed8e030cafb5c7ea4e1deda4e75f066f528fb`

全量构建、最终符号检查、GPT、ext4、FAT、嵌入内核和 CA 包均已离线通过。
TRNG、Ctrl+C、HTTPS 和 SSH 仍需实板验收；详见
`A733_CONSOLE_ENTROPY_CURL_SSH_V40.md`。

## LAN/SSH v42：ARP 入站可达性修复候选

v41 实板已证明 Wi-Fi、DHCP、NTP、SSH 主机密钥、socket bind 和 listen 均
成功，但 Windows `192.168.31.228/24` 对开发板 `192.168.31.27` 没有 ARP
表项且 ping 不通，故 SSH 超时发生在 TCP/SSH 之前。

v42 在 DHCP 成功后发送三次 gratuitous ARP，新增 ARP/IPv4/TCP/SYN 入站
计数与最近端点诊断，启用 8 项 TCP backlog 和网络统计。全量 WSL 构建、GPT、
ext4、FAT、嵌入内核、CA 包与配置检查已通过。镜像不保存任何密码或密钥。

内核 SHA-256：
`5f36bae3f0d91b180e47206b78606acda2513e2ec3f62530c8f6136584536f94`

候选镜像 SHA-256：
`1302e3ac468bdca7373d7030f65c83670e464b619baff4cb9e785324187018c2`

下一门槛是实板确认 `/dev/a733-wifi` 中 `garp=3/0`，Windows ARP/ping 恢复，
然后验证 2222 端口和 SSH 登录。详见 `A733_LAN_SSH_V42.md`。

## LAN/SSH v43：标准 ARP Announcement 与逐方向诊断候选

Windows 与开发板已确认连接同一个 5 GHz AP/BSSID，Windows 路由无冲突，
但 Windows 没有开发板的 ARP 表项，故 v42 的 unsolicited ARP Reply 未能恢复
局域网入站可达性。v43 将 DHCP 后的三次公告改为 Request / Reply / Request，
其中 Request 使用 RFC 5227 风格的 ARP Announcement；同时新增 ARP 请求、
回复、最近操作/地址以及驱动发出的 ARP 帧计数。这样一次实板测试即可区分
“请求未到板端”和“板端已回复但被 AP/Windows 丢弃”。

内核 SHA-256：
`c28fddfa79e4859e1c654890f7d639e5720c5b94fac0971ef22e369c55e3cbf0`

候选镜像 SHA-256：
`cafb03e793cea20f37cab6e024dfd16225611762e58d8908591fe642308badb8`

全量 WSL 构建、GPT、ext4、FAT、内嵌内核、CA 包、配置与关键字符串检查均
通过。下一门槛是烧录 v43 后，在 Windows 发起 ping/SSH，再读取
`/dev/a733-wifi` 的 `req`、`tx-arp` 和 `syn`。详见 `A733_LAN_SSH_V43.md`。

## LAN/SSH v44：Ctrl+C 后监听端口清理候选

v43 实板已确认 Windows 能学习开发板 ARP，ping 0% 丢包且 TCP 2222 探测
成功，局域网入站路径因此通过。剩余故障是 Ctrl+C 停止 sshd 后 `ps` 中已无
服务任务，但重新绑定 2222 返回 `EADDRINUSE`。libssh 原本已启用
`SO_REUSEADDR`；实际根因是默认 SIGINT 绕过 `ssh_bind_free()`。

v44 为 sshd 加入 SIGINT/SIGTERM 的可控退出路径，使阻塞 accept 被中断后正常
释放 bind socket、完成 libssh 清理并清除密码缓冲。全量构建及镜像文件系统
校验均已通过。

内核 SHA-256：
`3d6f01ef996f8ed2d1a18bb5ec48942fd60637832ce81eaad22e8af4d6abef9a`

候选镜像 SHA-256：
`326ad5066d26cb4c4aa82f6c6b4399c5778e13286e5de212b4afe11fdb7a6c39`

下一门槛是实板直接 SSH 登录，并验证 Ctrl+C 后出现 socket released 提示且能
立即再次启动 sshd。详见 `A733_LAN_SSH_V44.md`。

## LAN/SSH v45：认证后交互式 NSH 候选

v44 实板的 Windows `ssh -vvv` 已确认 SSH 密钥交换、主机密钥、密码认证、
channel、PTY 和 shell request 全部成功；随后服务端输出
`nsh: sh: syntax error` 并关闭连接。根因是 Linux 示例采用 `sh -l`，而 NuttX
NSH 不支持登录 shell 参数 `-l`。

v45 将交互式 PTY 会话改为纯 `sh`，保留远程命令的 `sh -c`，并实现无 PTY
shell 的真实 NSH 进程与管道，同时补充 spawn 错误诊断。全量构建和镜像校验
均通过。

内核 SHA-256：
`5009450190e613ded059568e300413e434a9a3c7e43bcc50c747cf517dfa49cc`

候选镜像 SHA-256：
`5e4da1ba485c3c2a853fb789b53541696cad09f08e0f3a21db6a5217ebd97685`

下一门槛是实板登录后保留远程 `nsh>` 并执行基础命令。详见
`A733_LAN_SSH_V45.md`。
