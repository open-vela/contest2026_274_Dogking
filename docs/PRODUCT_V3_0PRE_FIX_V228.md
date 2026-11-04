# Vela v3.0pre-fix / v228：强制联网模式不再降级本地

日期：2026-11-04。候选版；稳定版仍为用户验收的 v1.1/v219。保留 v227 镜像及 Git 恢复点，不创建 PR、不推送，不操作物理 SD/UFS。

封装完成：[发布证据与哈希](evidence/product-v30prefix-v228-image/README.md)。独立2GiB镜像 SHA256 `fce43893a2cfe0b40160dbf83017fa17b77c0bd179dc4f0793c183399332e09c`；42项继承/新增软件门禁、最终链接审计、SDK319项还原、镜像回读和Windows哈希通过。新版实机仍待验证。

## 原因及证据

用户先执行 `aipet mode cloud`，随后 `aipet run` 却出现 `frozen=local reason=cloud-unconfigured-or-cooldown` 并预加载 MiniCPM5、本地 ASR 和 vocoder；相同环境下 `aipet vision once` 仍为 `forced-cloud` 且讯飞 TTS 两句成功。

原因是 v226 的常驻服务曾将 CLOUD 强制转换成 AUTO，云失败后的两分钟冷却期使服务选择本地；云 ASR 失败还可将保留 PCM 送本地识别。前台识图没有这种转换，表现不一致。这是软件策略错误，不是用户未执行 cloud 命令。

## 本版政策

| 用户模式 | ASR / LLM / TTS | 云失败后 |
|---|---|---|
| `cloud` | 全部云端，不加载本地语音/LLM 模型 | 符合条件的网络/ASR/TTS操作最多3次尝试；仍失败则等待或丢弃本轮，服务保留；绝不转本地 |
| `local` | 全部本地 | 明确报告本地错误，不上传音频 |
| `auto` | 明确允许自动选择 | 保留原来的网络恢复后降级政策；不要在要求纯联网时使用 |

强制 CLOUD 忽略 AUTO 两分钟降级冷却，但仍检查 Agent 就绪、时间和云凭据。缺少这些前提或网络不可用时显示 `waiting-cloud-MIC-OFF`，三次恢复后5秒可取消等待再检查；不会假装 MIC READY，也不会开本地模型。网络恢复且前提满足后继续云端监听，无须重新 run。

云 ASR 三次失败时丢弃本轮音频，不执行动作、不送本地重新识别。LLM/动作不自动语义重放。云 TTS 仍只重试尚未播放的句子，不重复已播前缀或动作。`aipet stop` 保留合作停止，不强杀任何 NPU/音频所有者。

`aipet mode`、设置模式反馈和 `aipet audio service status` 明确报告是否允许本地自动回退。手动 `aipet ask` / `aipet vision once` 同样遵守严格 CLOUD，不因 AUTO 冷却改成本地。

## 测试顺序

### 升级与保留配置

本版是应用层路由修复，不改变 v227 的分区、模型或启动交接协议。若当前使用的确为 v227，并且有能可靠读写 SD 第3分区 ext4 的工具，可在开发板正常关机后备份原 `/boot/openvela/a733/Image`，再用本版封存目录中的 `Image` 替换该文件，以保留第4分区 `/data` 内的 Wi-Fi/SSH/云凭据、音量和模式设置。不要误写到 FAT 数据分区、不要改 DTB 或 UFS。发布证据须记录 `Image` 与 `nuttx.bin` 的 SHA256 相同；没有完成镜像封存前不提供未验证的内核升级文件。

整卡镜像会覆盖目标 SD 的分区和配置，不能承诺保留云密钥；烧录前先备份。镜像生成不等于已替用户烧录。升级后用 help/service status 确认实际运行的是 v228。

### 板上命令

先确认网络、时钟、Agent 和云凭据已按旧版配置。不要在串口输入尖括号占位符，不重新公开密钥。

```text
aipet stop
aipet audio service status
```

等待 `active=0` 后再设置模式；活动服务中更改模式会被拒绝。

```text
aipet mode cloud
aipet mode
aipet audio providers
aipet audio output i2s
aipet run
aipet audio service status
```

应看到 v228、`mode=cloud automatic-local-fallback=0`、`frozen=cloud reason=forced-cloud`。就绪时为 `READY MIC ON`；网络/凭据未就绪时为 `waiting-cloud-MIC-OFF`，不是本地模型加载。测试三轮聊天、识物和音量设置，所有语音后端应为 cloud。

保持服务运行，在另一终端或设备端短暂断开/恢复网络；不要求再次更换路由器。观察无 MiniCPM5 preload、本地 `asr-native-prepare` 或本地 vocoder prepare；允许当轮云错误，但不应转本地。恢复后继续说新的一句话；不能自动重放断网前的运动命令。

```text
aipet stop
aipet audio service status
```

等待 `active=0`；再执行 cloud / run 验证可恢复。只有主动需要离线时，停止后执行 `aipet mode local`。不要用 `auto` 来测试“联网绝不降级”。

## 验证边界

新增测试执行真实常驻服务逻辑，覆盖网络不可用、配置未就绪、旧冷却期、ASR三次失败、LLM失败、TTS三次失败、下一轮和停止恢复。网络/MIC/ASR/LLM/TTS/UART用明确 mock，不是板测；ASan/UBSan通过也不代表真实网络时延达标。保留完整继承回归、ARM编译、ELF/ABI/镜像读回检查。

本版不宣称修复 706E 网络的底层接收超时。用户已确认 v227 换至 ZBCK-E-5G_Wi-Fi5 后讯飞 TTS 正常；该事实不能替代 v228 的严格联网常驻多轮上板验收。
