# v178：MiniCPM5 本地 LLM 与通用 ASR 转换

2026-10-25。用户本轮跳过 STM32 硬件排查，逐项处理本地 LLM 和离线语音。
没有改动 MCU 协议/arm 策略、下位机源码、UFS 引导区或相机驱动。v177、历史文档和
恢复点保留。按芯片/NPU移植分级验证：主机数值、目标编译、真实硬件和应用语义分开验收。

## 本地 LLM 已实现

- 受限 MiniCPM5-1B GGUF 支持，不是通用 llama.cpp 全架构移植。
  hidden=1536，Q=2048，K/V=256，head dimension=128，24层；不误用 hidden/16=96。
  MiniCPM5 没有 Q/K/V bias；NORMAL RoPE 与 Qwen NeoX 分开，按 GGML 递推频率计算。
- `llama-bpe` 三位数字分组、整段 token 优先匹配和参考源 Unicode 字母/数字类别表。
  普通文本不自动加 BOS；模型聊天模板显式加 `<s>`，NoThink 生成前缀为
  `assistant\n<think>\n\n</think>\n\n`。特殊标记按词表 ID 插入，不当普通字节分词。
- 对齐 RMSNorm、SiLU、F32 attention 求值顺序与 masked-KV softmax 的四路尾部处理。
  曾有单 token 正确、多 token 误差放大的情况；失败日志全部保留，没有放宽阈值。
- 三层缓存：权重/线程池、分词与张量目录、相同 prompt 前缀 KV。
  复用真实计算过的状态，不重放固定答案，最终位置仍计算，失败不提交新缓存。
  热请求不再反复扫描几十万条 GGUF 词表/merge 元数据；unload 释放所有缓存。
- 检测到模型文件身份/大小/修改时间变化时，拒绝复用并要求显式 unload。
  不要在模型常驻期间原地修改模型；元数据检查不能替代完整文件摘要。
- Agent 本地路由实时输出完整 UTF-8 片段，报告 `first-visible=... ms`；计时包括
  本地路由适配器内的 UI 通知、分词、冷加载等，不包含之前的命令行输入时间。
  流式内容仅 provisional，不用于动作执行；最终正文不重复，失败标记 aborted。
- 预加载只生成1 token，不再为预热生成完整32 token回复。

## 模型与自动选择

用户现有文件：`C:\Users\Lenovo\Downloads\MiniCPM5-1B-Q4_K_M.gguf`。
大小688065920 bytes，SHA-256：
`81b64d05a23b17b34c475f42b3e72fbde62d4b92cc34541f7a8031d0752deafa`。

先在电脑核验摘要，再安装为 `/ufs/models/MiniCPM5-1B-Q4_K_M.gguf`（优先），或
`/data/models/MiniCPM5-1B-Q4_K_M.gguf`。模型单独安装，不编入内核、不自动下载或覆盖。

`model auto`：有效UFS MiniCPM5 → 有效SD MiniCPM5 → 已通过的UFS Qwen v174 →
旧UFS Qwen → 原SD Qwen。MiniCPM5默认候选必须符合上述大小；这不替代安装时完整SHA256。
明确自定义路径不被改写；`model sd`强制SD。旧Qwen文件不删除。

## 验证与性能边界

真实 GGUF 的12组中英/数字/空白/Unicode分词与本地参考一致；三组中文聊天模板一致。
三个真实提示词各4个 teacher-forced位置，每位置比较全部130560 logits。
最终12个位置 maxabs=0、RMSE=0、argmax相同；阈值进一步收紧为 maxabs≤1e-5、RMSE≤1e-6。
连续请求同时验证张量目录缓存和共享前缀KV；不是空输入或固定答案mock。

参考为本地 `llama.cpp-0.1.2` 源码：CPU、F32 KV、逐token、关闭融合/重打包/AVX/FMA，
固定逐算子oracle。其他CPU后端的探索失败日志保留，不能混作同一基准。
ARM64 NEON激活单元真实交叉编译通过；模型选择/API清理/流式线程隔离、错误路径
ASan/UBSan通过；既有I2S应用层回归通过。A7Z本版数值、语言质量、温度和延迟仍待板测。
本版LLM仍为CPU/NEON，不冒充NPU加速。

同一套真实回归也验证了原Qwen2.5-1.5B：12个位置完整logits同样逐元素一致。
实际UTF-8 API的数学请求分别返回 `1+1=2`（MiniCPM5）和 `2`（Qwen）；仍不替代板上质量测试。
新增离线配置目录自动创建，不依赖在线API初始化；临时文件O_EXCL/no-follow、fsync及
rename保存经过主机故障/残留临时文件/符号链接测试，不覆盖旧配置或跟随临时链接。

**首段1–2秒尚未实机验收。** 按之前约67.58 MiB/s的UFS实测读取速度，冷读688MB
仅I/O就约9.7秒，还不含计算。必须分别测冷请求和预热后请求，不能拿热请求冒充冷启动。
长输入的新token仍要实际计算，没有伪造首token。

## 板测命令

备份重要SD数据后再刷候选镜像；完整刷卡会覆盖目标卡，不自动保留旧卡上的私有配置。
把已核验GGUF放入上述目录后，在NSH执行：

```sh
ufs status
aipet audio output off
aipet local on
aipet local unload
aipet local model auto
aipet local tokens 24
aipet local cores fc
aipet local status
aipet local ask "你好，请简单介绍自己"
aipet local ask "一加一等于几？只回答结果"
aipet local ask "给我讲一个很短的故事"
aipet local status
free
```

第一条ask是冷请求，后面是热请求。保留first-visible、prompt/generated、prefill-cache
reused/fresh、load/compute；最终elapsed不是首token时间。`local ask`强制本地，普通ask
在联网时仍可能走已有云路由。关闭扬声器是为避免纯LLM测试触发现有云TTS，不是I2S故障。

单独预热也可使用 `aipet local preload` 后再 `aipet local ask "你好"`。
从另一shell可 `aipet local stop` 协作停止；不要删除运行任务。
换模型先unload；回退可显式选 `/ufs/models/qwen2.5-1.5b-instruct-q4_k_m.v174.gguf`。
恢复音频：`aipet audio output i2s`，但其任意文字合成目前仍依赖云TTS。

## 通用 ASR 本轮进展

使用上游固定提交 `e2382758de9a0219b4efe682b95af30b399db3b8` 和逐份SHA锁定源图，
不是263类KWS。已有本地 `ubuntu-npu:v2.0.10.2`、Acuity6.30.22、Vivante5.11.0完成
三模型导入和fp16 NBG导出。容器禁网、只读根目录、无设备/socket挂载，只写新out目录。
仅将符号batch N固定为1，不改权重/状态/整数输入语义，不猜输出长度。

| 产物 | NBG bytes |
| --- | ---: |
| encoder fp16 | 41201104 |
| decoder fp16 | 7316264 |
| joiner fp16 | 6440816 |

ASR为36入/36出，encoder输出1×24×512，decoder512维，joiner6254类。
不能与KWS的39输入/263类混用。实际NBG头magic/version/CID和厂商nbinfo原生I/O/
padding/内存已检查，目标CID=0x1000003b。
摘要：`out/asr-export-v178-20261025/artifacts-verified.json`。
静态原生I/O总量：encoder共72个张量4133696 bytes，decoder为2112 bytes，
joiner为29120 bytes。仅encoder I/O就超过当前2MiB tensor池，后续必须连同prepared区
做真实内存预算/布局验收，不能只换模型后反复重试ENOMEM。
初版报告在错误子目录查找NBG而显示空列表；独立审计找到 `*_nbg_unify/network_binary.nb`
并核对，不把编译退出码0单独当成导出成功。

独立私有包：`out/a733-linux-asr-contract-v178-20261025-r2.tar.gz`。
以后安排官方Linux测试时使用，不自动覆盖现在的openvela卡：

```sh
tar -xzf a733-linux-asr-contract-v178-20261025-r2.tar.gz
cd a733-linux-asr-contract-v178-20261025-r2
sudo bash test-linux-asr-contract.sh --inspect --npu-idle-confirmed
sudo bash test-linux-asr-contract.sh --run-zero-smoke --npu-idle-confirmed
```

有完整性、设备占用和温度门禁；失败停止，无reset/retry；回传results中tar.gz。
零输入只测合约/硬件smoke，绝非录音、识别率或语音链路验收。使用Linux已安装厂商运行库，
包内不捆绑/重新分发厂商共享库。

## 尚未完成的语音环节

1. 新ASR NBG仍缺真实音频、状态回灌、原生完整张量及转写的Linux golden。
2. 尚未生成它们的openvela prepared包/通用转写解码器，不能使用旧KWS包替代。
   encoder NBG已超过当前24MiB prepared区，后续还需按真实布局验收内存预算。
3. 一般离线TTS还缺Matcha CPU acoustic/text前端的RTOS接入；vocoder单独不能
   将任意回复文字合成为语音。
4. `ICS43434 → 通用本地ASR → Agent/LLM → 本地TTS → I2S` **仍未全部完成**。
   本轮不静默调用云端冒充离线，不把NBG编译成功写成整机通过。

## 构建与恢复

```sh
bash tools/build-minicpm5-reference.sh
bash tools/test-minicpm5.sh /path/MiniCPM5-1B-Q4_K_M.gguf infer
bash tools/test-aipet-llm-v178.sh
bash tools/test-aipet-audio.sh
bash tools/build-a733-llm-v178.sh
# 保留失败日志后重编
bash tools/build-a733-llm-v178.sh --incremental
bash tools/package-a733-llm-v178.sh
```

包装器检查实际暂存的LLM及既有NPU/UFS源字节，退出后恢复共享SDK。
另修复了构建暂存的旧对象问题：cp -a保留的源码时间可能早于已有对象，造成源码hash正确
却仍链接旧激活对象、缺少softmax符号。v178仅对可恢复的临时应用副本更新时间戳强制重编，
不改源目录时间；最终还检查ELF同时包含激活/softmax/路由符号。失败日志与旧对象保留在
`out/v178-build-attempt2/`、`attempt3/`、`attempt4/`，不以失败构建交付。
制品：`out/v178-llm-candidate-20261025/manifest.json`，板测状态维持false。
开始前恢复点 `out/checkpoints/a733-pre-v178-offline-20261025/`；
结束后恢复点 `out/checkpoints/a733-v178-llm-candidate-20261025/`。
恢复只允许到新目录，含Git历史、未提交/未跟踪代码和文档，不重置现有工作区。

来源：[MiniCPM5官方GGUF](https://huggingface.co/openbmb/MiniCPM5-1B-GGUF/blob/main/MiniCPM5-1B-Q4_K_M.gguf)、
[Zipformer固定源图](https://huggingface.co/csukuangfj/k2fsa-zipformer-bilingual-zh-en-t/tree/e2382758de9a0219b4efe682b95af30b399db3b8/exp/96)。
