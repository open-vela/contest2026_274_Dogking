# LLM v88：持久模型快照与直接 RAM 权重

前置恢复点 `a733-llm-multicore-v87-forward-verified`，六核心默认和
可选核心列表保持不变，系统调度配置不变。所有新增代码在比赛仓库
overlay；官方公共工作区仍仅临时占用并由构建脚本恢复。

首次成功的 forwardfast 会保留整份模型 RAM，之后相同路径命中缓存，
不再打开或读取 SD 模型文件；每次使用独立只读 fmemopen 流重新解析
GGUF 目录。输出 cache-hit/load=0。缓存当前只保存一份模型；不同
路径必须先 `aipetllm unload`，避免静默同时分配两份约1.1 GB缓冲区。
无自动开机加载，重启后缓存为空。首次失败不发布缓存并释放新缓冲区。

`aipetllm cacheinfo` 报告 empty/resident、字节数、路径。
`aipetllm unload` 明确释放缓存，可重复调用。缓存占用是设计行为，
不是推理结束应立即归还的临时内存。正常 unload 后检查 free 恢复。

模型作为路径标识的不可变快照，不对文件替换自动失效。上传新版、
重命名或修改同路径模型前必须卸载，之后再次运行才读新文件。
不要在首次载入期间覆盖文件。缓存路径上限1023字节，模型上限1536MiB。
缓存命令/推理使用 trylock，竞争时明确返回 busy，不等待；卸载不能
释放正在计算的权重。pthread cancellation 在缓存操作期间禁用并恢复。
尚未完成任意强制任务 kill/SIGINT 场景下的资源回收审计，首次回归请
等待命令自然完成，不强杀；异常强杀后持续busy应重启，不能强制释放
可能仍在被工作线程访问的缓存。

并行 Q4_K/Q6_K 矩阵直接使用模型缓冲区中的权重，不再复制当前矩阵。
有偏移/长度/对齐检查，工作线程输出行互不重叠，所有线程 join 后才
消费结果。F32 和旧 forward1 继续原路径。线程仍按矩阵创建并退出，
持久线程池、目录缓存和 multi-token KV cache/连续生成未实现。

## 实机生命周期测试

烧写前备份模型和配置，整盘烧写覆盖TF卡；镜像未包含后来传入的GGUF。
模型重新传入后执行，每一行分别输入：

```text
aipetllm cacheinfo
free
time "aipetllm forwardfast /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf 9707"
aipetllm cacheinfo
free
time "aipetllm forwardfast /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf 9707"
time "aipetllm forwardfast /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf 9707 6,7"
aipetllm unload
aipetllm cacheinfo
free
aipetllm unload
```

首次有SD载入，第二/第三次必须cache-hit且总耗时接近compute，逐层CRC
应与v87一致，final-state=17a04f7d、logits=7fdfee67、argmax=6233。
换核心列表不能重新加载。unload之后empty，约1.1GB占用释放；多次重复
推理不能不断增长内存。两终端并发请求应收到busy，不死锁或读取已释放
缓冲区。还需负测不存在路径、非GGUF文件、换模型路径及长期网络并发。

当前仍是独立单token前向，无历史KV状态，不能将缓存命中次数当作持续
生成能力，也不预估tok/s；后续线程池/目录缓存后再接prefill和生成。

## 候选构建存档

源提交 `37f2e92`，WSL 构建通过；缓存控制及模型驻留状态已链接。
官方 arch.h 临时补丁已恢复，无 .orig 残留。当前尚未实机验证缓存
跨命令生命周期，不宣称第二次无需加载和卸载释放已测试通过。

- 镜像：`A733-A7Z-ALL-Files/openvela-a733-cubie-a7z-sd-llm-cache-v88-candidate.img`
- 大小：2147483648字节；内核1713600字节。
- 内核SHA256：`83916fd009f0ca0cf551fcb4f85752461aaba83ddb6650b8d6002e8eaac62bc1`。
- 镜像SHA256：`d304f200047740f1084b0b0a8adca1a88a6d4fcbee839ffb5149c350d22600de`。
- ext4检查、嵌入内核回读和GPT检查通过；基础镜像分区4尾部非2048
  扇区对齐提示不变。v87保留。
- 标签：`a733-llm-cache-v88-candidate`。
- 恢复包：`A733-A7Z-ALL-Files/archives/llm-v88/a733-llm-v88.bundle`。
- 构建/打包/检查日志：同目录 build.log、package.log、verify.log。

## v88 实机缓存生命周期基本验证

用户回传默认六核心（mask fc）两次前向：

| 次数 | load（秒） | compute（秒） | 命令总耗时（秒） |
| --- | --- | --- | --- |
| 首次 | 174.9839 | 0.8210 | 175.8280 |
| 缓存命中 | 0 | 0.7909 | 0.8080 |

cacheinfo 报告 resident bytes=1117320736。第二次明确 cache-hit，
无需SD模型读取。两次28层CRC全部与v87一致，最终state=17a04f7d、
logits=7fdfee67、argmax=6233、logit=10.4909735。
因此本次跨命令模型驻留与前向数值回归通过。缓存命中单token前向
约1.24次/秒（按总耗时倒数），仍不等于连续生成tok/s。

内存记录（字节）：

| 状态 | used | nused | nfree |
| --- | --- | --- | --- |
| 首次前向前 | 36080 | 89 | 4 |
| 两次前向后、缓存驻留 | 1118717328 | 245 | 27 |
| unload后 | 1462128 | 245 | 27 |

卸载释放1117255200字节，与模型大小相差65536字节；大块模型缓存已
释放，但使用量相较初始仍高1426048字节（约1.36MiB），分配块数也未
恢复初值。因此不能宣称完全无泄漏。可能包含pthread/TLS/运行库常驻
分配或未释放资源，需要重复缓存命中、反复加载/卸载及线程数量对照
确认是否线性增长，尚未定位原因。用户未提供unload后的cacheinfo、
并发busy测试或第二次unload，不能据此宣称所有负测完成。

恢复标签：`a733-llm-cache-v88-basic-verified`。
独立恢复包：`A733-A7Z-ALL-Files/archives/llm-v88/a733-llm-v88-basic-verified.bundle`。
下一步优先资源生命周期审计与持久线程池，然后目录缓存及多token
prefill/KV cache/采样生成，不改变当前六核心默认接口。
