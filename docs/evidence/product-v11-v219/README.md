# v1.1／v219 软件及镜像封存证据

运行时代码 `f6531dbc0db75590ef76904afe4c786ec4bd8503`；打包工具 `67dbf45e0cbdd7b8adcbda5118a3e550441b6885`。
这是新镜像待板测证据，不是真实 AP、真实声音或首音延迟验收。

| 文件 | 证明范围 |
| --- | --- |
| `summary.json` | 制品摘要、原始反馈哈希、基线保留及明确的未验收标志 |
| `host-arm-summary.json` | 19 项语音＋7 项产品门禁；产品 ARM 编译含 13 个单元 |
| `full-link-summary.json` | ELF/ABI/ISA、24 个构造器、内存布局和完整摄像头继承门禁；包含实际固件源码哈希 |
| `elf-audit.json` | 启动/内存布局审计明细；没有为通过构建而放宽旧保护 |
| `sdk-restored.json` | 316 个 SDK 暂存路径按字节还原 |
| `package-summary.json` | 最终 2GiB 镜像、启动脚本、模型、SOUL、MiniCPM5 回读及未改区域的摘要 |
| `resident-lease-v219.log` | 对冻结 v217 实际 provider＋实际 prepare 复现 `-16`，再通过新 owner 修复；引擎是 MOCK |
| `runtime-lease-v219.log` | 实际 RuntimePorts＋规则/聊天分发＋实际 prepare/provider，六轮/两次会话、外线程和取消；引擎/PCM 是 MOCK |
| `wifi-v219.log` | 生产 PBKDF2/HMAC/PRF、RSN 字段/重放事务、Python 独立向量及真实伪终端密码编辑/取消/恢复；USB 为 MOCK |
| `product-service.log` | 实际常驻服务的停止/重启、识物 lease 交接、无凭据云拒绝；设备和语义适配为 MOCK，与上一行真实 RuntimePorts 测试互补 |

Wi-Fi 日志中的 PMK/PTK 仅来自公开测试密码 `password`、SSID `IEEE` 等测试向量，不是真实凭据。
真实板测原文含 SSID/MAC，仅私有保存在 `out/v219-board-feedback-20261031/`，不随本目录公开。

新镜像：`openvela-a733-cubie-a7z-sd-v1.1-v219-candidate.img`，2,147,483,648 字节。
SHA256：`fe7a28ce9996e3344a1866610c7cdb9137a1088965b0207161df6ffab4c3359f`。
旧 v217 镜像 SHA256 仍为 `98daee1f3b4653d3d65a14a3e92c2713560aac2fd6183946d2b06b7a3c79bc21`。

没有改全系统调度，没有改 NPU 硬件执行路径、底层 I2S 时序或摄像头/YOLO 六路校验。
旧版部分视觉/声音有用户板测确认，但新固件的 Wi-Fi、常驻连续多轮、声音、停止/恢复仍待测试；没有云账号调用、写物理盘/UFS、PR 或推送。
完整操作及恢复见 [v1.1 记录](../../PRODUCT_V1_1_V219.md)。
