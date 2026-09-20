# A733 本地 LLM 与 AI 桌宠 Agent 接入

## v119 候选镜像

- 文件：`openvela-a733-cubie-a7z-sd-local-agent-v119-candidate.img`
- 镜像 SHA-256：`fd7f6f8c83e4bff23fe998b19f7b76f5c2094b5d39689a1f2e7befdf5bd67707`
- 内嵌内核 SHA-256：`97360881e2bd282074634c57305ec4b8a73637887017332c40709de46e510a70`
- 基线：已完成 Wi-Fi、官方 Agent 与 UART4 TTS 实机验收的 v118；只替换
  `/boot/openvela/a733/Image`，未重建分区。
- 校验：ext4 `e2fsck`、GPT `sgdisk -v`、内嵌内核回读哈希均通过。

## 已实现链路

```text
输入
  -> 桌宠快速规则
  -> 有网：openvela 官方 packages_ai_agent / DeepSeek 或 MiMo
  -> 无网或云端失败：LocalRouteBackend / Qwen2.5-1.5B Q4_K_M
  -> 统一 UTF-8、情绪与动作白名单解析
  -> 终端输出
  -> 可选 UART4 TW-TTS
```

本地入口调用 `aipetllm_infer()`，不使用 NSH 子进程。它复用已经通过板测的
Qwen2 tokenizer、ChatML、Q4_K/Q6_K NEON 量化内核、28 层前向、请求内 KV
cache、常驻模型缓存和 CPU2..7 六核工作池。CPU0..1 继续供系统、Wi-Fi 和
Agent 调度使用。当前本地生成采用贪心解码，NPU 不参与 LLM。

## 默认配置

- 模型：`/data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf`
- 最大输出：32 token，可设为 1..64
- CPU 掩码：`fc`，即 CPU2..7
- 模式：云端优先；断网或云端失败自动本地回退
- 模型按需加载；首次从 SD 读取约 1.1 GiB，实测约需 175 秒
- 加载后模型常驻 RAM，后续请求不再读取 SD

配置保存在 `/data/ai_agent/config/`，不写入镜像中的 API Key，也不修改官方
Agent 的在线配置。

## 命令

```text
aipet local status
aipet local ask "你好，请简短介绍自己"
aipet local preload
aipet local stop
aipet local unload
aipet local on
aipet local off
aipet local tokens 32
aipet local cores fc
aipet local model /data/models/qwen2.5-1.5b-instruct-q4_k_m.gguf
```

`preload` 会执行一次最短生成以建立模型缓存；首次运行长时间无输出属于 SD
加载阶段。`stop` 是协作式停止，会在层边界退出。`unload` 释放约 1.1 GiB 模型
和计算线程；推理进行中会拒绝卸载，而不会释放正在访问的权重。

## 板端验收

```text
ls -l /data/models
aipet local status
free
time "aipet local preload"
aipet local status
free
time "aipet local ask \"请只回答：本地链路成功\""
```

开启 UART TTS 后，再执行：

```text
aipet tts on
aipet local ask "请只回答：本地语音链路成功"
```

预期终端包含 `aipet-local:` 性能行、`[route=local_llm ...]`，模块播报同一回复。
随后可关闭热点并执行普通 `aipet ask`，验证自动回退；恢复网络后普通请求仍走
官方 Agent。不要在模型载入或推理时直接断电，优先使用 `aipet local stop`。

## 当前边界

- 本地后端复用桌宠表现层，但不执行官方 Agent 的云端工具循环。
- KV cache 在一次请求内有效，请求结束释放；跨轮持久 KV 仍是后续性能任务。
- 首次加载受当前 SDMMC PIO 吞吐限制，推荐按需 `preload`，暂不默认开机加载。
- 32 token 适合桌宠短答；长故事可调到 64，但延迟和热量会明显增加。
