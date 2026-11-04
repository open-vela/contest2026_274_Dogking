# v138 蓝牙 HCI 候选验收（2026-10-11）

## 状态与恢复点

source-ready / build-passed / image-produced：通过。
boot-passed / hardware-passed / stress-passed：未验证，等待实机。
BLE 扫描、配对、GATT、双板通信尚未完成。此镜像不能用来驱动下位机电机。

源码提交：`f610e03`。恢复分支 `backup-before-ble-v138` → `87b6268`。
I2S 暂停，历史代码、文档和镜像保留，v137 麦克风仍未通过。
应用 codec 已通过 CRC 标准向量、往返、边界、错误报文及 100000 组固定种子畸形输入测试；GCC `-Wall -Wextra -Werror` + ASan/UBSan 全部通过。

## 构建与镜像

全量构建：2730 targets，06:47。编译器 `aarch64-none-elf-gcc` 13.4.0，CMake 3.23.0，Ninja 1.12.1。

```text
build directory: quickly-openvela/cmake_out/cubie-a7z_nsh_v138_ble_hci
.config SHA256: 8f9226ef0f35feb55e57d975fab98691f65a21244502536dc2558ffd30c2951f
nuttx.bin bytes: 2203864
nuttx.bin SHA256: 6b027026945e15b5e5a6682d399305b2627a02b49b9e519cc8432f6d4bcb4e17
image: A733-A7Z-ALL-Files/openvela-a733-cubie-a7z-sd-ble-v138-hci-candidate.img
image bytes: 2147483648
image SHA256: 313e210cca9e7e3a8ed40316254fc707bad962f9bae0161ed0f075c8d6712f84
```

保留 build directory 中的 `nuttx` ELF、`System.map`、`.config`、`build.ninja`、`.ninja_log`，便于精确地址解析。
构建结束检查官方目录无新增 `.inc`，原 USB 驱动无本轮 include；EXIT 恢复路径已执行。没有有效 Git 元数据的整套官方工作区不声称 Git-clean。

封装基底是 v137 镜像，只替换 partition 3 的 `/boot/openvela/a733/Image`，不修改原镜像。
ext4 检查、GPT 检查和内嵌内核反向哈希全部通过。partition 4 未按 2048-sector 结束的提示继承原布局，GPT 仍报告 No problems found。
启动流程不变：Boot0/SCP/BL31/U-Boot `booti`，EL1，Image 0x40200000，不使用 `go`。

## 烧录与测试

按原 SD 镜像烧写方法写 v138，先备份卡内数据并核对目标 SD 卡；磁盘上的旧恢复镜像不要覆盖。
串口 115200 8N1，进入 NSH：

```text
cat /dev/a733-bt
echo probe > /dev/a733-bt
cat /dev/a733-bt
dmesg
```

未测试时 `last=-11 stage=not-probed` 正常。成功最低要求：

```text
last=0 stage=hci-query-passed ... hci-status=00 unsafe=0
```

还要核对 version/address/features 合理，重复 probe 至少 3 次，再冷启动重复。并行检查 Wi-Fi：

```text
ifconfig
ping -c 4 <实际网关地址>
```

电脑 SSH 必须保持可用；显示与 Agent 做原功能回归。压力检查应至少 10 次 probe 并行双向网络。

负向测试：`echo invalid > /dev/a733-bt` 应报错，不发送 HCI，不增加 probes；双 shell 同时 probe 只允许一个执行，另一个报忙。
若 `unsafe=1`，保存 dmesg 后重启，不继续 probe。失败回传完整报告。若启动回归，恢复先前已通过的功能基线镜像，保留本轮 ELF/日志。

## 后续边界

HCI 实机通过后，增加官方 `bt_driver_s`、event/ACL RX 和组包，再启用官方 BLE-only Zblue/openvela 服务与 GATT Client。连接、权限、去重、业务 ACK、Agent 工具尚待开发。
用户确认：先开发 A7Z；没有现成可用的 ESP32-S3 工程。提供的历史仓库 `https://github.com/hxy299/ESP32S3N16R8-Openvela` 用户称不可用，本轮未读取/修改，不作为已验证基线。后续从官方 ESP32-S3 支持核对 N16R8 配置。
详见 `docs/BLE_SMART_HOME_PLAN.md`。
