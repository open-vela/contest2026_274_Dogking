# v142 官方 BLE Host 修复候选

日期：2026-10-14。恢复标签：`backup-before-v142-hostfix`（012b073）。
v141 启动、8 核、计时与 Wi-Fi 扫描通过镜像保持不变；I2S 暂停。

## v141 失败证据

原始串口日志保存在 `20261013-v141-host-init-failure.txt`。
pair auto 0 执行；enable 进入 BleTurningOn；state=1 并非 enabled。
官方 Zblue 初始化报 -5，10 条命令 / 10 个事件，扫描未通过。
transport error=0 只代表 USB lower 没有报告致命错误，不代表 Host 就绪。
空闲事件轮询 -110 不单独视为控制器故障。

## 确定问题与修复

1. bttool 的 getline 不做应用层回显/编辑。NuttX 分支改用已有官方
   std_readline，保留 Linux getline；处理 CR/LF、空行、EOF、超长输入，
   限制 argv 容量并接受空格/制表符。只打印命令名称和 rc，不打印配对参数。
   rc=0 表示 API/队列接受，不表示异步操作已经完成，仍以状态回调为准。
2. LE 初始化失败回调误发 BREDR_OFF。修正为 BLE_OFF，启动中状态收到失败
   事件回到 Off。未强行删除线程/重置共享 Wi-Fi USB；初始化失败后要求重启，
   尚不宣称支持不重启的失败恢复或 enable/disable 长稳。
3. /data 是当前 NuttX FAT；其 fat_ioctl 始终 ENOTTY，UnQLite findInodeInfo
   要求 FIOC_FILEPATH，因此数据库初始化失败。项目补丁补足 FAT descriptor
   路径与正常 open/dup/close/分配失败释放，并清理历史 O_APPEND seek
   失败时的文件结构链表/sector-buffer 泄漏。保留官方 UnQLite/POSIX 文件锁，
   不删除 persist.db、不退回无锁数据库或纯内存数据库。
   暂不保证打开文件后 rename/unlink、同文件不同大小写/8.3 别名的锁等价，
   本验收数据库固定路径且禁止在运行期间重命名。
4. A733 lower 添加 Command Complete/Status 的 opcode/status 诊断与 sticky
   failed-op，不打印随机数、密钥或 ACL 内容；正常启动日志限制为前 32 个事件/
   命令，异常 status 始终打印。

## -5 的候选原因与对照配置

该版本 common_init 正常顺序为 Reset / Local Features / Version /
Supported Commands，随后 PRNG 请求 5 次 LE Rand，共 9 条命令；开启的
Controller→Host ACL 流控接着发送 Host Buffer Size，恰好可能是第 10 条。
这只是基于计数的推断，旧日志没有 opcode/status，不能称为已确认根因。
v142 显式关闭可选 CONFIG_BT_HCI_ACL_FLOW_CONTROL 作 USB 对照，保留
Host→Controller 的 ACL credits；RX 大小 1021、额外 RX buffers=9、
Event 大小 255、discardable buffers=10。不能把 USB backpressure
等同于已完成无线/GATT 压力验收；仍需观察丢包、队列和内存。

参考：Bluetooth SIG [HCI functional specification](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Core-54/out/en/host-controller-interface/host-controller-interface-functional-specification.html)，
该流控是独立的 Controller→Host 方向，关闭后不能继续发送 Host Number Of
Completed Packets。Zblue 的条件编译负责这一点，不伪造 Controller response。

## 验证与构建

- `bash tools/test-bttool-console.sh`：从打补丁后的真实代码提取 reader，
  mock 官方 line editor，仅验证 CRLF/空行/EOF/长度界限/重新分配；
  GCC -Wall -Wextra -Werror + ASan/UBSan 通过。真实字符回显仍待上板。
- 已补充超长输入尾部清空测试（尾部不能误执行 enable）、最大完整行边界。
- 已提取真实 command callback 测试空白分割、空命令、31 参数与 argv NULL
  哨兵、32 参数拒绝、异步未配置分支与 quit 释放。ASan/UBSan 通过。
- `bash tools/test-fat-filepath.sh`：从真实补丁后源码提取 ioctl，mock VFS/
  挂载 primitives，验证路径拼接、根路径、NULL、长度、强制卸载/挂载错误及
  锁释放，GCC 严格告警 + ASan/UBSan 通过；不替代真实 SD/UnQLite 读写验收。
- HCI framer 和 home protocol 原有 fragmentation/bounds/CRC/100000 fuzz
  回归通过，均使用 ASan/UBSan；不等于 BLE 无线链路通过。
- 两个新增补丁 patch dry-run 通过；完整编译读取新的 defconfig。
- 构建命令（比赛仓库根，WSL）：

```sh
A733_BUILD_DIR="$PWD/../../quickly-openvela/cmake_out/cubie-a7z_nsh_v142_ble_host_fix" JOBS=12 bash tools/build-a733-wapi.sh
```

构建包装器临时暂存官方源文件，EXIT 恢复。v141 ELF/镜像不覆盖。
## 最终产物与状态

源码修复：`fe86775` → `fc7c5ad` → `3f2e6c6`；后续测试/文档提交
`7a2cc37` / `6c52109`。最终构建采用最新的 31 参数 + NULL 哨兵版本。
在最终构建尚未编译 bt_tools.c 时同步这一小改动到包装器已备份的临时源码；
构建中的四个 CLI/SAL/state 文件与重新应用仓库补丁的结果一致
（只归一化文件末尾缺失 LF），测试日志保留该检查。EXIT 仍恢复官方原件。

- 最终 kernel：`quickly-openvela/cmake_out/cubie-a7z_nsh_v142_ble_host_fix/nuttx.bin`，
  **2577736 bytes**，SHA256：
  `193dfb44cfd5b52ea23e1e15666ed96a918e3db8940f901167c0d54ecb721eee`。
- runtime Image footprint：**36868096 bytes**；text/rodata/data 页边界、
  栈和 ARM64 Image header 完整 footprint 检查通过。继续由 U-Boot booti 启动。
- 镜像：工作区 `A733-A7Z-ALL-Files/openvela-a733-cubie-a7z-sd-ble-v142-cli-hostfix-candidate.img`，
  **2147483648 bytes（2 GiB）**，SHA256：
  `6ba47e14951119d25a29c745aa135110b6daf7e9a7c2fe17f69bcd034528a5bb`。
- 只替换源镜像 partition 3 的 /boot/openvela/a733/Image；
  e2fsck -fn 通过，提取内核 hash 与最终 kernel 一致，GPT No problems found。
  继承源镜像 data 分区结尾非 2048-sector 边界警告，未改变分区布局。
- v141 恢复镜像 hash 复核未改变：
  `a81e1005c4ba33b107b502b347dd7ec5e8e74908c17dde0f0eeaf1e1ee5caf6b`。
- 本轮 65 个暂存/补丁官方路径 SHA256 或 absent 状态与构建前一致，
  见 `20261014-v142-official-restore.log`。不以此宣称其他既有官方工作区改动
  也为干净状态。
- System.map 确认 bluetoothd_main / bttool_main / kvdbd_main /
  a733_bt_host_send / a733_bt_deliver_event。
- 保留既有 GGML UNUSED 重定义、Zblue poller dangling-pointer、
  linker build-id discarded 与 Rust 配置跳过告警；不是零告警构建。

日志：`20261014-v142-build.log`（完整构建）、
`20261014-v142-final-build.log`（最终边界修复后构建）、
`20261014-v142-console-tests.log` / `20261014-v142-fat-tests.log`、
`20261014-v142-package.log` / `20261014-v142-verify.log`。

复现打包（比赛仓库根目录，Linux/WSL）：

```sh
bash tools/package-a733-kernel-image.sh \
  ../openvela-a733-cubie-a7z-sd-ble-v141-boot-layout-candidate.img \
  ../openvela-a733-cubie-a7z-sd-ble-v142-cli-hostfix-candidate.img \
  ../../quickly-openvela/cmake_out/cubie-a7z_nsh_v142_ble_host_fix/nuttx.bin
bash tools/verify-a733-image.sh \
  ../openvela-a733-cubie-a7z-sd-ble-v142-cli-hostfix-candidate.img \
  193dfb44cfd5b52ea23e1e15666ed96a918e3db8940f901167c0d54ecb721eee
```

输出镜像已存在时工具会拒绝覆盖；重新编译应选新的输出文件并使用实际 kernel hash。
烧录会覆盖目标 SD 数据，请先备份模型、Wi-Fi/API 私有配置和其他文件。
当前状态：**source-ready / host-tests-passed / build-passed / image-produced**。
**v142 boot / KVDB 持久化 / BLE_ON / scan / ACL / GATT / 共存长稳均待实机验收**。
具体命令见 `docs/BLE_OFFICIAL_HOST_TEST.md`；若初始化失败先保存 HCI opcode/
status 与 dmesg，再重启，不反复 enable/scan 或私有 probe。
