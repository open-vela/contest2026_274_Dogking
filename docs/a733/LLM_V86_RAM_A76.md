# LLM v86：RAM 模型与单 A76 前向对照

前置恢复点：`a733-smp-8cpu-a76-v85-basic-verified`。不修改官方公共
源码，不改变已验证的八核启动配置；所有新增实现位于比赛仓库 overlay。

新增命令 `aipetllm forwardfast model.gguf token-id`。当前任务绑定 CPU6，
检查 MIDR part=d0b 后，将模型以 256 KiB 分块读入完整 RAM 缓冲区，
通过官方工作区 libc 的只读 `fmemopen` 复用已有 GGUF/量化/前向代码。
分别输出模型载入和计算时间。正常退出及受控错误时释放模型缓冲区并
恢复原亲和掩码。普通文件最大 1536 MiB；不足的连续内存会明确报错。

这是单次命令内的模型驻留：结束后释放，不是跨命令持久缓存，也不是
多 token 生成。仅一个 A76 计算线程，CPU7 和六个 A55 尚未承担矩阵
并行计算。原 `forward1` 行为保留作为基准。没有调整 CPU PLL/频率。
不要同时运行多个 forwardfast，避免多个约 1.1 GB 的模型副本。

## 实机检查

```text
aipetllm cpucheck
free
time "aipetllm forwardfast /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf 9707"
free
```

首次实机检查应等待正常结束，检查结束后内存恢复。预期数值沿用已有
计算路径：final-state-crc=17a04f7d、logits-crc=7fdfee67、argmax=6233。
如不同，保存逐层 CRC，不得仅凭成功返回断言数值正确。CRC 也仅是
回归证据，尚未替代与独立参考实现的误差验证。

旧单 A55+SD 流式 forward1 耗时 473.6016 秒；新的加载/计算数据必须
实测，不能把模型加载耗时直接当作每 token 推理速度。

后续：保持模型和解析目录的生命周期、矩阵行按两 A76/异构核心分工、
多 token prefill/持续 KV cache/采样循环、量化 NEON 回归、长时间温度
和网络/NPU 并发稳定性。当前仍只是单 token 前向，不能进行完整对话。

## 候选发布存档

构建源提交 `7139ad3`，WSL 增量构建通过。新增 RAM 前向、A76 包装和
官方 libc fmemopen 已链接。官方 arch.h 临时补丁已恢复，无 .orig 残留。

- 镜像：`A733-A7Z-ALL-Files/openvela-a733-cubie-a7z-sd-llm-ram-a76-v86-candidate.img`
- 镜像大小：2147483648 字节。
- 镜像 SHA256：`4f74489b3fabc2ed0d0663cdc1c77b3d1a1c98638c36c5b9e510fba01ee11d31`。
- 内核 SHA256：`d55c390b75956832c5923d19ac380042b0d4cc73055816892678becf69ff3dda`。
- ext4 检查通过，嵌入内核回读一致，GPT 报告无问题；继承的分区4
  非2048扇区尾部对齐提示不变。
- 恢复标签：`a733-llm-ram-a76-v86-candidate`。
- 恢复包：`A733-A7Z-ALL-Files/archives/llm-v86/a733-llm-v86.bundle`。
- 构建/打包/校验日志：同一目录 `build.log`、`package.log`、`verify.log`。

尚未在开发板验证该新命令，不宣称数值和速度实测通过。整盘烧写会
覆盖 TF 卡现有分区和数据，请先备份模型及板端配置。本地基底镜像
不包含用户后来上传到 TF 卡的 Qwen GGUF；烧写后需重新传入模型。

## v86 实机前向回归通过

用户实测 `forwardfast`：确认 CPU6 Cortex-A76，一个计算线程。
模型载入 1117320736 字节，load=158.1617 秒，compute=1.1492 秒，
命令总耗时 159.3263 秒。载入吞吐约 6.74 MiB/s。
28 层 state CRC 全部与旧 forward1 记录一致；最终
final-state-crc=17a04f7d、logits-crc=7fdfee67、argmax=6233，
logit=10.4909735。由此确认本次 RAM/A76 路径的数值回归通过。

旧 473.6016 秒包含 SD 流式读取，新 compute 仅计算阶段，两者不能用来
宣称纯 CPU 提升倍数。含载入的总耗时改善约 2.97 倍；计算阶段倒数
约 0.87 次单 token 前向/秒，不等同于持续生成 tok/s。
当前命令结束仍释放模型，重复命令会再次加载；必须实现持久生命周期
才能摊销 158 秒开销。多 token KV cache/生成、内存释放实测和长期
压力测试仍待完成，不将单次回归视为完整聊天系统通过。

恢复标签：`a733-llm-ram-a76-v86-forward-verified`。
恢复包：`A733-A7Z-ALL-Files/archives/llm-v86/a733-llm-v86-forward-verified.bundle`。
