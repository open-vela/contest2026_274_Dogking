# v144 BLE Host 关闭/重开候选与定位记录

2026-10-15 后续实测：五轮正常扫描/停止/关闭/OFF/退出重进通过，见
[实机归档](BLE_V144_FIVE_CYCLE_HARDWARE_PASS.md)。本文件保留 2026-10-14
候选交付时的状态及构建事实；Wi-Fi 共存、GATT、长期压力仍未通过验收。

## 验收边界

v143 的前两轮官方 BLE enable → 真实广播扫描 → stop → disable → OFF → quit
实机通过；随后重开不再产生广播，下一轮 disable 长时间停在状态 6。
因此 v143 **不是重复启停稳定通过版本**。v144 针对 Host 生命周期修正并增加定位，
不是 GATT/ESP32-S3 通信通过版本；I2S/摄像头/下位机继续暂停。

原始日志保留，不删旧开发文档：

- `20261014-v143-first-cycle-pass.txt`：首次完整循环通过。
- `20261014-v143-reopen-failure.txt`：第二轮完整循环通过；后续重开扫描仅接受，无真实结果。
- `20261014-v143-closing-hang.txt`：关闭超过 130 秒仍状态 6，关闭成功事件缺失。

失败时 lower-half `open=1 worker=31 error=0 needs-reboot=0`；HCI TX=68/RX=526
没有增加，最后 opcode=2005。GATT 上层 shutdown 已完成；bluetoothd 的 libuv worker
在 Semaphore 等待。`ps` 不含阻塞位置/调用栈，**尚不能排他证明缓冲耗尽或锁死**。
scan status=0 / on_scan_stopped 是服务接受/扫描器回收，不等于控制器命令已完成。

## 改动与原因

永久改动仅在项目 `patches/bluetooth-zblue-reopen.patch`，构建时暂存到官方 SDK：

1. Zblue `host/scan.c`：扫描 Reset 仅清数据，扫描互斥锁一次初始化。
   原代码把已初始化 POSIX-backed mutex 清零并重新初始化，是生命周期隐患。
2. Zblue `host/hci_core.c`：H4 关闭、接收 poll 移除后，取消并有限等待 init/TX/RX
   work 空闲；再归还残余 command/RX/sent buffers，最后清空 controller 对象。
   重复 disable 不再清零另一个关闭调用仍拥有的对象。
   2 秒名义回收等待失败保留对象并返回错误，不伪报 OFF、不覆盖 live work。
   当前配置无 PRIVACY/ISO/独立 BT RX workqueue；不据此保证其它配置的全部延迟工作。
3. 命令池分配改为 2 秒名义有限等待；耗尽打印 opcode，返回原有 ENOBUFS 路径，
   不增大缓冲池掩盖问题。原有 HCI 同步回复等待/超时 assert 未改；出现超时仍应重启定位。
4. 官方 SAL scan 将请求参数拷贝进已入队请求，避免后续 set_parameters 改变尚未执行的参数。
   新增 enqueue / worker-begin / worker-end rc；**上层扫描状态仍不是硬件完成证明**。
5. 官方 H4：移除 poll 时清空半包；poll 注册失败关闭 fd；连续 EAGAIN 写入
   名义约 2 秒后失败，write=0 直接失败，避免零进展无限循环。
   修正 H4 send 失败仍 unref 而 Host 再 unref 的重复释放：失败还原 H4 头、保留调用者
   ownership 和错误码；只有成功发送才由 driver 消耗引用。
6. 增加 `BLE-LIFE sync-wait/sync-done` 与 disable reset/close/quiesce 日志。
   只打印 opcode/返回码/阶段，不打印密钥或 HCI 载荷。
   HCI checkpoint 使用直接 syslog：当前 CONFIG_BT_DEBUG_LOG 未启用，普通 Zblue
   LOG_INF/LOG_ERR 会编译掉；不打开全栈调试洪泛。

v143 恢复点：`backup-before-v144-ble-reopen` → `c15f84b`。
v143 镜像保留，SHA-256：
`fa3b6592e35a367de29cd1b4fe3b7a6233d3ebdaeea106e6b88bbbd037bb0bf5`。

## 源码测试和构建

Linux/WSL，在项目仓库执行：

```sh
bash tools/test-zblue-reopen.sh
bash tools/build-a733-ble-v144.sh
bash tools/package-a733-ble-v144.sh
```

新单元测试抽取实际补丁后的函数，以 ASan/UBSan 验证 100 轮有限回收、扫描锁一次初始化、
超时不清对象、不归还 live buffer、H4 短写/EAGAIN/零返回/错误引用归属和入队参数快照。
另保留 v143 CLI/lifecycle 与 FAT 回归测试。测试 mock 不是 USB/HCI/SMP 实机压力测试。
构建独立目录 `quickly-openvela/cmake_out/cubie-a7z_nsh_v144_ble_reopen`，不覆盖 v143 ELF。
当前 checkout 的 v143 构建入口拒绝混入 v144；复现 v143 应在另一 checkout 使用
`a733-ble-v143-lifecycle-candidate` 标签，不能用当前 wrapper 覆盖恢复用 ELF。
构建 wrapper 的 EXIT trap 恢复全部暂存路径，前后 hash 独立比较；以最终日志为准。
镜像由 v143 基础镜像只替换 kernel，保留 GPT/数据分区方案；未操作物理 SD。

## 实机下一门

先连接 Wi-Fi、记下 `ifconfig` / 网关 ping；通过串口执行 bttool，逐条等待异步回调：

```text
bttool
pair auto 0
enable
state
scan start -t 0 -p 1M -m 0 -l 1
scan stop
scan dump
disable
state
quit
```

enable 等状态 2；scan 至少等 10 秒、必须有真实 ScanResult；stop 等 stopped 回调；
disable 等状态 0，不是状态 6；quit 后回到 NSH。同一开机重复 5 轮，然后增加
“扫描中 quit → 重进 scan → 正常关闭”两轮，再 ping 网关。实际实时超时含调度延迟。

出现无结果/状态 6 不消失，停止继续重开，立即保存：

```text
echo hostdiag > /dev/a733-bt
cat /dev/a733-bt
ps
dmesg
```

`enqueue` 无 `worker-begin`：先前 SAL job 阻塞；begin 后无 end：该 job 内阻塞；
command-pool timeout：可证明缓冲池分配未能完成；sync-wait 无 sync-done：等待 HCI 回复；
close 后无 quiesce complete：工作回收门失败。任何失败均不可宣称稳定/GATT 就绪。
Host 活跃时不要 probe，不 force kill bluetoothd，不绕过状态强行重新初始化。

## 本轮最终产物

状态：**source-ready / host-tests-passed / build-passed / image-produced / image-verified**。
尚无 v144 boot-passed、hardware-passed 或 stress-passed 证据。

最终镜像：上一级 `A733-A7Z-ALL-Files/` 内
`openvela-a733-cubie-a7z-sd-ble-v144-reopen-candidate.img`，2,147,483,648 bytes。
旁边保存 `.img.sha256`；不要把 2 GiB 镜像放进 Git。

| 产物 | SHA-256 |
| --- | --- |
| v144 镜像 | `829a1e972129c35a3347575ee5dac219a060cdddec6206fbd410954c44b66716` |
| 最终 nuttx.bin，2,581,928 bytes | `643701ecb181ae48bc203d87e0ff36b346947bd8dc9017b19faa7425e71ef956` |
| 未剥离 ELF nuttx | `19fed98a06d2ae36baab2142e6f435f5143c20e97cd39a086739bdde9bd2efd1` |
| 归档 build.config | `2ef2a3f4705b570e9b962afe918e20abcf11612bdf9a98c91c277093ee5a34c6` |

ELF/runtime footprint = 36,872,192 bytes，4 KiB 页/段/栈/ARM64 Image 检查通过。
工具链 GNU AArch64 13.4.0、CMake 3.23.0、Ninja 1.12.1；构建使用 12 jobs。
CONFIG_SMP_NCPUS=8、LIBUV_THREADPOOL_SIZE=1、CMD_TX_COUNT=2、BLE-only/GATT Client
不变；没有以增加并发或命令池容量掩盖旧故障。

最终诊断字符串已从内核读回，见 `20261014-v144-kernel-checkpoints.log`。
补丁/生命周期回归抽取的生产源码与实际 staged 构建源码一致，见两个 staged-match 日志。
首次完整构建中间日志 `20261014-v144-initial-build.log` 保留；最终 H4 ownership 和
可见 syslog 补充修正经 `--incremental` 重建，以 `20261014-v144-build.log` 和上述
最终内核 hash 为准，不把中间 ELF 当交付件。

最终 `20261014-v144-official-restore.log`：119 个暂存路径前后 hash/不存在状态一致。
这个结论只针对 wrapper 本轮触及的路径，不宣称整个官方 checkout 的 Git 状态干净。
v143 nuttx.bin/ELF 原 SHA 均独立复核通过，没有覆盖恢复件。
修复前/后源码副本保存在上级 `v144-ble-source-audit/`，正式补丁在本仓库。

`20261014-v144-package.log` 与 `20261014-v144-verify.log`：ext4 e2fsck 通过，
读回内嵌 kernel 的 hash 与构建产物一致，GPT 检查无问题，Windows 独立镜像 hash 一致。
GPT 分区 4 末端非 2048-sector 对齐提示继承自基础镜像，不是本轮改分区；没有操作物理磁盘。
保留既有 ggml UNUSED 重定义、Zblue poll 内联 dangling-pointer 及 build-id discarded
构建警告，不称 warning-free；本轮没有编译/链接错误。

源码检查点依次 `488b560`、`455109b`、`bbf2e93`，恢复入口保护 `b86cb20`。
最终含证据的恢复标签为 `a733-ble-v144-reopen-candidate`；所有提交仅本地保存，没有远端推送。
