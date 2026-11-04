# v180：本地 LLM 有界多轮上下文与精确 KV 复用

2026-10-25。以本地 Git `75b5faf`（v179）为基线。只修改 CPU LLM 应用层、
ChatML 模板与推理缓存，不改变 NPU、UFS、I2S、STM32 协议或内存映射。
本次不发布新 SD 镜像，不要求中间刷卡；继续遵守两条语音/LLM链路完成后统一验收的安排。

## 本次用户实机证据

原始串口输出保留在 `../evidence/llm-context-v180/user-sd-resident.txt`。
模型为 SD `/data/models/MiniCPM5-1B-Q4_K_M.gguf`，688065920 字节。

| 请求 | 首段可见 ms | 生成 token 数 | 首 token 后生成时间 s |
| --- | ---: | ---: | ---: |
| 你好，请简单介绍自己 | 2883 | 17 | 4.724 |
| 一加一等于几？只回答结果 | 3747 | 6 | 1.672 |
| 给我讲一个很短的故事 | 3112 | 24 | 6.589 |
| 我想听童话 | 2465 | 24 | 6.467 |

- 首次 SD 读入 230.4211 秒；含建词表、预填充与1 token预热总共246.8134秒。
- 其后 `cache-hit; load=0 sec; no model SD reads`，权重/目录/线程池驻留正常。
- 热生成约3.0–3.6 token/s，不能用含预填充的总耗时直接计算解码吞吐。
- 驻留后占用约715MB，约3.52GB可用；unload 后回到约198KB用户堆占用。
- 日志 `single-turn=1`：前缀 KV 复用不是语义对话历史；此前模型不知道上轮聊了什么。
- UFS `stage=quiesce error=-18` 是该次日志中的另一个未解决问题，本轮没有改 UFS。
- 上表不是 v180 的速度。热首段1–2秒仍未达到/未验收，不能以主机计时替代板测。

## 多轮实现

- 默认最多保留最近 **3个 user/assistant 完成对**，可配置0–4；仅存在内存，不保存对话文件。
- 是**可信物理设备的共享会话**：串口、SSH、普通路由的本地 fallback 与离线语音本地LLM
  共用。不是按SSH账号/云chat-id隔离的多用户系统；交接设备时先clear，不应暴露给不可信用户。
  这里只记录真正经过本地LLM的问答；云端Agent、规则直答、MCU直达命令的历史不会自动同步。
  验收记忆时使用`aipet local ask`，不要把云/本地两套会话当作同一份上下文。
- 角色以专用 token ID 编码，用户/历史正文不解释角色标记。拒绝`<|`、`<s>`、`</s>`、
  `<think>`、`</think>`原样控制标记，不把历史拼成可注入的system字符串。
- MiniCPM5按GGUF内官方模板：开头BOS，过去assistant不包含NoThink标记；
  只有当前生成前缀追加`<think>\n\n</think>\n\n`。Qwen维持自身ChatML模板。
- 每段上限4096字节，固定4对存储约32KiB BSS，不在NSH栈上分配32KiB。
  仍限制最多512输入token、64输出token。超限仅在分词阶段删除最旧完整对，不截断当前问题，
  不拆半个UTF-8字符，不删除system提示。当前问题本身放不下就报错，不调用模型。
- 只有推理、完整UTF-8输出和回复解析成功才写入历史；取消/溢出/失败不提交部分回答。
  达到输出token上限的有效短回答仍可存入历史，它并不保证语句已经结束。
- `preload` 永远是无历史预热，不把“只回答：好”写成用户对话，也不读取已有会话。
- clear/context配置变更清除语义历史和KV，不卸载688MB权重。
  unload/off/模型路径更改清除语义历史；模型文件身份变化也清除，不混用不同模型的历史。
  底层文件身份检查仍会拒绝复用已更改的驻留权重，需要显式unload。
- 所有配置/历史操作与本地推理互斥；忙时返回，不在取消时强杀计算线程。
  配置文件保留原子、O_EXCL/no-follow、fsync/rename保护。
- 旧 `aipetllm_infer` C API继续为无历史单次调用；新增`aipetllm_infer_chat`让调用者显式拥有历史。

## 本次速度优化与限制

1. 成功后保留所有**实际计算过**的token KV（上限512），包括已经解码的生成token；
   不缓存尚未输入模型的最后预测token，不保存/重放答案。精确前缀匹配失败的部分必须重算。
2. 保持最后一个prompt位置重新计算以获得新logits；不拿过期logits冒充推理。
3. 应用流式模式不再逐输入token打印`forward2 position=...`；低层非流式诊断仍保留。
4. 不改变矩阵数学/量化/线程分配/CPU频率，不根据主机数值回归宣称板上更快。
5. MiniCPM5官方历史模板会移除NoThink标记，所以不能保证上一轮所有生成KV均可复用。
   删掉最老历史时前缀也可能失配；长上下文需要更多预填充，首段可能反而变慢。
6. 后续显著提速方向是批量prefill、合并调度/算子、真实A76/A55负载测量；这些尚未实现。
   降低输出token上限主要减少总回答时间，不直接解决首token预填充。
7. SD冷读230秒与驻留后推理是两回事。当前不改已搁置的UFS初始化；只需一次加载即可持续热测。

## 新版后续统一验收命令（旧镜像尚不支持 context/clear）

先保持SD权重驻留，关闭云端TTS干扰：

```sh
aipet audio output off
aipet local on
aipet local model /data/models/MiniCPM5-1B-Q4_K_M.gguf
aipet local context 3
aipet local tokens 48
aipet local preload
aipet local clear
aipet local ask "请记住，我叫小明，喜欢童话。只回复：记住了。"
aipet local ask "我叫什么？喜欢什么故事？"
aipet local status
```

不重新读SD，清空会话后测单轮基线，再恢复多轮：

```sh
aipet local context 0
aipet local ask "你好，请简单介绍自己"
aipet local ask "一加一等于几？只回答结果"
aipet local context 3
aipet local clear
aipetllm cacheinfo
```

`context N`保存的是轮数设置，不保存文字；`clear`清空文字/KV且保留权重。
不要为了清空聊天执行unload，否则下次又要读整份SD模型。重启后历史为空。
记录每轮`local-context used/dropped/stored`、`prefill-cache reused/fresh`、`first-visible`
和生成数量；分别比较单轮、多轮、发生滑窗裁剪的延迟。3轮记忆不是无限长期记忆。

## 软件验证与未完成项

- ASan/UBSan覆盖C接口、历史裁剪前置门禁、取消、清空/卸载、预加载、
  配置原子性、重入互斥、共享会话和原路由/临时流式输出安全；真实分词测试另验非法标记。
- 使用本地真实MiniCPM5与Qwen GGUF，与已建立的独立llama.cpp参考比较历史模板token，
  原3提示词各4位置完整logits，以及多轮新问题的完整logits。
  两模型所有所测误差为0；多轮有/无KV缓存重算结果逐项相同。
- 真实ARM64/NuttX编译与完整链接结果、staging源码摘要、SDK还原另存证据。
- 主机参考是受限CPU数学oracle，不是ARM NEON数值/性能或语义记忆板测。
- 通用NPU ASR目标布局/provider、Matcha RTOS声学执行与完整文本规范化仍未完成，
  没有将本次LLM改动宣称为完整离线语音助手。
- 历史文档、旧镜像及v178/v179标签保留。v180普通本地提交、唯一标签与bundle/checkpoint
  独立保存；不自动推送GitHub，不刷写SD/UFS，不删除旧Qwen或其他模型。

完整构建：`out/context-build-v180-r1`，2993构建目标通过；SDK233路径逐字节恢复。
ARM最大静态栈帧2464字节；内核2801184字节，运行布局37453824字节，页/栈/arena检查通过。
内核SHA256：`1b93cb82aa698daf5b010df734dee8086b1a95b9e31701b287ccba418dbe29d0`。
冻结到`out/v180-llm-context-20261025-r2/`，源码摘要和数值/编译签核在
`../evidence/llm-context-v180/build-signoff.json`。最初封存脚本误把`nuttx.bin`写成输入名
`Image`而失败，保留空的首次封存目录/日志；已修正为只读`nuttx.bin`并在新r2目录封存。
这是制品封存路径错误，不是内核构建失败；没有丢弃失败记录或覆盖旧镜像。
