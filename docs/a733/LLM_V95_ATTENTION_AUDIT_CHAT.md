# v95：attention 对照诊断与单轮 ChatML

## 背景和证据边界

v94 实机已生成 8 token 并正常返回；相同 Hello world 的第二位置 argmax 与 v93 不同（4894→271），第一位置则完全一致。比较源码可见 KV 的分配、写入、读取均已统一改为 capacity 步长；尚无证据证明该布局损坏。泛化 attention 同时将两项显式表达式改成循环累加，浮点求值/指令融合可能改变结果，但**这还不是已证实的根因**，也不能认定 v93 就是精度正确的参考。

v95 在 position=1 保留 v93 的显式两项 context 表达式作为兼容基线，并在同一 Q/K/V、同一 softmax 上计算泛化表达式，报告 changed 和 maxabs；随后选用两项表达式。每层打印 Q/K/V/context CRC，便于判断偏差是否发生在输入投影之前。更长历史仍使用泛化累加。不把旧 CRC 恢复或两个表达式相等当作独立模型精度验收。

## 新的对话入口

```sh
aipetllm chat /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf "Hello" 16
```

单轮 ChatML：system=You are a helpful assistant.；随后 user 消息和 assistant 起始标记。im_start/im_end 从模型词表按字符串查找并直接写入 ID，不经普通 BPE。当前 Qwen2 词表为 151644/151645。普通角色、换行、正文仍复用现有 BPE，完整 Unicode 类别审计尚未完成。用户文本包含 `<|` 时拒绝，以免控制标记注入。最大模板加正文共 64 输入 token、4096 字节正文，最多生成 64 token。

chat 遇模型 EOS 或 im_end 停止，控制 token 不作为回答文本解码。只在最后一个提示位置计算 LM head，前面位置保留完整逐层 KV；这不改变后续 Transformer 的状态。chat 每位置仅打印第一/最后层 CRC，保留进度，减少串口压力。generate/generateids 保留完整诊断。最终文本仍在生成完成后一次性解码，非流式。

仍未实现跨命令对话 KV、多轮聊天、采样、Ctrl+C 安全取消、ASR/TTS。模型/线程池仍驻留，unload 释放模型和工作线程。不得关闭热保护或把用户报告的降频当作已经测量到的事实。

## 一次烧录回归顺序

```sh
aipetllm forward2fast /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf 9707 1879
aipetllm generateids /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf 9707,1879 4
aipetllm generateids /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf 9707,1879 4
aipetllm generateids /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf 9707,1879 1
aipetllm chat /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf "Hello" 16
aipetllm cacheinfo
aipetllm unload
aipetllm cacheinfo
free
```

请保留 attention-audit 全部行、第二位置层 CRC 和 argmax。若显式表达式也不恢复 v93，要继续核对 Q/K/RoPE、模型一致性、编译求值和数据路径，不能直接宣布修复。独立 Linux/llama.cpp 同 GGUF 参考仍必要。chat 对话效果需实机验证；EOS、长度边界和重复压力另外测试。

forward2fast、generateids COUNT=1 和 COUNT=4 的第二位置应一致：三者分配 capacity 分别为 2、2、5，预填充输入相同。若差异随 capacity 改变，应优先检查步长、越界或内存依赖；若同一 v95 三者一致而仍与 v93 不同，继续按阶段 CRC 和浮点求值核对。可用 6,7 核心列表作对照，但不预设多核心就是差异原因。

## 已完成的主机回归

WSL g++ C++17 运行 `tools/test-aipetllm-encode-api.cxx` 退出 0。原始 Hello world 返回 9707,1879；ChatML 共 21 token，包含三枚 im_start、两枚 im_end 和一组连续 9707,1879，stop=151645。容量不足、空输入、空输出和用户控制标记拒绝。此为接口/模板结构检查，不是独立 tokenizer 或 Transformer 精度验证。

v94 源码回退 bundle：上层目录 `a733-v94-before-attention-audit-chat.bundle`。所有实现保存在仓库 overlay；每次构建前提交和 bundle，临时覆盖官方环境后由脚本恢复；新镜像独立命名，不覆盖 v94/v93。整卡烧录前备份 TF 模型、密钥和配置。

## 候选构建存档

固件源码提交 `cc79e5a`；构建、封装及验证均退出 0。官方工作区 `apps/system/aipetllm` 已恢复为构建前不存在的状态，其余覆盖由构建脚本 EXIT 恢复。ELF、Image、System.map、构建/封装/验证/主机编码日志位于本地 `archives/llm-v95/`。

- 上层镜像：`openvela-a733-cubie-a7z-sd-llm-chat-v95-candidate.img`，2147483648 字节。
- 内核：1729984 字节，SHA256 `a48bc0d0b3dbb7395c701f911cd1a1c0ec2e228c8eab8661ffae5159f55a43db`。
- 镜像 SHA256：`f56d01c63676b0e748b1067a479ba406faaf0064b17e52ee2fd45449e12114f7`。
- 内核回读一致，ext4 检查通过，GPT 无错误（保留原分区尾部对齐提示）。
- `g_cpu_idlestackalloc=423f5000`、`g_idle_topstack=42405000`，页对齐保持。

当前没有 v95 板端日志，不宣称双位置数值差异已修复，也不宣称 ChatML 的回答效果已通过。数值和对话回归是烧录后的首要任务。

## 实机数值回归及单轮聊天基本验证通过

后续用户日志：`C:/Users/Lenovo/.codex/attachments/2d6e38c6-2ca9-4798-9cf8-108eb13141b0/pasted-text.txt`，更新上述候选状态。正常进入 NSH，两个命令均正常返回。

### 双 token 数值兼容

forward2fast 输入 9707,1879，加载 175.1461 秒，计算 1.4615 秒（含每层 audit 打印）。第一位置仍为 final-state=17a04f7d、logits=7fdfee67、argmax=6233。第二位置 28 层 CRC 与 v93 日志逐项比较完全一致，final-state=d2ec6d95、logits=96794b9a、argmax=4894、logit=16.4877815。

audit 同组数据上，泛化累加与显式两项表达式每层有 388–549 个 context 分量位模式不同，maxabs 范围 5.96046448e-08–4.76837158e-07；选用显式表达式恢复旧基线。证据已确认这条浮点求值路径的变化足以造成当前双 token 回归差异；没有证据指向此次 KV 数据损坏。尚未独立确认具体是 FMA 融合、指令顺序或其他编译求值机制，也不把 CRC 恢复等同于独立模型精度验证。更长历史仍走泛化路径，需要参考容差核对。

### ChatML 回答和结束

`chat MODEL "Hello" 16` 使用 20 个输入 token，命中模型 RAM 缓存，6 workers、mask=fc。生成正文 token 为 9707,0,2585,646,358,1492,498,3351,30，共 9 枚；下一预测为 im_end=151645，报告 generation-stop，未把控制标记解码为正文。最终输出 32 字节：`Hello! How can I help you today?`，正常返回 NSH。

计算 10.5269 秒，包含 20 个提示位置、后续反馈、结束预测及诊断输出；不能用该数字作为稳态解码 token/s。共处理 29 个位置，平均每位置约 0.363 秒，也只是本次整体计算观测。文本 BPE 和最终解码不包含在该计算计时内。解码助手末尾的 `Transformer inference pending` 是沿用早期独立 decode 诊断的过时提示，不代表这次生成未执行；后续应清理该提示。

### 尚待验证

此轮没有 generateids 不同容量/重复命令、cacheinfo、卸载后 workers/内存、中文或多轮测试。现阶段确认单次双 token 兼容和单次英文单轮聊天；独立 Linux/llama.cpp 参考、更长历史容差、重复压力、中文 tokenizer、长回答、Ctrl+C 安全取消、多轮会话和散热/频率测量仍待完成。

源码及此记录以 `a733-v95-chat-basic-verified` 标签和上层 `a733-v95-chat-basic-verified.bundle` 保存。保留原 v95 candidate 镜像名称与哈希，不覆盖回退镜像。
