# v94：合并预填充、因果 KV 和连续生成

基于 v93 双 token 实机验证状态；保留 SMP 页对齐栈修复、RAM 模型缓存、持久线程池和默认 CPU2–7 策略。系统其他任务的调度策略不变。

## 合并能力

- `generateids MODEL IDS COUNT [CORES]`：输入 1–64 个十进制 token ID，逗号分隔。
- `generate MODEL "raw text" COUNT [CORES]`：复用现有 Qwen2 BPE，提示词最多 4096 字节、64 token。
- COUNT 为 0–64：0 仅预填充；大于 0 贪心 argmax 连续生成，遇 GGUF eos_token_id 停止。
- 每层每位置的 K/V 在本次命令中保存，后续位置不重算历史；最大处理位置数 127。Qwen2 使用 NeoX split-half RoPE。
- 正常结束后解码生成 token 为文本；目前不是实时文本流。模型和线程池跨命令保留，KV 每次命令结束释放。
- 可选 CORES 如 `6,7`；缺省 `2,3,4,5,6,7`。输入数字边界、重复核心和非有限 logits 检查。

这是原始文本续写入口，不是已经完成的聊天助手。尚未加入 ChatML 对话模板、跨命令对话 KV、采样参数、ASR/TTS 或 NPU LLM 加速。BPE 的完整 Unicode 分类和特殊 token 识别仍需审计；不要手工把 ChatML 标记拼成普通文本当成特殊 token。

## 同一次烧录的测试清单

命令均在一行执行。第一次加载模型约 175 秒；随后复用 RAM。

```sh
aipetllm generateids /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf 9707 1
aipetllm generateids /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf 9707,1879 4
aipetllm generateids /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf 9707,1879 4
aipetllm generate /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf "Hello world" 4
aipetllm cacheinfo
aipetllm unload
aipetllm cacheinfo
free
```

单 token 第一预测应为 6233；双 token 第一预测应为 4894（v93 position=1）。相同序列两次应有相同生成 ID、层 CRC，created 不随矩阵次数增长；文本入口应与 9707,1879 数字入口相符。卸载后 workers=0。也应测试 COUNT=65、空 CSV、尾逗号、超词表 ID、重复核心，确认安全拒绝。

## 验证边界和后续任务

编译和镜像检查不能替代板端执行。连续生成需实机验证，并与 Linux/llama.cpp 同 GGUF、同输入序列独立核对；重复一致不等于模型精度正确。v93 只有运行重复性验证，没有独立参考精度验证。EOS 提前结束和 64-token 边界需要额外测试。

任意 kill/Ctrl+C 的清理尚未审计，不宣称可随意中断。模型缓存互斥保护和关闭 pthread cancellation 仅覆盖已有正常调用路径。高负载网络并行、长时间生成、重复加载/卸载内存压力仍待测试。

保留 v93 镜像、源码标签和 `a733-v93-before-merged-generation.bundle` 回退。构建前保存源码提交/bundle；通过临时 overlay 构建并恢复官方文件。整卡烧写前备份 TF 卡模型、密钥和配置，生成镜像不含用户后来传入的 1.1 GB GGUF。

## 主机编码回归

`tools/test-aipetllm-encode-api.cxx` 是独立主机测试，不参与固件编译。WSL g++ C++17 编译，与本地 `llama.cpp-0.1.2/models/ggml-vocab-qwen2.gguf` 运行退出 0：文本 Hello world 返回 9707,1879；容量不足、空提示词、空输出指针拒绝。该测试只验证编码接口，不验证 Transformer 生成精度。

## 构建和镜像存档

固件源码提交 `4ce2892`；编码回归工具提交 `0562854`。构建退出 0，内核 1721792 字节。ELF/Image/System.map、构建、封装、验证和编码回归日志位于仓库本地 `archives/llm-v94/`（不随代码上传）。官方工作区 `apps/system/aipetllm` 已恢复为构建前不存在的状态，临时覆盖由构建脚本 EXIT 恢复。

- 镜像：上层目录 `openvela-a733-cubie-a7z-sd-llm-generate-v94-candidate.img`，2147483648 字节。
- 内核 SHA256：`73e9ad848612c50a6b1f448ac70d8071b229abfb783a5b8cc08e06280f968e91`。
- 镜像 SHA256：`3ec65add737dbadf911fb4d318d5ea00060c9628f4943ab4c2a155fd577952a8`。
- 内核回读一致，ext4 检查通过；GPT 无错误，仅继承分区 4 尾部不是 2048-sector 边界的提示。
- `g_cpu_idlestackalloc=423f3000`，`g_idle_topstack=42403000`，页对齐断言保持。

这是候选版本，尚未得到 v94 板端连续生成日志。不可把“镜像校验通过”记录成“生成实机验证通过”。

## 首次连续生成实机记录

用户日志来源：`C:/Users/Lenovo/.codex/attachments/a1c3a033-8e40-4d76-80df-80818e16fac5/pasted-text.txt`。以下记录更新前述候选状态，但不是完整精度验收。

`generate MODEL "Hello world" 8` 成功返回 NSH：BPE 为 9707,1879；默认 mask=fc、6 workers；模型加载 174.9803 秒，计算 4.3159 秒。共处理 9 个位置（2 个提示 token 和 7 个反馈 token），输出 8 个 token：271,2,2585,311,1855,264,501,1196。解码 28 字节，为两个换行加 `# How to create a new user`。这是原始文本续写而非 ChatML 对话。

计算含预填充、LM head 和诊断输出：8/4.3159≈1.85 个输出 token/秒，仅为本次整体吞吐；不能把它作为纯解码稳态速度。首次 175 秒加载不包含在该计算吞吐内，最终文本解码也在计算计时外。

position=0 完整复现基线：final-state=17a04f7d、logits=7fdfee67、argmax=6233。**position=1 尚未通过旧版本数值回归**：本次 layer[0]=c309f7a8、final-state=fcdda499、logits=f5c952cb、argmax=271；v93 相同输入为 layer[0]=6b3938ef、final-state=d2ec6d95、logits=96794b9a、argmax=4894。差异从第一层已出现，应核对泛化 attention 的累加/浮点路径和 KV 布局，并与独立参考对比，不得直接判定新旧哪个正确。

此日志没有第二次生成、cacheinfo、卸载或内存压力记录，不能据此宣称线程池长期稳定、KV 释放无泄漏或 EOS 提前停止已测试。用户报告此前停顿是 CPU 过热降频；日志无温度、频率或热管理证据，暂按现场观察记录，原因未独立确认。

当前结论：单次实机连续生成、文本解码和正常返回已确认；数值回归、独立参考精度、重复稳定性和散热条件下的性能仍待核对。下一步优先复测同输入的 forward2fast、generateids 和 generate，定位第二位置的差异；然后加入可靠特殊 token/对话模板，而不是直接把此输出当作聊天效果验收。
