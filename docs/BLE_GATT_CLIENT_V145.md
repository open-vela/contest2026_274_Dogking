# v145：A7Z 官方 GATT 智能家居客户端

后续实机更新（2026-10-15）：SSH下info/frame、非法参数/适配器OFF清理、八核/计时、
HCI probe、五轮BLE启停、扫描中退出重进和47包零丢失的轻负载Wi-Fi共存已测。
**GATT正向及ESP32-S3双板仍未测**。默认SSH会话出现报文损坏，临时显式AES-GCM
会话完成回归；完整限定结论与失败记录见 [实机报告](evidence/BLE_V145_SSH_RUNTIME_ACCEPTANCE.md)。

## 范围和恢复

遵照用户 2026-10-15“直接推进”，在 v144 五轮通过基线上新增产品层 `home` 命令。
**未修改 v144 的 HCI/USB、Zblue、bttool、Wi-Fi、I2S、显示和 LLM 实现。**
恢复分支 `backup-before-v145-gatt-client`、标签 `a733-ble-v144-lifecycle-5cycle-pass`
指向 `86df0c3`；旧镜像保留不覆盖。当前不开发 ESP32-S3 BSP、不改用户历史工程，
没有可烧写的 ESP32-S3 下位机固件；本次是 A7Z 一侧接入和验收入口。

`app/a733home/a733home_main.c` 调用官方 openvela Bluetooth 实例、GATT Client
发现/查询/Read/Write/Subscribe/Disconnect API。复用既有 `home_protocol.{h,c}`，
不自写 ATT/L2CAP、蓝牙 Host 或芯片驱动。
manifest 新增 linkfile `app/a733home → apps/system/a733home`，保持共享协议单一来源。
构建 wrapper 只暂时复制该应用目录，退出时恢复原目录/符号链接/不存在状态。

## 命令

```text
home info
home frame ping 1
home frame state 2
home frame ack-ping 1
home frame ack-state 2
home frame event-state 1
home probe AA:BB:CC:DD:EE:FF random
home monitor AA:BB:CC:DD:EE:FF random 60
```

`info` 和 `frame` 是离线入口，无服务、无下位机也能运行；frame 只打印报文，不发送。
请将示例地址替换为测试外设的真实地址，地址类型用 bttool ScanResult 的 AddrType：
0 → public，1 → random。本版不支持 RPA 解析/身份地址自动解析及任意自动选设备。

`probe` 自动执行：指定地址连接 → 发现约定服务 → 查找特征并检查属性 → 订阅通知 →
读取 Status 并验证协议 → 写 PING → ATT write 响应 **且**匹配的业务 ACK →
写 GET_STATE 并双重确认 → 断开并清理。任意一步失败返回非零，输出阶段和错误码。
`monitor` 在初次建立完整 profile 后观察 1..300 秒，每约 2 秒查询状态；确认断开时
最多三次重连同一地址，重新发现/订阅/读取，使用新 seq，不重放旧请求。
超时/CRC错误后不盲目重新发送。没有 SET_OUTPUT、继电器、舵机、电机或任意原始写入入口。

## 启动流程（不得省略）

在 NSH 执行 `ps`，仅当对应服务不存在时依次执行：

```text
kvdbd &
sleep 2
bluetoothd &
sleep 3
bttool
```

进入 bttool 后：

```text
pair auto 0
enable
state
```

等状态 2 后可扫描定位外设地址；取得地址后 `scan stop`，等 stopped 回调，再 quit。
**运行 home 前不要 disable**，因为 home 要复用已启用的官方服务。
home 不抢占另一 bttool 的扫描，也不修改全局适配器启停策略，不自动配对或接受配对。
同一时间只运行一个 home，不同时用 bttool 操控连接/关闭适配器。

## 测试外设约定

服务和特征采用已经归档的协议，不临时改 UUID：

| 用途 | UUID | 属性 |
| --- | --- | --- |
| Service | `a7330001-6b42-4f65-8d37-9b184b771001` | Primary |
| Command | `a7330002-6b42-4f65-8d37-9b184b771001` | Write with response |
| Status | `a7330003-6b42-4f65-8d37-9b184b771001` | Read + Notify，CCCD |

默认 ATT MTU=23 即可，单帧最大 20 bytes。外设先不接任何物理执行器。
Status 初始 Read 返回合法 HOME_EVENT/GET_STATE（可用 `home frame event-state 1` 获取）。
Command 收到 request 后，以相同 seq/opcode 在 Status 发 HOME_ACK，payload[0]=0 表示成功。
可先用支持自定义服务的 GATT 测试外设验证；仅有随机广播设备不能运行成功 probe。
最终 ESP32-S3 服务端必须验证 CRC、白名单、权限、重复 seq 和动作边界。
CRC/地址指定不是认证；生产控制必须有配对/加密、授权和失联安全态。本版验收命令
仅发送无动作 PING/GET_STATE，不会绕过或自动降级外设的加密权限。

## 线程、超时和诊断

- 使用官方 `bluetooth_set_external_uv`：所有应用 GATT 回调由当前命令线程 pump，
  回调内仅校验数据/更新状态，不同步查询 SDK，避免耗尽 libuv 单 worker。
- connect/discover/subscribe/read/业务 ACK 的**回调等待**使用 CLOCK_MONOTONIC，
  每阶段 15 秒；disconnect/cancel 5 秒。监视时长不是所有阶段合计的硬上限。
- **官方同步 IPC API 内部仍有 uv_sem_wait，无整体 deadline**；本次不冒险修改公共 IPC。
  若日志停在 API 调用前，需保存 ps/dmesg，不能宣称所有调用已完全防卡死。
- SIGINT 设置停止标志并走清理；不要 force kill。清理失败保留静态 UV 存储，
  后续连接命令拒绝运行，提示保存日志并重启；不清空 live handle 强行重用。
- 日志区分 ATT 写响应和业务 ACK。早到 ACK、重复/旧 seq、错误 opcode、错误 handle、
  非法 CRC 均不能单独造成成功。通知不承担硬实时控制。
- 业务 ACK=0 只证明应用协议确认，不证明电机物理到位。当前未注册 AI Agent 设备工具。

## 主机回归和构建

Linux/WSL 中从本仓库执行：

```sh
bash tools/test-home-client.sh
bash tools/build-a733-ble-v145.sh
bash tools/package-a733-ble-v145.sh
```

ASan/UBSan：共享协议 100000 组畸形输入；实际应用源码 + 官方 GATT ABI 头文件、mock
蓝牙/UV，验证 100 轮完整流程、API失败、回调超时、无服务/错属性/坏Read、丢ACK、
错误seq/CRC、NACK、ACK早于ATT响应、重复ACK、断线重连、清理失败隔离和禁止输出。
mock 测试不证明真实 UV/IPC、USB ACL、射频、SMP 和双板通信通过。
完整构建同时跑 v144 生命周期、控制台、Zblue 和 FAT 回归。
构建目录 `quickly-openvela/cmake_out/cubie-a7z_nsh_v145_gatt_client`，保留 v144 ELF。
官方暂存路径恢复需由本次最终 restore.log 证明，不宣称整个 SDK Git 状态干净。

## 后续验收门

1. v145 启动；home info/frame；旧 bttool 五轮和 Wi-Fi/ping 回归。
2. 有约定服务的测试外设：连接/发现/Read/Subscribe/PING业务ACK/GET_STATE业务ACK/断开。
3. 外设不在场、错地址类型、缺服务、坏帧、拒绝写、缺ACK；必须失败，不能只看 API=0。
4. monitor 中断开/恢复外设，确认最多3次同地址重连，无旧请求重放；Ctrl+C 清理。
5. 核验 ESP32-S3 官方板型和 N16R8 存储配置，再制作 Peripheral 固件、验证双板。
6. 真实设备动作安全验收之后，才加入受限 SET_OUTPUT 和官方 Agent 工具。

## 最终构建与镜像（2026-10-15）

交付时状态：**source-ready / host-tests-passed / build-passed / image-produced**。
后续运行和BLE/轻负载共存证据见顶部更新，未另做完整冷启动/多次重启记录，
尚无真实GATT hardware-passed或双板stress-passed证据。
运行代码检查点 `f7839ba`；镜像/日志归档标签 `a733-ble-v145-gatt-client-candidate`。
保留 v144 标签和镜像，不覆盖。没有推送远端，也没有写入物理 SD 卡。

| 产物 | 字节数 / SHA-256 |
| --- | --- |
| `../openvela-a733-cubie-a7z-sd-ble-v145-gatt-client-candidate.img` | 2147483648；`d66bc9afdf8ecc690fca77b8d42b2f1b177a40cfbdeab9c2c1be893a57f40d32` |
| v145 `nuttx.bin` | 2590272；`27e04cccfdcb340b991fbef723464a3b9e49747f2a796ae3e9e410654d24593f` |
| v145 `nuttx` ELF | 3590232；`810cfef41a558b97bf80bfdf4b910ad8e654273d5b04812075f4ca2d21d7d0dc` |
| v145 `.config` | `d6cf9a1177029504822433fc790f6e0704ee9ce339dd8373e72ad91e33190189` |

ARM64 ELF 的段/栈/页对齐和 Image footprint 检查通过，runtime=36880384 bytes。
确认 `home_main`、官方 `bt_gattc_discover_service/read/write/subscribe` 已链接，
内建命令 `home` 优先级100、栈16384。GCC13.4.0、CMake3.23.0、Ninja1.12.1，
本次完整构建耗时19分39秒（不是板端运行时间）。保留八核、MMU断言、BLE-only、
GATT Client 和 libuv 单 worker 配置。配置中未发现非空凭据配置项。

实际暂存的七个应用源码/构建文件与仓库逐一 cmp 通过。构建退出后 **129 个路径的
内容/不存在状态 + home 目录身份**与构建前快照一致；SDK 中该目录恢复为不存在。
这不是整个 SDK Git clean 的证明：其现有 `.git` 格式在 WSL 中不可解析，构建日志有
相应诊断；另有可选 Rust 参数缺失、原有 ggml 宏重定义、Zblue poller 局部地址和
build-id 丢弃警告，完整保留，不能将编译成功解释为这些路径已通过 GATT 实测。

打包以 SHA 校验通过的 v144 镜像为基础，仅替换第三分区的
`/boot/openvela/a733/Image`；保留 Boot0/SCP/BL31/U-Boot/DTB，仍由 U-Boot `booti`
加载至 `0x40200000`，不要使用 `go`。ext4/e2fsck、镜像内核回读、GPT 检查通过；
第四分区非2048扇区整边界是原镜像继承的提示，未改分区布局。
Windows Get-FileHash 独立核对镜像、ELF、内核和配置哈希一致。

证据均位于 `docs/evidence/20261015-v145-*`：build、official-restore、artifact-check、
layout、package、verify、build.config，以及五组主机回归和 staged-home-match 日志。

### 上板先做的检查

先备份 SD 中自己的 `/data` 文件，用原来的烧录方法选择上述 **v145** 镜像，核对容量
和目标 SD 卡，切勿选择电脑硬盘。烧录会覆盖目标卡，不在本文自动执行。
串口115200 8N1；冷启动进入 NSH 后先执行：

```text
home info
home frame ping 1
home frame state 2
home frame event-state 1
ps
free
time "sleep 5"
```

应看到 `openvela official GATT client v145`、`needs-recovery=0`、三条十六进制报文，
命令正常返回；这一步不要求下位机。随后按上文“启动流程”运行官方服务和 bttool，
回归扫描、停止和启停，另测 Wi-Fi/ping 共存。出现异常先保存完整 ps/dmesg，不要
重复启动多个 bluetoothd 或强杀正在执行的 home。

取得符合协议的测试外设地址后，保持适配器状态2/4，停止扫描并退出 bttool，**不 disable**：

```text
home probe AA:BB:CC:DD:EE:FF random
home monitor AA:BB:CC:DD:EE:FF random 60
```

请替换真实地址与地址类型。probe 成功必须同时出现服务/特征发现、订阅完成、合法
Status Read、PING和GET_STATE各自的 ATT 响应与业务 ACK，最后
`result=0 cleanup=0 ACKs=2`；不能只凭 connect/write API=0 判定。
monitor 再验断线重连和 Ctrl+C；无约定外设时不要求 probe 成功。
失败回滚：重新烧录保留的 v144 镜像；源码恢复请另建 checkout 使用 v144 标签，
不要在当前目录执行 `reset --hard` 或覆盖未提交工作。
