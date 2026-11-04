# 答辩三张亮点生图：文案与证据

2026-11-04。作者要求使用生图功能，替换此前PPT中不满意的亮点呈现。采用内置imagegen逐张生成16:9宽屏技术图，并逐张检查；不覆盖原PPT。

交付目录（相对工作区Openvela）：`决赛答辩/亮点生图版_20261104/`。三张PNG及完整提示词单独保存在此目录。最终1672×941，约16:9，不宣称4K。可插入PPT整页使用，图中文字不可编辑。

## 1. 在线Agent

`01_Agent在线交互.png`。I2S采集/端点→云ASR→规则优先的意图路由→云LLM人设与有限上下文→完整回答解析→正文云TTS、表情SPI、受限ACTION通过UART4到STM32。

示例：用户“我有点紧张，陪我玩，动一下。”；回复“[安慰] 我在这里陪你，慢慢来。[ACTION:motor.forward(3)]”。明确为协议示例，非板测录音转录。3桌宠步×20=60脉冲。明确的前进指令通常走规则，不必经过LLM。

依据：`board/a733-cubie-a7z/openvela-overlay/apps/system/aipet/cloud_persona.hxx`、`product_runtime.cxx`、`product_core.cxx`、`mcu_link.cxx`、`tools/test-product-v227.cxx`。默认关闭模型动作，需要显式开启、本轮邀请、链路/故障及参数验证；每轮最多一个，不执行任意代码；ACK≠完成。

## 2. YOLO流程与优化

`02_YOLO优化实测.png`。IMX214 RAW→CPU图像处理→640×640 RGB→VIP2 YOLOv8n→六输出/NMS→同帧SPI底图与框。对比历史短测：

| 版本/路径 | 稳态FPS | 30秒帧/推理 |
| --- | ---: | ---: |
| v206 | 2.483 | 74/74 |
| v207 | 5.158 | 154/154 |
| v208串行 | 6.455 | 193/193 |
| v208双槽 | 7.733 | 231/231 |

整段含首尾开销7.324FPS；2.483→7.733约3.11倍。v206→v207准备97.15→33.2ms，输出/权重验证152.4→55.2ms；NPU硬件+等待约17.1ms基本未变。package是准备子项，不重复加总。该轮A55/软件CRC，不能声称A76/硬件CRC带来这些收益；30FPS和长视频长稳尚未验收。

证据：`docs/evidence/camera-v206-board-20261029/README.md`、`camera-v207-board-20261029/README.md`、`camera-v208-board-20261029/README.md`。优化保留全部六路检查与冷/热一致性，不以删校验换速度。

## 3. 离线语音闭环

`03_离线语音闭环.png`。ICS43434 I2S、×5增益→CPU48→16k重采样/特征→NPU Zipformer与CPU解码→CPU MiniCPM5-1B Q4_K_M（688065920字节）→CPU ORT声学+NPU vocoder→MAX98357A I2S。

优化说明：初始化一次、模型/会话/权重常驻、跨块状态、400ms开头保留/700ms静音、多核/批预填充/精确前缀KV、本地历史默认0、人设保留、完整短语/尾包与合成播放衔接。半双工，不是AEC或播报中打断。

v1.1/v219真实本地多轮获得作者确认；8次ASR尾段0.950–1.513s，只是收尾，不等同整段识别或整机首音。整机3–5s首音仍未统一验收。

依据：`docs/evidence/product-v11-v219/board-accepted-20261101.json`、`docs/PRODUCT_V1_2_V220_PREVIEW.md`、`docs/PROJECT_FULL_SUMMARY.md`。本地英文未知词容错不等于自然英文TTS。数据不因重新制图成为v229新板测。
