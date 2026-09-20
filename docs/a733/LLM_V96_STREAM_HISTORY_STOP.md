# v96：流式输出、有限多轮历史与协作式停止

基于 v95 单轮英文聊天基本验证状态，保留双位置显式 attention 表达式和诊断入口，不改 SMP 栈、系统调度、Wi-Fi/NPU 驱动或热保护。

## 合并能力和边界

- `chat MODEL "message" COUNT [CORES]`：仍是无历史单轮，但默认逐 token 输出。词表先加载到命令私有 decoder，不为每枚 token 扫描 SD 元数据。遇到 UTF-8 跨 token 的尾字节先保留，字符完整后输出并 fflush；不会把半个汉字与逐层诊断交错打印。正常输出仍可能在停止于半字符时以替换字符结束并警告。
- `ask MODEL "message" COUNT [CORES]`：同样流式，使用一个全板共享、互斥保护的 RAM token 历史。成功完成且正文非空才尝试提交本轮；完整输入+回答+im_end 不超过 64 token 才保留。超限明确提示未保留，旧历史不变，不悄悄截断。后续 ask 使用完整历史重新预填充，**不是跨命令持久 KV**，因此轮数增加会增加首 token 等待时间。
- `newchat` 清空历史，`history` 显示数量和模型路径，不打印聊天正文。历史不写入 TF 卡，不同模型路径要求先 newchat，模型缓存切换仍要求 unload。unload 不自动清空 token 历史。
- 全板只有一个共享 ask 会话，不是按 SSH 用户/终端隔离的多租户会话；一个 ask 正在执行时另一 ask/newchat/history 会提示 busy。不要在不同用户间混用敏感对话。普通 chat 独立且不提交历史，模型后端仍排他。
- `runstatus` 显示当前后端生成活动状态，`stop` 原子设置协作停止标志。请从另一 SSH 会话执行；模型读取每个 256 KiB chunk、计算每层开始检查，等待当前矩阵线程完成后正常清理返回 125，中断的 ask 不提交部分历史。加载/计算开始前（BPE、decoder 准备期间）尚未 active，stop 不排队。若线程池本身死锁，stop 不能解开 semaphore 等待。
- **不宣称修复 Ctrl+C/kill 强制删除任务**。保留取消禁用和正常清理路径，但任意强制终止、断电等仍需要专项审计。协作式 stop 与 POSIX SIGINT 是不同机制。
- 输出 `first-prediction`（不含模型加载和 tokenizer 的首个预测等待时间）与 `after-first`（首预测之后至最后预测，包含结束预测和 callback/输出开销），不把总 prefill 时间当作纯 decode 速度。

generate/generateids/forward2fast 保留原逐层诊断和最终解码；chat/ask 的计算日志在文本输出期间被抑制，先显示提示位置进度。最多 64 输入/历史 token、64 输出 token；EOS/im_end 不作为正文打印。ask 在达到输出预算时也会在保存历史时补 im_end，属于主动关闭截断回答，不等同模型自然结束。

## 错误修复

原独立 decode 对重复 ID 只复制第一份 raw piece，后续重复项长度为 0。现在复制到所有重复位置。主机重复 9707,9707 正确输出 HelloHello。删除早期 decode 中误导性的 Transformer inference pending，并更新 info 的阶段和实际边界。

## 主机验证

WSL C++17 `tools/test-aipetllm-encode-api.cxx`（开启 AIPETLLM_BPE_TEST_API，只在主机测试，固件不包含测试入口）运行退出 0。英文 BPE 和 ChatML 结构回归保持；历史追加保留前缀且超容量拒绝；Hello world 你好 编码 9707,1879,220,108386 后流式解码与原 UTF-8 文本逐字节相符。“你”以 E4/BD/A0 对应的三枚 byte token 输入，pending 长度依次 1/2/0，最终只输出完整字符。C11 standalone decode 输出 HelloHello。

这些是接口、往返和 UTF-8 测试，不是中文与独立官方 tokenizer 的全类别审计；中文 Transformer 对话效果仍需实机测试。完整 Unicode 表、多轮质量、长稳、取消后的重入/内存压力、独立 llama.cpp 精度参考仍待完成。

## 板端一次烧录测试

先确认模型仍在 TF 卡、网络正常。一行一条，不要 shell 反斜杠续行。

```sh
aipetllm forward2fast /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf 9707 1879
aipetllm chat /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf "你好，请简短介绍自己。" 24
aipetllm newchat
aipetllm ask /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf "My name is Tom." 8
aipetllm history
aipetllm ask /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf "What is my name?" 8
aipetllm history
```

需要先查看第一轮是否 retained；若回答较长造成容量超限，不能声称第二轮已记住该轮。测试 stop 时主会话运行 chat COUNT=64，另一 SSH 会话先 runstatus 确认 active=1，再执行 stop；随后主会话应正常返回，重新 chat 验证重入。初次加载中停止可能释放未发布模型，下次仍需重新加载；已驻留模型被停止后继续保留。

```sh
aipetllm runstatus
aipetllm stop
```

后续测试 busy/超限拒绝、重复 ask/newchat、unload/cacheinfo/free、网络负载并行；保留日志和温度/频率实测，不关闭热保护。新功能目前未经板端验证，不把主机通过当成实机通过。

## 恢复与构建纪律

## v96 构建与镜像存档

- 最终内核源提交：`fd464f1`。首次构建因首 token 计时变量可能未初始化被 `-Werror` 拦截；显式初始化后重新编译通过，失败产物未用于镜像。
- 日志与符号：`archives/llm-v96/build.log`（失败记录）、`build-final.log`、`package.log`、`verify.log`、`nuttx.elf`、`Image`、`System.map`。此目录是本地存档，不整体纳入源码 Git。
- 独立镜像位于仓库上层：`openvela-a733-cubie-a7z-sd-llm-stream-v96-candidate.img`，大小 `2147483648` 字节；基于 v95 镜像仅替换启动内核。
- 内核大小 `1734128` 字节，SHA256：`c3000c0c6087fcad150fb779d9cf080168b0331f08f153924711bb74b8354451`。
- 镜像 SHA256：`506a2ab6e53a114b72fb6ae26602b59e8466a968a97b3860d05b8c116b5b57fa`。
- ext4 检查、GPT 检查和内核回读哈希通过；继承的分区 4 尾部非 2048 扇区对齐提示仍存在，GPT 报告 No problems found。
- 空闲栈边界 `0x423f6000` / `0x42406000` 均页对齐。构建结束官方工作区临时 `apps/system/aipetllm` 目录已恢复为不存在。
- 候选标签：`a733-v96-stream-history-stop-candidate`；上层恢复包：`a733-v96-stream-history-stop-candidate.bundle`。主机 tokenizer/UTF-8 分片及重复 ID 解码测试通过；这不是板端推理验收。

回退 bundle 为上层 `a733-v95-before-stream-session.bundle`，v95 已验证标签保持。实现只在代码仓库 overlay，构建前提交/bundle，官方环境只临时覆盖并由脚本恢复。新镜像独立命名，不覆盖任何回退版本。整卡烧录会覆盖后来放入 TF 的模型/密钥/配置，务必先备份。
