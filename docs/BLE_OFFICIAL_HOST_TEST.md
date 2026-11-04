# A7Z 官方 openvela BLE Host 验收

2026-10-15 实测更新：v144 已通过五轮正常 enable/scan/stop/disable/quit/重进，
每轮都有真实广播并回到 OFF(0)，见
[实机证据](evidence/BLE_V144_FIVE_CYCLE_HARDWARE_PASS.md)。
Wi-Fi 共存、扫描中直接 quit、长期压力和 GATT 仍待验收。
**重启后必须先检查并启动 kvdbd、bluetoothd，不能省略下文第 2 节。**
下方候选状态描述保留作历史记录。

2026-10-14 最新更新：v143 两轮通过后重开/关闭失败，不能视为长稳通过。
v144 为 Host 回收/重开修复与诊断候选，上板门以
[v144 测试记录](evidence/BLE_V144_REOPEN_CANDIDATE.md) 为准。
以下 v143 候选说明保留作历史记录，不代表现在的验收结论。

最新候选：v143（生命周期修复，构建/镜像校验通过，待实机）。v142 已扫描通过，但关闭/退出
失败，不要在 v142 重复 enable/disable 期待恢复。本文以下开关循环只针对 v143。
证据和限制见 `docs/evidence/BLE_V143_LIFECYCLE_FIX.md`；历史记录保留。

当前阶段：官方 `bt_driver_s` / BTH4、Zblue、openvela 服务及 GATT Client 已接入。
v142 官方扫描已有实机证据；v143 需验证关闭与退出重进。ACL/GATT 尚未实机通过。

恢复基线为 v141：启动、8 核、计时、Wi-Fi 扫描及 H4 注册已有实机输出，见 `docs/evidence/BLE_V141_BOOT_HARDWARE_PASS.md`。后续 v141 HCI query 通过，但官方 Host 初始化 -5、数据库 I/O 错误，扫描未通过；v140 启动失败。当前待验收候选为 v143，需烧录新镜像。

v142 采用官方 readline 回显/编辑，逐条打印 command name + rc，修复 LE 失败状态并补足 FAT 文件路径 ioctl；可选 Controller→Host ACL 流控关闭作 USB 对照。失败证据、推断和配置见 `docs/evidence/BLE_V142_HOST_FIX_CANDIDATE.md`。rc=0 只表示接受，必须继续看状态和回调。

I2S 继续暂停；ESP32-S3 暂不修改，没有下位机新固件。当前只测试 A7Z。

## 分层与代码位置

```text
官方 bttool（未来智能家居应用也使用这些 API）
  -> 官方 openvela Bluetooth framework / 本地 socket IPC
  -> 官方 bluetoothd / GAP / GATT Client / Zblue Host
  -> 官方 BTH4 /dev/ttyHCI0
  -> A733 bt_driver_s lower half
  -> FCU760K USB1 EP0 / interrupt event / bulk ACL
```

永久代码在本仓库 overlay：

- `vendor/allwinnertech/chips/a733/a733_bt_checkpoint.inc`：保留诊断；host 打开时 probe 返回忙。
- 同目录 `a733_bt_transport.inc`：标准 open/send/close/receive，事件/ACL RX、单个常驻可休眠 worker，与 Wi-Fi USB 锁共享。
- 同目录 `a733_hci_framer.h`：标准 HCI 分片/合包，包长检查，不是私有 ATT/GATT 栈。
- 同目录 `Kconfig`：映射 A733 64-bit ABI 给官方 Zblue。
- `vendor/allwinnertech/boards/a733/cubie-a7z/configs/nsh/defconfig`：启用官方 BLE 服务、GATT Client、libuv、KVDB/UnQLite、LOCAL IPC 和 H4。

接收缓存边界：event 最大 257 字节，ACL 最大 1025 字节；独立 USB DMA 缓冲，不混用 WLAN endpoints。RX queue 满/坏帧/真实 USB 错误会记录 error 并停止该 host 的 RX，而不是静默丢掉控制回复继续运行。诊断下的 NAK 超时属于正常无数据。
该阶段 ACL TX payload 配置 251 bytes；不支持恰好整 512-byte 的 USB OUT/ZLP 终止，显式拒绝。SCO/ISO/蓝牙音频不在本轮。
硬件成功尚未代表上述 ACL 数据路径通过；需后续双板 GATT 测试。

若强制中断时 USB message 只收了一部分，关闭后设置 `needs-reboot=1`，拒绝重开/私有 probe，避免把旧尾部当成新 HCI header。当前恢复手段为保存日志后重启；不以重置共享 USB 控制器影响 Wi-Fi。这一故障恢复属于明确限制，不宣称任意取消后可无损重开。

## 构建补丁

所有公共源码改动均为项目管理的临时补丁，EXIT 恢复：

- `bluetooth-a733-build.patch`：仅蓝牙组件保留断言，解决 release 配置的 assert-only locals 告警，未关闭全局 Werror。
- `bluetooth-bth4-reopen.patch`：首次重新打开时清除上次会话残留 RX bytes；保留同次打开的多引用和 lower USB DATA toggles。
- `bluetooth-ble-only-build.patch`：隔离关闭 Classic 时的 BR lookup/静态 bond helper；snoop 路径超长时跳过，避免截断路径用于文件操作。
- `zblue-a733-compat.patch`：校正 HCI completed-packet callback 声明、隔离未开启 privacy 的 IRK 访问、通过 intptr_t 转换 ARM64 线程 ID，并正确检查 pthread_create 的正数错误返回。修复 k_poll 持锁阻塞等待、退出仅按登记数量清理前缀而可能遗漏非连续 events 的问题。GCC 13 对 k_poll 活跃期间登记栈上 poller 的 dangling-pointer 告警保留显示，但不作为该组件的编译错误；强制取消与长稳仍须单独验收，不能把源码审计当成运行验证。

官方日志配置依赖 property monitor，因此启用官方 `kvdbd`；不伪造属性接口，不默认启动经典蓝牙/SPP/音频。当前配置未启用永久 pairing/bond 恢复验收，后续须单独验证 settings 与密钥存储。
本轮关闭 `CONFIG_BT_GATT_CACHING`（本地 GATT 服务端的 robust caching/database hash），当前官方 SDK 这段多实例接口改造存在未传 hdev、声明不一致等编译错误。保留 Service Changed 和官方基本 GATT Client，不宣称 robust caching 已适配；后续如需本地动态服务端再单独完善该分支。
本轮 A7Z 只作 Central，关闭本地 `BLUETOOTH_BLE_ADV`，不启动广播/Peripheral；ESP32-S3 的广播在后续下位机工程中实现。官方 BR enhanced/sniff 在 BLE-only 配置下明确返回 NOT_SUPPORTED，不伪装为成功。

## 上板前

新增项目补丁 `bluetooth-cli-init-recovery.patch` 与 `nuttx-fat-filepath.patch`
由构建脚本暂时应用并恢复，不删除用户数据库、不实现私有 GATT。

保存 SD 中模型、Wi-Fi/API 配置等用户数据，再写入本轮新候选；不要覆盖磁盘上 v139 恢复镜像。
串口 115200 8N1，也可以 SSH 操作。以下命令注明提示符所属位置，**不要把 `bttool>` 命令直接发给 NSH**。
v142 程序自行回显；若此前在串口软件开启了“本地回显”，请关闭以免字符显示两遍。
只复制命令本身，不复制 nsh>/bttool> 提示符、Markdown 代码围栏或 C++ 标记。

## 1. 无 Host 时回归诊断

在 `nsh>`：

```text
ls -l /dev/ttyHCI0
cat /dev/a733-bt
echo probe > /dev/a733-bt
cat /dev/a733-bt
```

应看到 `registered=0 open=0`（registered 的 0 是注册返回成功），query 仍为 `last=0 stage=hci-query-passed`。
**不要 `cat /dev/ttyHCI0`**：它是二进制 HCI 传输设备，不是文本状态节点。

## 2. 手动启动官方服务

在 `nsh>`：

```text
kvdbd &
sleep 1
setprop persist.a733.ble.acceptance v143
getprop persist.a733.ble.acceptance
bluetoothd &
sleep 1
ps
bttool
```

服务尚不自启动。重复执行前先用 ps 确认没有已有的 kvdbd/bluetoothd，避免重复绑定 socket。
数据库测试应读回 `v143`，且 setprop 不报 commit 错误。测试只新增专用验收 key，
不改凭据。完成后可用 `setprop persist.a733.ble.acceptance` 删除该测试 key。
重启后先启动 kvdbd，再 getprop 同一 key，可验收持久化；首次回传日志无需先删除。
若创建实例失败、进程退出或出现 assertion，停止进入后面的扫描步骤；另一个串口/SSH 执行 `cat /dev/a733-bt` 和 `dmesg` 回传。UnQLite/KVDB 使用 /data 的官方数据库目录；不要删除已有凭据/目录。

## 3. 官方工具内启用与扫描

进入 `bttool>` 后逐行输入：

```text
pair auto 0
enable
state
```

等待约 3–5 秒，再执行一次 `state`。本 BLE-only 配置应到 **2（BLE_ON）**；
1 是正在启动，0 是关闭。只有出现 BLE_ON 才执行：

```text
scan start -t 0 -p 1M -m 0 -l 1
```

只执行一次 start。等待扫描状态回调 status=0 后观察广播结果，不要连按 start/stop。
若 zblue init failed、状态回到 0/持续为 1，立即 quit，回到 NSH 保存
`cat /dev/a733-bt`、`dmesg` 和 `ls -l /data/persist.db`。重点记录
`A733 BT HCI: TX opcode=...` / `event=... opcode=... status=...` 与
`hci-proof: failed-op=... failed-status=...`。
初始化失败后 Host lower 可能仍 open=1，禁止 probe；重启后再测试。
不要因工具显示 accepted 就把 enable/scan 记为通过。

先用被动、低功耗、legacy BLE scan，不运行经典 `inquiry`。`pair auto 0` 关闭工具默认的自动接受配对。暂不连接未知设备。
等待约 10–20 秒，可让手机用 BLE 广播工具发一个已知测试广播（普通手机蓝牙设置可见不等于有 BLE 广播）。没有广播源时“未看到设备”不能单独判为驱动失败，但应有扫描启动状态回复。

结束：

```text
scan stop
scan dump
```

等待 `on_scan_stopped_cb`，再次 `scan dump` 必须为 idle；stopping 不算通过。
重复一次 `scan stop` 应提示 already stopped，不应再发停止命令。随后执行：

```text
disable
state
```

等待约 3–5 秒，再执行 `state`，正常关闭应为 0，然后 `quit`。
state=6 是 BLE 正在关闭，不能立即 enable；若持续不归零，保存日志后重启。
首次验收不强制 Ctrl+C 杀线程；如果关闭失败也保存日志后重启。

在 `nsh>` 检查：

```text
cat /dev/a733-bt
dmesg
```

成功最低证据：官方 enable/state 正常、scan callback 的 status=0、有真实 ScanResult（地址/RSSI/advertisement），host `error=0`、TX/RX 计数增加。`last` 仍是私有 probe 的上次结果，**不用于判断官方 Host 成功**。

## 4. Wi-Fi 共存与重复启动

v143 先完成单客户端生命周期测试：state=2 后重复一次 enable 应提示
already enabled，state 仍为 2，dmesg 不应新增 btBrEnableFail。
完成三轮正常开关后，额外扫描中直接 quit；工具最多等待 2 秒停止回调。
重新进入 bttool，state 应仍为 2（quit 不关闭 daemon/适配器），scan dump 为 idle，
可重新 scan start。再次 stop、disable、state=0、quit。若 cleanup timeout，
先保存诊断再重启，不继续重进。此门不使用第二个 bttool 并发操控适配器。

在另一个 NSH/SSH 会话连接 Wi-Fi，执行 `ping -c 20 <实际网关>`，并同时做一轮 BLE scan，记录全部丢包和 RTT，而不是只写“正常”。至少重复三轮 enable→scan→stop→disable；检查 RX worker pid 不不断新增、无 host failures、无内存持续增长。原 ST7735/Agent 做回归。
Host `open=1` 时 `echo probe > /dev/a733-bt` 必须报忙/记录 -EBUSY，不可抢走官方命令事件。它可能改动诊断 last，不影响 host counters。
冷启动复测；长稳和 Wi-Fi 重连仍为待办。

## 5. 下一阶段

扫描通过后才进行 ESP32-S3 官方 Peripheral/GATT Server、主控 service discovery、write/notify、业务 ACK、权限及重连测试。当前不接电机，不把 LLM 回复直接执行为物理动作。应用报文 codec 与 UUID 约定见 `BLE_SMART_HOME_PLAN.md`。
