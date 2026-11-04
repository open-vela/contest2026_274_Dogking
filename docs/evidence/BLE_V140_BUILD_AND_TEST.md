# v140 官方 BLE Host 候选构建与验收记录

日期：2026-10-13。硬件：Cubie A7Z / A733 / FCU760K USB1。I2S 保持暂停。

## 恢复点

- `backup-before-ble-v140`：接入官方 Host 前的仓库状态。
- v139 HCI 查询三轮通过；镜像保持不变，见 `BLE_V139_HARDWARE_PASS.md`。
- v140 源码阶段提交 `3083816`；等待路径修复提交 `1ab2b00`。

## 本轮实现

官方 openvela Bluetooth framework/socket IPC → bluetoothd/GAP/GATT Client/Zblue → BTH4 `/dev/ttyHCI0` → A733 USB HCI lower half。标准 HCI event/ACL 组包，不自行实现 ATT/GATT。Host 与诊断互斥；与 WLAN 共享 USB 锁，不重置 WLAN 控制器。

保留限制：SCO/ISO 不支持；ACL TX payload 251；不支持恰好整 512-byte 的 USB OUT/ZLP；半帧中断会要求重启。未验收 pairing/bond 持久化与强制取消恢复。

## 编译问题与处理

1. 官方 release 配置下 assert-only locals 告警：仅蓝牙组件 `-UNDEBUG`，不关闭全局 Werror。
2. Classic 关闭时 SAL 仍引用 BR helper：补丁隔离 Classic-only 路径。
3. 日志依赖 property monitor：启用官方 KVDB server/UnQLite，手动先启动 kvdbd。
4. ARM64 配置/线程 ID 转换、HCI callback 过时声明、privacy 关闭时 IRK 访问：项目补丁修复。
5. k_poll 持锁阻塞与非连续 events 清理：项目补丁修复，尚需实机调度/长稳验证。
6. 默认开启的本地 GATT robust cache 存在多实例迁移残缺（hdev、声明/参数和 gatt_ctx）。首轮 Central 不需要该可选服务端功能，关闭 `BT_GATT_CACHING`，保留基本官方 GATT Client 与 Service Changed。未宣称 robust caching 已完成。
7. 最终链接暴露 generic connection manager 引用 Classic sniff、广播 SAL 引用未配置的扩展广播。Classic enhanced/sniff 在 BLE-only 下返回 NOT_SUPPORTED；A7Z 首轮关闭本地 BLE_ADV，保持 Central/Observer/GATT Client。ESP32-S3 的 Peripheral/广播在后续工程实现。

GCC 13 dangling-pointer 告警仅在 Zblue 保留为 warning；正常/超时路径清理经源码核对，不能代替取消压力测试。

## 主机验证

`test_hci_framer.c` 与 `test_home_protocol.c`：GCC `-Wall -Wextra -Werror -fsanitize=address,undefined` 均通过。覆盖分片/合包/最大包/容量/上层拒收、CRC/往返/畸形输入，各含 100000 组固定种子 fuzz。测试命令见 `app/a733home/README.md`。不代表蓝牙无线链路验收。

## 构建复现

在 Linux/WSL 比赛仓库根目录：

```sh
A733_BUILD_DIR="$PWD/../../quickly-openvela/cmake_out/cubie-a7z_nsh_v140_official_ble" \
  bash tools/build-a733-wapi.sh
```

默认完整构建，读取仓库 defconfig；增量目录变更配置必须另行重配置，不能假设 `--incremental` 自动读取新的 defconfig。本轮已有目录用官方 kconfig-tweak 关闭同一个生成配置符号，再执行增量构建；最后以实际 `.config` 和 config.h 核对。

构建脚本临时暂存 overlay/补丁，EXIT 恢复官方源码。构建输出、镜像、工具链不纳入 Git；永久源码与补丁保存在比赛仓库。

## 产物与硬件状态

构建、链接、镜像生成及校验通过；**尚无 v140 实机 boot/扫描/ACL/GATT 通过证据**。完整上板步骤见 `docs/BLE_OFFICIAL_HOST_TEST.md`。

- 内核目录：`quickly-openvela/cmake_out/cubie-a7z_nsh_v140_official_ble`（工作区根目录相对路径）。
- `nuttx.bin`：2577810 bytes；SHA256 `0298d4a7fb63b352c925e7da229e23dc08ffb8f0bce8c900a0e446d05f2edb10`。
- ARM64 Image header：offset 0x38 为 ARM64 magic，继续使用 U-Boot booti，不用 go。
- 镜像：`A733-A7Z-ALL-Files/openvela-a733-cubie-a7z-sd-ble-v140-official-host-candidate.img`，2147483648 bytes；SHA256 `307a05d0bddb4f954c87a3957ac03d7e394bb490298a33db7389b91105aaa6e5`。
- 镜像从 v139 复制，只替换 partition 3 的 `/boot/openvela/a733/Image`；源镜像不修改。e2fsck -fn 通过；提取内核 SHA256 一致；sgdisk 显示 No problems found。继承 data 分区结尾非 2048-sector 边界提示，未为此改变用户数据分区布局。
- System.map 已核对 `bluetoothd_main`、`bttool_main`、`kvdbd_main`、`a733_bt_host_open`、`bt_enable_mc`、`bt_gatt_discover` 和 `bt_gattc_discover_service`。
- 实际 .config：64BIT/KVDB_SERVER/BTH4/Central/Observer/GATT Client 启用；BT_GATT_CACHING 与 BLUETOOTH_BLE_ADV 关闭。编译保留 GCC13 poller dangling-pointer warning、历史 GGML UNUSED 重定义和 linker build-id discarded 告警，不记作零告警构建。
- 最终增量编译日志：`A733-A7Z-ALL-Files/build-logs/v140-official-ble-final3.log`；打包/校验日志为同目录 `v140-package.log` / `v140-verify.log`。
- 构建脚本 EXIT 已恢复官方源码；抽查 poll.c/thread.c/hci_core.h/id.c/Zblue CMake/BTH4/蓝牙 CMake 共 7 个文件，SHA256 均与本轮暂存前备份完全一致。

打包及验证命令（比赛仓库根目录）：

```sh
bash tools/package-a733-kernel-image.sh \
  ../openvela-a733-cubie-a7z-sd-ble-v139-usb-route-candidate.img \
  ../openvela-a733-cubie-a7z-sd-ble-v140-official-host-candidate.img \
  ../../quickly-openvela/cmake_out/cubie-a7z_nsh_v140_official_ble/nuttx.bin
bash tools/verify-a733-image.sh \
  ../openvela-a733-cubie-a7z-sd-ble-v140-official-host-candidate.img \
  0298d4a7fb63b352c925e7da229e23dc08ffb8f0bce8c900a0e446d05f2edb10
```

打包工具拒绝覆盖已有输出。重新构建后内核 hash 可能变化，应使用新目录/镜像名和实际 hash，不能照抄旧 hash 冒充同一产物。
验收顺序：boot → 私有 HCI 回归 → 官方服务启动 → enable/state → 被动 BLE scan → 三轮开关/Wi-Fi 共存 → 后续 ESP32-S3 GATT 双板测试。当前不控制电机或继电器。
