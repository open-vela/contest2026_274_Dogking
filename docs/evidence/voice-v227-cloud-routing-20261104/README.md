# v227 强制联网却降级：用户板测原始证据

2026-11-04 用户附件原样保存为 [board-v227.txt](board-v227.txt)。SHA256：`769d9c7693dcb797e1d384719cb15aece563b10b420f6e24907e75b11b96860b`。

日志串口多任务输出有交错，不修改原始文本，不根据交错内容伪造逐字精确日志。

- 用户执行 `aipet mode cloud`，随后 `aipet run` 却选择 local / cloud-unconfigured-or-cooldown，并加载 MiniCPM5、本地 ASR/vocoder。
- 用户停止服务后，`aipet vision once` 为 forced-cloud，真实相机/YOLO结果生成两句云 TTS，均 status=0，clauses=2/2。
- 这证明常驻与前台路由政策不一致，不是“用户没选 cloud”。源码中的 cloud→automatic 转换与保留 PCM 本地识别分支已在 v228 修复。
- 此文件是 v227 的用户板测，不是 v228 验收。v228 ASan/UBSan / ARM / 镜像验证另存；无新硬件测试不能晋升稳定版本。

用户在上一条消息另确认：v227 换至 ZBCK-E-5G_Wi-Fi5 后，`今天的语音测试已经完成，请继续下一步。` 云 TTS 成功，握手655ms，clauses=1/1，合成/预取阶段6284ms。该耗时不是首音频延迟，也不能证明 706E 的底层问题已经修复。
