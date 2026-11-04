# 测量、正确性与发布边界

## 语音时间线

采用同一单调时钟记录以下事件，按轮次/请求ID关联。麦克风/扬声器的物理时刻可通过GPIO标记、逻辑分析仪或外部录音对齐；没有物理测量就明确写“软件时间”。

| 事件 | 含义 |
| --- | --- |
| load-start / models-ready | 冷加载、预热结束；单独给出，不能隐入每轮推理 |
| speech-end | 用户最后一个有效语音样本，不是静音端点判定时刻 |
| endpoint / asr-final | VAD提交与最终文本可用；差值才是相应收尾 |
| llm-submit / first-visible / llm-done | 区分预填充、首字、后续生成 |
| clause-ready / tts-start / pcm-ready | 首个完整可合成短语、声学/vocoder、首段真实PCM |
| driver-first / acoustic-first | 首次送入设备与真实可听起音，二者不能混用 |

`首音 = acoustic-first - speech-end`。有重叠时不要把所有内部耗时直接相加：例如ASR在录音中推进，TTS与后续句生成/播放有重叠。同时给出端点等待、排队和输出设备延迟，不能只报“模型计算”。

## 视觉时间线

记录帧ID、采集、RAW处理、letterbox、输入准备、NPU提交/IRQ/等待、输出/权重检查、解码/NMS、SPI绘制、发布和清理。标注父/子阶段，package若包含在prepare中就不能重复求和。

- FPS分开报：仅预览、每帧YOLO、跳帧检测、稳态、含冷启动/停止的整段。
- 显示帧必须绑定其模型输入与检测框的同一帧ID；生产但未消费的槽不计显示FPS。
- 短测给出显示帧/推理次数、有效秒数、错误/清理/回退次数。短测无错不是小时级长稳。

## 比较实验

固定模型/输入/配置/统计口径，分别测冷启动和热态，交错基线与候选，报告中位数/p95、范围和失败数。单条最好结果、不同提示词、重复问题的KV命中不能代表通用性能。使用同样本板测之前，A/B主机测试仅证明语义/数值关系。

测试包含静音、极短语音、末尾不足一个块、长回复、混合中英/未知词、零检测、变化输入、OOM、设备失败、取消、多轮和停止重启。资源计数要看所有owner退出后是否回归基线；RAM常驻允许高水位稳定，不要求回到冷启动值。

## 本项目已保留证据

路径相对比赛仓根目录；使用前先检查其版本与内容，原始数字不是当前版自动继承的性能承诺。

- `docs/evidence/camera-v206-board-20261029/README.md`
- `docs/evidence/camera-v207-board-20261029/README.md`
- `docs/evidence/camera-v208-board-20261029/README.md`
- `docs/evidence/product-v11-v219/board-accepted-20261101.json`
- `docs/PRODUCT_V1_2_V220_PREVIEW.md`
- `docs/a733/VOICE_V211.md`
- `docs/PRODUCT_V3_0PRE_FIX2_V229.md`
- `docs/evidence/product-v30prefix2-v229-image/README.md`

当前材料不支持宣称30FPS、自然英文TTS、全双工/AEC、整机首音3–5秒已统一验收，或v229已完成新版真实云账号长稳。新证据可以更新结论，但不得倒改旧日志。
