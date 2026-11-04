# v2.1pre／v226 镜像与软件封存

2026-11-03。最终固件冻结于 `935a7b10c301f52b00f190d518751576def8583c`，独立预览镜像，不替代v1.1/v219稳定版。[修改及顺序测试](../../PRODUCT_V2_1PRE_V226.md)。

镜像 `openvela-a733-cubie-a7z-sd-v2.1pre-v226-candidate.img`，2,147,483,648字节。
SHA256 `63aa91f38a6223e0b3ae0d549f3be17020916824aa5273c15906697188799683`；内核SHA256 `cff8aa15e8e0c243a78970f416f03be2ead9ad053da4a2ee0a1c1b3038c6eb30`。旁有校验文件；`windows-hash.json`为Windows独立复核。

## 已完成的软件检查

- `host-arm-summary.json`：40项门禁，含继承语音/LLM/MCU/端点门禁，以及实际恢复路径、Wi-Fi判定、真实本机TLS/WebSocket及云协议。网络/MIC/LLM/UART替身明确标注。
- `cloud-recovery-v226.log`：第三次ASR/TTS成功、耗尽后同PCM本地识别再分发一次；凭据就绪但carrier掉线时三次恢复／成功回云；第二句失败不重播第一句／动作；停止、重启及租约清理。
- `wifi-recovery-v226.log`：生产健康判定及实际spawn信号属性测试；父任务屏蔽SIGINT时子任务仍可接收；不是RF/AP验收。
- `actual-wss-summary.json`：本机真实TLS认证、WebSocket分片／ping／掩码／越界、不可信证书拒绝，以及取消／EOF分类；没有调用付费云账号。
- `full-link-summary.json`、`elf-audit.json`：完整ARM构建、NPU ABI、实际ISA、亲和性／截止时间及摄像头软件回归。新增四类v226恢复日志确实在真实固件中，源码冻结与构建产物哈希匹配。
- `package-summary.json`：启动文件／模型／最终镜像回读及未改跨度核验；`sdk-restored.json`：316项SDK文件按字节恢复。
- `summary.json`：最终封存结果及六个旧镜像保留哈希。没有写物理SD/UFS，没有PR或推送。

## 构建和归档过程记录

最终构建为`out/product-v21pre-v226-build-r2`，门禁为`out/v226-product-gates-r1`，完整审计为`out/v226-full-link-r4`。先前r1构建被取消信号／carrier边界修正取代，不交付其中二进制。

早期源码冻结审计因第三方来源JSON的CRLF差异停止；确认无C/C++内容变化后，在单次Git命令中对齐Windows行尾规范，没有忽略代码空白、改全局配置或跳过ABI/ISA检查。最终r4全部通过。

首次封存的镜像及回读已成功，但归档误将继承测试日志名改为v226，复制时失败。修正后显式续封：重新验证镜像、固件、40门禁及完整审计摘要哈希；只补齐证据，不重建镜像，已有证据必须逐字节相同，否则拒绝。保留`out/v226-seal-launch-r1.log`失败记录和`out/v226-seal-resume-r1.log`续封记录。

恢复标签`a733-v2.1pre-v226-image-20261103`，完整Git bundle `out/checkpoints/a733-v21pre-v226-image-20261103.bundle`；不覆盖旧标签或制品。

## 尚未验收

真实云账号连续多轮、AP断电与重连、DNS异常、声学输出、机械动作不重复、停止的实际最坏耗时、整机首音延迟及长稳均待上板。主机故障注入／本机TLS不等于公网或整机成功；不保证3–5秒首音，也不将本版晋升稳定。
