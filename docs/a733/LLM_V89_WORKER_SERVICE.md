# LLM v89：跨命令工作线程服务与资源审计

恢复点：`a733-llm-cache-v88-basic-verified`。v88已经实测缓存命中总耗时
0.8080秒、CRC一致，但unload后较初始仍多1426048字节，原因尚未定位。
本阶段只完善计算服务生命周期，不宣称此前残留必定是pthread泄漏。

官方工作区 `nuttx/sched/pthread/pthread_join.c` 拒绝不同task group之间
的join（EINVAL）。因此常驻线程不能直接由短生命的前向命令创建，再
由另一个命令回收。本次使用常驻kthread `a733-llm-pool`统一创建/join
计算pthread，前向命令通过请求/响应semaphore访问该服务。服务由当前
官方openvela内核提供的kthread/pthread/semaphore API构建，不改公共
调度代码，不移植Linux调度器。

应用边界检查不允许apps引用`nuttx/kthread.h`，首轮构建因此被拦截。
已将启动入口放入vendor平台层`a733_boot.c`的
`a733_compute_service_start`，应用调用此平台hook；没有关闭边界检查。
pthread/semaphore调度仍是当前官方内核API，而不是自行实现的新调度器。

同一核心列表下，首次矩阵创建对应工作线程，以后矩阵/命令复用这些
线程。切换核心列表时服务先停止并join旧工作线程，再建立新线程；
默认仍CPU2..7、A76:A55=3:1。计算工作线程绑定所选核心，服务协调
线程也使用所选掩码。输入量化、输出行切分、直接RAM权重路径不变。
每个矩阵全部完成后，清空工作槽中的临时指针再允许主线程释放缓冲区。

`unload`先请求服务停止/join所有计算线程，确认后才释放模型。
服务协调kthread本身保留（8192字节栈与少量内核资源），睡眠等待后续
请求，不忙轮询；卸载后恢复其亲和掩码0xff。因此卸载后不期待与冷启动
内存完全相同，关键是稳定平台而非每次增长。service/sem控制存储静态
分配，不能在服务尚未返回时释放。join失败时保留模型，禁止不安全释放。
初始化失败清理已初始化semaphore；工作线程创建失败明确报错，可用
unload请求服务回收已创建的部分线程。

`cacheinfo`增加：pool pid、mask、workers、created累计创建数、matrices
累计矩阵数。同核心重复前向created应不增加；换核心允许增加；unload
后workers=0，created和matrices保留诊断历史，服务pid仍存在。
本阶段尚未完成强制kill/SIGINT回收审计，回归请等待正常完成。

## 实机测试

整盘烧写前备份模型和板端配置，镜像不包含后来上传的Qwen模型。
按行执行，先完成首次模型载入，再连续重复同一命令至少5次：

```text
free
aipetllm cacheinfo
time "aipetllm forwardfast /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf 9707"
aipetllm cacheinfo
ps
free
time "aipetllm forwardfast /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf 9707"
aipetllm cacheinfo
free
```

同mask命令应cache-hit、created保持6、每次增加矩阵计数。
CRC与v88相同：state=17a04f7d、logits=7fdfee67、argmax=6233。
随后测试切核和卸载：

```text
time "aipetllm forwardfast /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf 9707 6,7"
aipetllm cacheinfo
ps
aipetllm unload
aipetllm cacheinfo
ps
free
aipetllm unload
```

切核后mask=c0、workers=2，cache-hit不读模型；卸载后empty/workers=0。
重复载入/命中/卸载至少3轮，比较卸载后used、nused、ps是否稳定。
线程池不能证明所有残留资源已修复，需据上述数据继续定位。
并发busy、网络/SSH响应和温度压力仍待测试。目录缓存、真实multi-token
prefill/KV cache、LM采样/连续生成尚未实现，后续单独推进。

## 候选构建存档

最终构建源提交 `51f759a`，WSL 构建通过。首轮应用私有内核头引用被
边界检查拒绝，已在vendor平台hook修复；第二轮严格返回值检查错误已
修复。失败及最终日志均保留，不将失败输出内核当成发布产物。
System.map确认 `llm_pool_service` 与 `a733_compute_service_start`。
官方arch.h临时补丁已恢复，无.orig残留。线程池和资源释放尚待实机测试。

- 镜像：`A733-A7Z-ALL-Files/openvela-a733-cubie-a7z-sd-llm-pool-v89-candidate.img`
- 大小2147483648字节，内核1713600字节。
- 内核SHA256：`7b506634aedba72a0c04cb211e5ee3381f8f74990c51c06fa85c7493492aeeef`。
- 镜像SHA256：`72d0d0d3e9a7306bdbd054e377ce0e9878787623829aba2e8826d844de421b7e`。
- ext4检查、内核回读、GPT检查通过，继承的分区4尾部对齐提示不变。
- 恢复标签：`a733-llm-pool-v89-candidate`。
- 恢复包：`A733-A7Z-ALL-Files/archives/llm-v89/a733-llm-v89.bundle`。
- 同目录日志：build.log（边界失败）、build-platform.log（编译失败）、
  build-final.log（成功）、package.log、verify.log。

v88镜像与基本验证标签保持不变，不宣称内存泄漏已经定位或修复。
