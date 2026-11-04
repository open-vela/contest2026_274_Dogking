# v158：RAM 常驻与连续三模型 KWS 检查点

后续更新：用户六场景实机通过，1771次调用及全部结果/chain匹配，见
[实机归档](../evidence/local-voice-v153/openvela-v158-runtime-passed.md)。
以下候选交付时的待测状态保留为历史；实时麦克风/ASR仍未通过。

2026-10-20。保留 v157 通过镜像和全部历史过程文档。本阶段遵循 NPU 适配 skill 的
分层门禁：上一阶段硬件输出通过，不等于本阶段应用闭环或实时语音已经通过。
构建/镜像状态与哈希见 [交付证据](../evidence/local-voice-v153/resident-kws-v158.md)。

## 本阶段实现

- 原 ABI v4、A7TV ABI1、2MiB I/O 池、24MiB prepared 池、32MiB DMA arena、
  MMU/时钟/IRQ/旧 LeNet 地址不变。实验开关默认仍关闭。
- 增加 `A733_NPUIOC_TENSOR_PREPARE`（0xef，query_s 参数）和 RUN 标志 RESIDENT=2。
  每个 fd 只允许一个模型，最多仍为四个上下文，快照总预算 64MiB；不向应用传内核指针。
- prepare 完整检查原始区域/输入 CRC，复制到普通堆内存；每轮 RUN 从未被 NPU 改写的
  原始快照重新构造 DMA 工作区，并再次检查 CRC，不把上轮输出当作初始模型数据。
  暖态 RUN 不打开模型文件。关闭 fd 擦除并释放快照，出错不隐式重试或回退。
- 三模型同时打开，全部 prepare 成功后才运行。encoder output1..37 直接反馈到
  下一块 input1..37；input38 明确为 FP32 特征偏移 0/16/32…，不是复制 output38。
- 每块 29×80 fbank，步进 16 帧，输出 4×320。decoder 两 token 上下文最多缓存 64 项，
  joiner 返回 263 维真实 logits，使用既有同一份 `voice_kws.c` 搜索核心。
- 原生所有输入和所有输出逐次核对 Linux 实测 CRC，未知输入在提交硬件前拒绝；
  状态和输出不使用冻结文件替代。reference.bin 是校验数据，不是后端输出。
- 最多 64 块/6000 次调用/900 秒软件预算，单次 NPU deadline 5 秒。
  在途工作禁止异步取消；任何错误停止、不 reset/retry，硬件/guard 错误继续隔离。
  18KiB logits 和模型/状态/搜索空间在堆上，不占 NSH 16KiB 任务栈。

入口为 `aipet audio npukws <模型目录> <features.f32>`，无需 Agent/API Key 配置。
它只读显式特征文件，不启动麦克风、不上传、不联网、不播音、不向 Agent 发送动作。
文件最长 1024 帧，所有 FP32 有限值、尺寸、关键字/参考表边界均先检查。
CRC 仅用于回归，不代替 SHA256/来源验证，也不把受信任 prepared 命令当安全沙箱。

## 六场景核对基线

以下是**实际应用代码在主机上读取已有 Linux 原生输出**后产生的预期；不是 v158
openvela 实机结果。三模型常驻的实际内核路径另有 mock 文件/硬件的适配器回归。

| 文件 | 场景 | calls | hits | chain | 命中 |
| --- | --- | ---: | ---: | --- | --- |
| features0.f32 | 完整唤醒词 | 161 | 1 | c0a6f8f2 | 你好小瑞 |
| features1.f32 | 短唤醒词 | 278 | 1 | 871a92c3 | 小瑞 |
| features2.f32 | 重复唤醒词 | 298 | 1 | 66236234 | 小瑞小瑞 |
| features3.f32 | 非唤醒命令 | 391 | 0 | a1596f25 | 无 |
| features4.f32 | 噪声 | 96 | 0 | 1d4e2db1 | 无 |
| features5.f32 | 静音 | 547 | 0 | aa07f970 | 无 |

共 1771 次应用模型调用。先前 Linux 回归 1728 次，因其跨六场景复用 decoder/joiner
查询缓存；本入口每场景独立释放，只缓存 decoder，所以次数不同，不是计数造假。
chain 汇总所有调用的模型编号/输入 CRC/全部输出 CRC，并非音频准确率指标。
“小瑞小瑞”原始阈值仍接近边界，六个固定样本不能证明普适唤醒准确率。

## 上板步骤

1. **先备份整个 data 分区内容**：模型、应用配置、SSH/Wi-Fi/语音凭据等。
   新整卡镜像烧录会覆盖 data。v157 镜像及现有 SD 内容不能视作自动保留。
2. 使用单独的 v158 候选镜像；不要覆盖电脑上的 v157 文件。
3. 在电脑核对私有资产包 SHA256 和包内 assets-sha256.json。
   先从已备份的 v156 完整资产恢复 `/data/npu/voice`（含下面旧检查点需要的 input 目录），
   再合并 v158 包的 `voice` 内容。新包只有三个模型和 kws，不包含旧 input/dynamic/vocoder。
   encoder/decoder/joiner 包与 v156/v157 相同；新增主要是 `voice/kws`。
4. 不后台并发 NPU、不执行私有 `/dev/a733-npu` 控制，保留完整 UTF-8 串口日志。

```text
npu info
npu selftest
free
aipet audio npuref joiner /data/npu/voice/joiner.a7tv /data/npu/voice/input/joiner
aipet audio npuref encoder /data/npu/voice/encoder.a7tv /data/npu/voice/input/encoder
```

旧检查点通过再继续；任一错误立即停，不连续重试，不 Ctrl+C/task_delete 在途任务。
本命令是有限场景，正常会自己返回 NSH。首次输出前要把三份模型读入 RAM。

```text
aipet audio npukws /data/npu/voice /data/npu/voice/kws/features0.f32
npu info
free
aipet audio npukws /data/npu/voice /data/npu/voice/kws/features1.f32
npu info
free
aipet audio npukws /data/npu/voice /data/npu/voice/kws/features2.f32
npu info
free
aipet audio npukws /data/npu/voice /data/npu/voice/kws/features3.f32
npu info
free
aipet audio npukws /data/npu/voice /data/npu/voice/kws/features4.f32
npu info
free
aipet audio npukws /data/npu/voice /data/npu/voice/kws/features5.f32
npu info
free
```

每条完成需 `status=0`、calls/hits/chain 与上表相符、`resident released`。
空载 NPU free 应恢复 `1654784`；堆用量检查相对**本次开机的测试前快照**，不要机械
要求与历史某次完全相同。保留首次冷加载时间和每模型 run-us 总耗时；run-us 仍包括
RAM 重建/CRC/缓存维护/复制，不是裸 NPU 时间，也未含麦克风/fbank 实时前端。

电脑日志核验：

```sh
python3 tools/npu-voice/audit-openvela-npukws.py \
  out/a733-openvela-kws-v158-r2/application-expected.json /path/to/serial.txt
```

## 可复现开发验证

```sh
bash tools/test-a733-tensor.sh
bash tools/test-aipet-npuref.sh
bash tools/test-aipet-kws.sh
bash tools/test-aipet-audio.sh
python3 tools/test-openvela-npukws-log.py
python3 tools/test-voice-prepared.py
python3 tools/test-aipet-npukws.py out/a733-openvela-kws-v158-r1
```

最后一项需要私有 host-native-map.json 及已有 Linux 报告的原始输出；它只用于主机
文件后端验证，不能安装到设备上冒充推理。发布测试包不含此映射或原生输出张量。
构建沿用 build-a733-wapi.sh 的暂存/退出还原流程，在全新 v158 构建目录打开
`A733_TENSOR_EXPERIMENTAL=1`；不要清理 v157 构建目录。

## 后续门禁

先完成本版六场景 openvela 硬件检查、暖态时延和释放/重开测试，再把已通过的 ICS43434
48k→16k/fbank 接入有界连续唤醒。ASR 仍缺独立模型转换/36 路状态验收；Matcha 声学
前端也未在 openvela 完成。KWS、声码器输出通过均不能代替完整离线语音助手。
