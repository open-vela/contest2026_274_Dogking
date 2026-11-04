# v141 启动 / SMP / 计时 / Wi-Fi 扫描实机通过

用户回传一轮实际启动和 NSH 测试，原始日志保存在 `20261013-v141-boot-smp-scan-pass.txt`。日志中的 `M0 v141-layout` / `M1` 与本轮修复候选一致；未提供板端内核 checksum，不能把 PC 端镜像校验当成板端读回校验。

## 本次可记为通过的范围

- 经 BL31 以 EL1 进入 `0x40200000`；出现 M0 → M1 → NSH，本次记录没有 FAULT/assertion，也没有重复异常输出。
- ps 显示 CPU0..7 IDLE；cpucheck 逐核绑核执行通过：CPU0..5 Cortex-A55（MIDR 412fd050），CPU6..7 Cortex-A76（MIDR 414fd0b1），各核 checksum=81e62f98，原 affinity=ff 已恢复。
- `time "sleep 5"` 为 5.0009 sec。
- wlan0 已注册，Wi-Fi firmware/USB runtime/RF 初始化通过；`wifi scan` 得到 10 项结果，覆盖 2.4/5 GHz。
- 官方 H4 传输注册返回 0。这里只证明注册成功，未证明 Host enable 或无线 BLE 扫描成功。
- GPT/FAT 挂载 /data 读写、NSH 可用。NPU/UART4/I2S 等日志仅说明节点/初始化检查，不据此记模型推理或音频功能通过。

## 未通过或未执行的范围

- `ifconfig` 为 UP / 0.0.0.0，尚未执行本轮 Wi-Fi connect、WPA2、DHCP 或 ping；不应写成“网络连接通过”。
- 没有 v141 私有 HCI probe、bluetoothd/bttool enable、BLE ScanResult、ACL、配对或 GATT 结果。
- 未执行多次冷启动、Wi-Fi/BLE 共存、长稳/内存压力测试。
- 外部 USB UVC probe=-19（缺设备/当前探测未找到设备），日志明确 not audio-ready；不记为摄像头/USB 麦克风通过。I2S 开发仍暂停。

## 恢复点和产物

- 修复源码提交 `9814948`，构建/镜像证据存档提交 `34d2288`。
- 镜像仍使用 `A733-A7Z-ALL-Files/openvela-a733-cubie-a7z-sd-ble-v141-boot-layout-candidate.img`；没有为通过状态重新生成或覆盖镜像。
- PC 端 image SHA256 `a81e1005c4ba33b107b502b347dd7ec5e8e74908c17dde0f0eeaf1e1ee5caf6b`，kernel SHA256 `0fa1cbc1b4bbe259f74febf7ce3da5f1b061f5e0653f063b800acd5d8daf659e`。
- v139、v140、v141 镜像和此前过程文档均保留。

## 下一道门

可以恢复官方 BLE Host 的分层验收，但先在没有 Host 占用时核对 `/dev/ttyHCI0` 与 HCI query，再启动一次 kvdbd/bluetoothd。检查 enable/state 结果后才启动已知广播源的被动扫描；异常即停止并保存诊断，不直接跳到 ESP32-S3 控制动作。完整命令见 `docs/BLE_OFFICIAL_HOST_TEST.md`。

这次正常启动支持本轮布局修复有效，但几项布局变更一起测试，不能单凭一轮结果排他地确定 v140 首次异常的唯一根因。
