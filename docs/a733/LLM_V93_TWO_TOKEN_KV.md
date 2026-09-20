# v93：完整 28 层双 token 因果 KV 验证

基于已通过线程池/缓存回归的 v92，保留页对齐的 SMP 栈和原调度策略。

## 新增能力

`aipetllm forward2fast model.gguf token1 token2 [cores]`。

一次调用中按顺序处理两 token，每层保存两个位置的 K/V，第二个位置使用历史 K/V、因果 GQA、稳定 softmax，再执行输出投影、残差、SwiGLU 和 LM head。默认 cores=2,3,4,5,6,7；模型 RAM 缓存和持久线程池跨调用复用，KV 仅在本次调用期间有效，结束后释放。

Qwen2 RoPE 经本地 `llama.cpp-0.1.2/src/llama-model.cpp` 的 `llama_model_rope_type` 和 `src/models/qwen2.cpp` 核对，使用 NeoX split-half 布局。早期 `qkv/attn2` 诊断的 NORMAL 相邻成对 RoPE 尚未重新审计，旧位置一 CRC 不能作为真实 Qwen2 多 token 正确性的参考。

没有实现跨命令持久会话、任意长度 prefill、采样、连续生成或聊天封装。不是完整离线助手。

## 板端测试（逐行执行）

```text
time "aipetllm forwardfast /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf 9707"
time "aipetllm forward2fast /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf 9707 1879"
aipetllm cacheinfo
time "aipetllm forward2fast /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf 9707 1879"
aipetllm cacheinfo
aipetllm unload
free
```

首 token 所有层和最终输出应与 v92 相同：final-state=17a04f7d、logits=7fdfee67、argmax=6233。双 token position=1 不应等同于独立 token=1879 的无历史前向，两次相同序列必须得到相同 CRC/argmax。保持相同 mask，created 不再增长；卸载后 workers=0。

双 token 每次矩阵计数预期新增 394（每 token 28*7+1），旧单 token 新增 141。first token 不重算历史，但本命令还未提供跨调用增量会话。

用户发回两个位置完整数值后，需要与 Linux/llama.cpp 同一 GGUF、同 token 序列、同 Q8_K 输入量化数值路径作参考核对。编译通过和重复一致不能替代模型正确性验证。

整卡烧录前备份模型、密钥及配置。源码恢复 bundle、ELF/Image/System.map 和日志独立存档；不覆盖 v92 恢复版本，不永久修改官方代码。

## 离线构建校验

最终构建源为 70708a9（包含 ff0e1fb 的 NeoX 修正），最终构建及验证退出 0。首轮 build.log 是修正前快照，不作为发布内核；发布对应 build-final.log。

- 镜像：`openvela-a733-cubie-a7z-sd-llm-kv2-v93-candidate.img`，2147483648 字节。
- 内核：1721792 字节，SHA256 `e346521c5447af5c2b5d9be83530e1817b747d317b3ad4f1873c75042ca072f7`。
- 镜像 SHA256：`91b989eaa53be8ebb80b9b9b74bf34ec26313d2ca361aa6b7900921167ef8a9f`。
- 内核回读相符，ext4 检查通过、GPT 无错误（继承分区 4 尾部对齐提示）。
- g_cpu_idlestackalloc=423f3000、g_idle_topstack=42403000，页对齐断言通过。
- 对应 ELF/Image/System.map 和日志保存在 `archives/llm-v93/`。构建临时覆盖已恢复，官方 apps/system/aipetllm 恢复为构建前不存在的状态。

尚未宣称双 token 实机运行或参考数值验证通过。

## 板端基本验证通过

用户提供日志来源：`C:/Users/Lenovo/.codex/attachments/58f765a0-905c-4bb2-9d07-f417d77cd57a/pasted-text.txt`。

正常进入 NSH，无异常 F0。序列 9707,1879 连续执行两次，两个位置全部 28 层 CRC 均重复一致。position=0 复现 v92：final-state=17a04f7d、logits=7fdfee67、argmax=6233。position=1：final-state=d2ec6d95、logits=96794b9a、argmax=4894、最大 logit=16.4877815。

首次模型加载 175.0683 秒、计算 1.0956 秒。第二次 cache-hit 无模型 SD 读取，计算 1.0915 秒、整条命令 1.1087 秒。此为两个输入位置的 checkpoint 耗时，不是连续生成速度。

pool pid=14、mask=fc、workers=6、created=6 不变，矩阵计数 394→788，符合每次 394 的预期。unload 报成功，随后 used=276800、nused=105；该日志没有卸载后的 cacheinfo，尚未独立确认此轮 workers=0，也没有多轮压力数据。

记录为 `a733-v93-two-token-basic-verified`，保留源码 bundle。已确认实机运行及重复性；Linux/llama.cpp 独立数值参考、任意长度 KV 会话、prefill/连续生成仍未完成。不得将这一标签理解为完整聊天或参考精度验证通过。
