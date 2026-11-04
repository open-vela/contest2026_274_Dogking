# v143 BLE-only 生命周期修复候选

日期：2026-10-14。状态：源码、主机回归、完整构建、镜像生成/校验通过，
**尚未实机验收**。不把镜像校验等同于启动/无线生命周期成功。

## 已保存的实机证据与恢复点

- `20261014-v142-scan-hardware-pass.txt`：v142 官方 Host 到 BLE_ON，扫描回调
  status=0，177 条 ScanResult，57 个不同地址（不等于 57 台物理设备）。
  KVDB 专用 key 写入/读回通过；尚未据此宣称重启持久化通过。
- `20261014-v142-lifecycle-hardware-failure.txt`：退出重进后 disable 不生效，
  再次 enable 误入 Classic TurningOn；扫描清理失败。USB transport error=0、
  needs-reboot=0、failures=0，不把私有 not-probed/-11 当作 Host 故障。
- `backup-before-v143-ble-lifecycle` → `440a3b6`，v142 原始源码和精确 ELF 保留。
- v142 镜像不覆盖，SHA256：
  `6ba47e14951119d25a29c745aa135110b6daf7e9a7c2fe17f69bcd034528a5bb`。
- 启动/SMP 恢复标签 `a733-v141-boot-smp-pass` 继续保留。

## 根因（日志与源码相互印证）

1. bttool disable 调用 `bt_adapter_disable_safe`，服务发送 SYS_TURN_OFF_SAFE，
   BleOn 原状态没有处理该事件，日志确认状态不变。
2. BleOn 收到 SYS_TURN_ON 无条件进入 Classic TurningOn；BLE-only 没有 Classic，
   日志确认 btBrEnableFail，状态滞留。不是 Wi-Fi 射频/USB 失效。
3. bttool 初始化只识别 ON=4，不识别 BLE_ON=2；g_cmd_had_inited=false 使退出
   跳过清理。scan uninit 原实现仅清空指针，不停止服务端扫描。
4. enable/disable 忽略 API status；scan stop 是 void 接口，不能以 rc=0 判定完成。
5. SAL 原实现将 disable work 入队后立即报告 OFF，而真实 bt_disable/HCI close
   尚未执行。这是源码审计发现的额外时序问题，不声称旧实测已单独触发它。

## 项目补丁

`patches/bluetooth-ble-lifecycle.patch` 在既有 cli-init-recovery 补丁之后应用。
不改 NuttX 架构、BSP/USB 固件、Wi-Fi、电源、I2S，也不实现私有 GATT。

- BleOn 重复开启：BLE-only 保持当前状态；Dual-mode 保留升级 Classic 的原逻辑。
- BleOn safe-off：进入官方 BLE profile/scan/connection shutdown，不直接重置 USB。
- bttool 识别 BLE-only ready；幂等初始化，关闭与退出均清理模块；退出标志防止
  延迟回调重新初始化已经退出的工具。
- enable/disable 打印 API 原始数值并将失败映射 CMD_ERROR，过渡中拒绝相反操作。
- scan stop 保留 scanner 至 STOPPED/失败回调，重复 stop 不重复发送；异步 start
  失败回调清理引用。官方 IPC dispatcher 仍负责释放 wrapper，工具不手动 free。
- scan dump 输出 idle/active/stopping。quit 发出 stop 后最多等待 2 秒的回调清理，
  超时明确要求保存日志、重启，不伪造 idle 或尝试不安全释放。
- 工具/扫描状态用 mutex 保护。当前官方 socket 接收循环将回调投递到 libuv
  work queue，sync IPC 回复在接收循环处理；本配置线程池为 1。
  不把该实现宣称为任意多工具实例/多线程池配置的压力验证。
- SAL 只有 bt_disable 成功返回后发送 BLE_OFF；失败维持关闭中状态并记录错误，
  不伪造成功。官方 Host 的正常 HCI Reset 不等于重置共享 USB/Wi-Fi 控制器。

## 主机回归

`tools/test-bttool-lifecycle.sh` 从打补丁后的真实官方文件提取函数/扫描源文件，
使用 mock IPC/状态机/Host 与 GCC ASan/UBSan：

- BLE_ON 初始化、重复回调不重置、退出/重进与退出后拒绝重新初始化；
- enable/disable API 错误、重复开启、过渡状态拒绝；
- BLE-only SYS_TURN_ON 幂等、safe-off 转换、Dual-mode SYS_TURN_ON 仍升级 Classic；
- start 失败、重复 stop、停止完成前阻止 start、超时不冒充 idle；
- 100 轮模拟 scanner 分配/停止/回调后释放，无 ASan/UBSan 报错；
- 20 轮后台线程提前发出失败回调及一次成功回调，验证返回值发布与回调互斥；
- SAL OFF 必须等真实 disable，关闭失败/入队失败不报告 OFF。

这不是实机 100 轮无线测试；真实 HCI reopen、Zblue 线程、USB DATA toggle、
冷启动/共存/长稳仍待板端证据。既有 reader/parser 与 FAT filepath 回归通过。

## 复现与交付

维护者布局、WSL/Linux 仓库根：

```sh
bash tools/build-a733-ble-v143.sh
bash tools/package-a733-ble-v143.sh
```

此脚本核对临时修改及 aipet/aipetllm 所涉及官方路径的 SHA256/缺失状态，
构建在独立 `quickly-openvela/cmake_out/cubie-a7z_nsh_v143_ble_lifecycle`，
不覆盖 v142 ELF。普通 repo-init 布局继续使用 build-a733-wapi.sh 并设置独立
A733_BUILD_DIR；维护脚本也接受 OPENVELA_WORKSPACE。

构建/测试/官方恢复日志保存到 `20261014-v143-*.log`。
`20261014-v143-lifecycle-staged-tests.log` 记录上述追加并发测试与四个实际编译
CLI/scan/state/SAL 文件逐行一致检查（仅归一化末尾 LF）。

### 最终产物

- 修复源码提交 `0b4a3a8`，追加早期并发回调测试 `9883131`；构建未在途中
  修改已暂存的四个官方源码，实际编译源码一致检查通过。
- 内核：`quickly-openvela/cmake_out/cubie-a7z_nsh_v143_ble_lifecycle/nuttx.bin`，
  2577832 bytes，SHA256：
  `266dce6c6526ed860222338ba6cafe060a638a4d4fffacba399f5bfa5b1be896`。
- 同目录未剥离 ELF `nuttx` 保留，SHA256：
  `f8326af6f388082edb30900c8a120cba4d21b69ef24d33ca2cfc552573cffc88`；
  System.map 确认 scan_command_wait_stopped、g_tool_exiting、g_scan_stopping。
  若异常，必须用此精确 ELF 解码 v143 地址，不能用 v141/v142 ELF。
- runtime footprint 36868096 bytes；ARM64 Image magic、file-backed size、
  text/rodata/data 4 KiB 页边界、BSS/栈、header footprint 校验通过。仍用 booti。
- SD 候选：`A733-A7Z-ALL-Files/openvela-a733-cubie-a7z-sd-ble-v143-lifecycle-candidate.img`，
  2147483648 bytes（2 GiB），SHA256：
  `fa3b6592e35a367de29cd1b4fe3b7a6233d3ebdaeea106e6b88bbbd037bb0bf5`。
- 复制已核对哈希的 v142 镜像，只替换 partition 3 的
  `/boot/openvela/a733/Image`，不更改 Boot0/SCP/BL31/U-Boot/DTB 与分区布局。
  提取新镜像内核哈希一致，e2fsck -fn 与 GPT 检查通过；继承源镜像 partition 4
  结尾非 2048-sector 对齐警告，不是本轮新增分区变化。
- GCC 13.4.0、CMake 3.23.0、Ninja 1.12.1，12 jobs 完整构建，日志报告 08:10。
  2952 个构建步骤完成；保留已有 Rust 配置跳过、GGML UNUSED 重定义、Zblue
  poller dangling-pointer、linker build-id discarded 告警，不宣称零告警。
- 本轮 115 个暂存/补丁及 aipet/aipetllm 官方路径 SHA256/缺失状态与构建前
  一致。只证明本轮触及路径恢复，不宣称整个既有官方工作区 git-clean。
- 最终配置归档 `20261014-v143.config`；console / lifecycle / FAT / staged
  source comparison / build / official-restore / layout / package / verify 日志
  均保留在本目录。

整镜像烧录会覆盖 SD 数据，先备份模型及 Wi-Fi/API/SSH/FTP 等配置。
启动可能仍显示 `v141-layout` 诊断标记，因为本轮刻意未更改 BSP；版本确认看
镜像哈希及 bttool 的 already enabled、Scan state、停止回调等新增行为。

## 实机验收门

按 `docs/BLE_OFFICIAL_HOST_TEST.md`，先单客户端完成三轮
enable → state=2 → scan start → status=0 → ScanResult → scan stop → STOPPED →
scan dump=idle → disable → state=0。加入重复 enable、重复 stop，以及扫描中
直接 quit 后重新进入 bttool 的测试。不要同时启动第二个 bttool 来关闭同一适配器。

若出现关闭超时、state=6 不归零、Host failure 或 needs-reboot，停止循环，保存
cat /dev/a733-bt / dmesg / ps；重启恢复。禁止强杀 bluetoothd、删除数据库、
Host open 时私有 probe。BLE 生命周期门通过后再开发 ESP32-S3 GATT/业务 ACK。
