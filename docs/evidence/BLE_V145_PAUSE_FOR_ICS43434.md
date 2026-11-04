# BLE 暂存与 ICS43434 开发入口（2026-10-15）

按用户要求暂停蓝牙，待 ESP32-S3 下位机可用后联合开发。蓝牙代码不回退、不删除。

- 恢复提交：`884c1825b095a65150245b9019bfcd2d056a259e`。
- 恢复分支：`backup-before-ics43434-v146`。
- 通过标记：`a733-ble-v145-runtime-scan-coexist-pass`。
- 完整恢复包（仓库旁）：`a733-before-ics43434-v146.bundle`，`git bundle verify` 通过。
- 恢复包 SHA-256：`0756db902a5562ce6615f7167e01f0523309ff5ec5ab15e29f629c584d40c354`。
- 镜像保留：`openvela-a733-cubie-a7z-sd-ble-v145-gatt-client-candidate.img`。
- 镜像 SHA-256：`d66bc9afdf8ecc690fca77b8d42b2f1b177a40cfbdeab9c2c1be893a57f40d32`。

已通过范围是控制器查询、官方 BLE 扫描/停止/关闭/重新进入和轻载 Wi-Fi 共存，
详见 [实机证据](BLE_V145_SSH_RUNTIME_ACCEPTANCE.md)。尚未完成真实 GATT 对端、
ESP32 双向数据、下位机动作及断线恢复验收；不能将扫描通过称为智能家居链路完成。
后续从 `app/a733home` 的候选 GATT 流程继续，不重做已通过的 USB HCI 传输。

用户现更换为 ICS43434；旧 INMP441 曾反接电源，存在损坏可能，但这不是对旧驱动
全部问题的定论。v138 的 FIFO 修正、计数/时长/文件提交保护和失败录音证据全部保留。
新一轮使用独立 v146 构建/镜像与 ICS 命令、文件名；不覆盖旧镜像和录音。

ICS 首先验证 48 kHz、标准 I2S、24-bit、32-bit 双槽及真实声学输入。
不在启动路径自动发声/录音，不提前接入 Agent/ASR，不宣称官方 audio upper-half 已完成。
