# v218 云语音软件候选证据

2026-10-31。运行时代码：`28929a2999586b621230729b74a205c132294ecb`。
基线恢复标签 `a733-v1.0-v217-audited-20261031` 及旧镜像均保留。

本目录为可随 Git 保留的小型验证材料，不包含真实服务密钥、TLS 私钥、音频、模型或大镜像。

## 通过的软件检查

- `summary.json`：汇总、产物 SHA256、源码版本及明确的未验收标志。
- `full-gate.json`：完整固件链接、ELF/ABI/ISA/布局及原有摄像头软件回归；硬件行为是模拟验证，不是板测。
- `ARM-units.json`：13 个实际 NuttX/AArch64 编译单元的源码 SHA256、栈帧；安全随机接口 C ABI 另经符号表检查。
- `protocol-provider.json`：真实 mbedTLS 签名与独立 Python HMAC 对照，提供商回复为 MOCK。
- `TLS-WebSocket.json`：本机真实 TLS/WebSocket，含错误证书拒绝、分片、Ping、掩码及超限检查。
- `Edge-HTTPS-handler.json`：本机真实 HTTPS 网关入口，上游为 MOCK；不是 Edge 服务或 FFmpeg 验收。
- `suite-*.log`：配置隐藏输入/加密、音频、产品服务与路由回归。
- `sdk-restored.json`：315 个 SDK 暂存路径已按字节还原。

独立缓存：`quickly-openvela/cmake_out/cubie-a7z_nsh_v218_cloud_voice`。
候选 `nuttx.bin`：7,234,408 字节，SHA256：

```text
c4cca0df64af446e51abc82c950284221d26008d7af2d4d84c9d4282bb0726be
```

## 尚未验证

未使用真实云凭据、未进行付费请求、未运行真实 Edge 合成或 MP3 解码、未上板验证声音，也没有新的整机首音延迟结论。
没有生成新 SD 镜像，没有开启开机录音或运动，没有 PR 或远端推送。
Edge 为可选电脑 HTTPS 网关方案，不是 NuttX 原生 Edge 客户端，也不是微软正式 Azure Speech 开发接口。

配置与测试步骤见 [多服务商说明](../../CLOUD_VOICE_PROVIDERS_V218.md)。
