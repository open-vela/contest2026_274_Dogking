# 小派 v1.2／v220 低延迟预览

## 定位与保留

**最新稳定版本仍为 v1.1／v219**，不是本预览。[稳定指针](STABLE_RELEASE.json)及原通过镜像保持不动。
稳定标签 `a733-v1.1-v219-stable-20261101`；预览分支 `preview/v1.2-v220`。
旧版本、过程文档和 Git 恢复包全部保留，不提交 PR、不推送、不写物理 SD/UFS。

本轮目标是消除本地聊天历史造成的逐轮变慢，不更换 ASR/TTS/LLM 模型，不重移植 NPU，不修改全局调度、I2S 时序或摄像头驱动。

**已生成独立测试镜像**：`../openvela-a733-cubie-a7z-sd-v1.2-v220-preview.img`，2,147,483,648 字节。
SHA256：`89a6d7f7d508427da0b4f193566c49d958451cc52c8e628a680f32a1062200a1`，旁边有 `.sha256` 文件；Windows 与 Linux 独立校验一致。
27 项门禁、完整链接／摄像头／ABI／ISA、模型及启动文件回读通过，SDK 316 项字节还原。[封存证据](evidence/product-v12-v220/README.md)。**本预览尚未实机验收。**

## 为什么先关闭历史

用户本次 v1.1 日志中，真实对话的 LLM 结果为：

| 历史对数 | 提示 token | 精确复用／新增 token | LLM 首字 |
| --- | --- | --- | --- |
| 0 | 83 | 70／13 | 3.797 秒 |
| 1 | 111 | 79／32 | 10.480 秒 |
| 2 | 142 | 107／35 | 13.851 秒 |
| 3 | 185 | 138／47 | 24.672 秒 |

清理历史后的另一条故事请求为 85 token、复用 70／新增 15、首字 4.406 秒。
同批 ASR 收尾约 0.95～1.51 秒；vocoder 一块通常约 78 ms。它们是内部阶段计时，不是耳朵听见声音的时刻。
证据主要指向 LLM 历史预填充和回复供给等待，不能笼统宣称 ASR/TTS 内核被 LLM 拖慢。

## 本版改动

1. **本地历史默认 0 对**。单轮之间不保存聊天记忆，姓名、指代和“接着上一句”等跨轮信息不再自动延续，这是明确取舍。
2. **配置隔离**：使用 `/data/ai_agent/config/local_llm.context.v12`；不读写 v1.1 的 `local_llm.context`。旧卡原有的 `3` 不会偷偷沿用，回退稳定版时原设置仍在。
3. **保留人设和情绪价值**：仍使用“小派、温暖俏皮、难过先安慰”的同一系统提示；保留完整句子、自然 EOS 和输出耗尽报错。不压小回复预算、不把残句当成功。
4. **保留常驻权重及精确前缀 KV**。关闭聊天历史不等于关闭 KV。`context 0/3` 只清聊天记录，不再触发整个人设前缀冷算；明确执行 `clear` 才重置 KV。
5. **复用 CPU 预填充工作区**，最多缓存 2 MiB。每次仍清零，保持原 `calloc` 语义；过大请求走临时分配，分配失败不破坏旧缓存。只在计算池成功停止并 join 后卸载；不确定故障仍保留 RAM，不强杀线程或 NPU owner。
6. **补充计时/状态**：`local-latency-v220` 区分历史策略；`llm-latency-v220` 分开精确复用、新增 token、批量阶段耗时；`llm-prefill-scratch-v220` 显示分配与复用次数。批量阶段不包含最后提示 token／LM-head，不要拿它冒充首字。

云端历史仍是独立会话，本轮没有擅自关闭云端记忆。ASR、CPU 声学模型、NPU vocoder、同帧 YOLO、SPI、UART4 故障保护均沿用稳定实现。

## 验证与构建

运行时代码恢复点：`133662be8c0f369f87ae1fc6ef24df2768960ed0`。

- `tools/test-preview-v220.py`：真实应用配置/历史代码，模拟引擎；稳定版旧行为、24 轮独立请求、旧设置隔离、可选历史、前缀保留、完整回复和工作区失败路径，ASan/UBSan 检查。
- `tools/test-llm-quality-v216.cxx` 配合 `AIPETLLM_PREVIEW_V12=1`：真实 MiniCPM5 与独立 llama.cpp 的六组冷／热／batch logits、上下文可选功能及完整回答样例。不是模拟模型，也不是板上性能测试。
- `tools/test-product-gates-v220.py`：继承全部 26 项产品／语音软件门禁，新增预览门禁，并绑定真实 GGUF 数值报告。
- `tools/check-product-full-v220.py`：保留完整摄像头、NPU ABI、内存布局、ISA、ELF/构造函数检查，额外要求实际固件含预览配置及新代码路径。

WSL 仓库目录中，使用独立构建缓存：

```bash
JOBS=12 bash tools/build-a733-product-v12-v220.sh \
  out/product-v12-v220-build-r1 out/ort-static-v185-r2
```

缓存目录为 `quickly-openvela/cmake_out/cubie-a7z_nsh_v220_product_v12_preview`，绝不覆盖 v219 缓存。完整门禁/镜像/校验和以 `docs/evidence/product-v12-v220/summary.json` 封存结果为准；没有该结果时不能说镜像已交付。

## 上板测试顺序

烧录会覆盖 SD，请先备份配置和个人文件。UFS 数据不需要重复制，旧 U-Boot 问题仍按约定保留，不在本轮擦写。

```text
aipet help
ufs status
```

帮助应显示 `v1.2 PREVIEW (v220; stable=v1.1/v219)`。只有 UFS 尚未挂载时执行一次 `ufs start`，确认 `/ufs/models/MiniCPM5-1B-Q4_K_M.gguf` 可读；否则自动使用镜像内的 SD 备份。

```text
aipet mode local
aipet audio output i2s
aipet audio volume 50
aipet local on
aipet local context 0
aipet local cores fc
aipet local batch 4
aipet local status
time "aipet local preload"
```

`limit=0` 是本版默认值，上面 `context 0` 用于消除上次预览中的显式选择。预加载只做一次，等待模型就绪。
**不要在每轮前执行 `clear`、`unload` 或重启**，否则会主动制造人设冷预填充。也不要用“你是谁”的固定身份回复测试 LLM 性能。

```text
aipet local ask "用一句话鼓励我。"
aipet local ask "我今天有点难过，陪我说句话。"
aipet local ask "为什么天空是蓝色的？简短回答。"
aipet local ask "给我讲一个有结尾的小故事。"
aipet local status
aipet run
aipet audio service status
```

等到 `READY MIC ON` 后，分别说以上不同问题，连续至少 10 轮。每轮记录 ASR tail、`prompt/reused/fresh/batch-ms`、LLM `first/rest`、TTS `first-driver` 和短语队列完成数。观察历史始终 `used=0 stored=0 limit=0`，没有因轮数自动膨胀的提示词。
相同问题会额外命中更长精确前缀，所以请**同时测试不同问题**，不能用重复问题的最快数值代表所有对话。

```text
aipet stop
aipet audio service status
```

等待 `active=0`，然后可执行 `aipet run`，确认热恢复和停止正常。不可强杀 NPU owner。

可选恢复记忆（先停止服务）：

```text
aipet local context 3
aipet local status
aipet run
```

先说姓名，再问姓名，检验显式启用的上下文；这不是默认低延迟模式。测完 `aipet stop`、等待退出，再 `aipet local context 0` 即可。无需 `clear` 或卸载权重。

## 验收边界与下一步

本版**尚无新的板上速度和实声验收**，不能宣称首音已达到 3～5 秒。内部 `first-driver` 也不等于声学首音，完整端到端还包括端点等待、ASR 收尾、LLM 首句和 TTS。
当前优先消除多轮越来越慢；工作区复用是辅助优化，不声称仅凭省分配就获得倍数加速。线程池原本已在信号量上睡眠，并非空闲自旋；本轮不贸然减少线程、修改 A76 绑核或全局调度。
后续应根据本版板测的分段计时，再决定矩阵内核和 CPU 竞争优化，仍保留真实数值对照与稳定版本。

## Git 与全量恢复

- 预览标签：`a733-v1.2-v220-preview-20261101`；稳定标签不移动。
- 源码／全部历史及外部模型、静态库、镜像的校验引用：`out/checkpoints/a733-v12-v220-recovery-20261101`。仅有目录不算完成，必须有有效 `COMPLETE` 并通过验证。
- 创建与校验：`python3 tools/checkpoint-product-v12-v220.py --llm /mnt/c/Users/Lenovo/Downloads/MiniCPM5-1B-Q4_K_M.gguf`；该工具拒绝覆盖同名恢复目录。只验证现有包时，执行 `python3 tools/checkpoint-a733-project.py verify out/checkpoints/a733-v12-v220-recovery-20261101 --check-references`。
- 历史源码在私有 Git bundle 与 tar 中；模型和整卡镜像按 SHA 引用，不重复塞进 Git。恢复到新目录，不强制覆盖含有未保存修改的工作区。
- 回到稳定行为时使用 v1.1 原通过镜像／标签。v1.2 的历史设置文件独立，旧版历史设置仍保留；音量、模型路径等共有设置不是版本化副本。
