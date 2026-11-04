# 本地 NPU 语音迁移前置证据 v152

2026-10-19。恢复标签 `a733-before-local-npu-voice-v152` 指向
`d52dea934cc6adb950e8659a426f0a4120b943a3`。v150 实机通过与 v151 候选均保留。

本轮不是完整本地语音镜像交付。`reference-audit.json` 只证明下载的四个语音
NBG/词表哈希、头部 CID/版本和 WAV 格式；没有运行它们、没有识别或合成结果。

参考归档（位于仓库外 `A733-A7Z-ALL-Files/references/`）：

| 来源 | 本地文件 | SHA-256 |
| --- | --- | --- |
| GitHub codeload 的 main 内容快照，未取得 Git commit | cubie-a7a-voice-assistant-20261019.tar.gz | af54166cc85522d7585c51cb44fe2fe57b894be929d21d94c5f08fb29f839f08 |
| Radxa 文档公开下载链接，实际是旧 Model Zoo v0.9.0 | allwinner-model-zoo-radxa-20261019.tar.gz | f209bbcbc1222c52035ff0ea9796b9de6cdb96598a1c5cf7aaec2a20ab2673a5 |
| k2-fsa/sherpa-onnx 官方release `tts-models/matcha-icefall-zh-baker.tar.bz2` | matcha-icefall-zh-baker-20261019.tar.bz2 | c3e1889c1b745180445daba6c1f51fb4aeceaff8500da425da6c5ff80e8620c5 |

Matcha资产在构建启动后下载完成；初始 `reference-audit.json` 保留下载前状态，
`reference-audit-with-acoustic.json` 记录补齐资产后的状态，两份不覆盖。
ONNX为75,624,188字节，SHA-256 `ff1cc7f7abdedc4eeec668a4a0dc2e72a0c314c5637d5fce885345b490bd08de`；
lexicon为1,363,705字节，SHA-256 `38b886d46aefa50da6322a64d72fd595d5f4fae1051adb160d647541b1e0a4a2`；
tokens为19,591字节，SHA-256 `56209b2bf609d5ac1d66ede6dae7bf5254bd3f8aa24c4a6823713d5b884d87ba`。
只准备资料，没有执行该ONNX、证明其与NBG数值一致或分发模型。

Git 克隆长时间未完成，已中止；GitHub API 被公共地址限流。随后使用官方 codeload
下载成功并校验归档，不声称已按 Git commit 锁定。参考文件没有覆盖原 Linux 工程。

`voice-golden` 是 Linux ARM64 工具，不会进入固件：真实厂商 API/头文件、最多64路
完整张量、5秒网络超时、精确输入文件长度、完整输出、拒绝覆盖已有输出目录。
默认不执行、不访问开发板。主机 mock 只验证39路资源生命周期，不模拟“识别通过”。
与已有 trace 工具配合前须先完成官方 Linux baseline 并审查资源/地址/许可。

`play22k` 是真正的应用层 PCM 兼容改动，32-tap 分数延迟窗化 sinc，320/147 倍
重采样；不更改底层48k时钟，不改采集/音量/云Agent默认路由，不下载/上传音频。
主机1kHz输出 RMS约7071，RMSE小于1个S16量化单位；这不是声学或硬件验收。

完整复现与后续门槛见 [迁移说明](../../a733/LOCAL_NPU_VOICE_V152.md)。
