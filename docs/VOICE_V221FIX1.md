# v221fix1：语音端点与长播报源码修复候选

后续交付（2026-11-02）：本修复已进入独立 [v1.3fix／v223镜像](PRODUCT_V1_3FIX_V223.md)，完整构建、31项软件门禁和镜像回读通过。以下“源码／未封装”保留为此前过程记录；原v221镜像没有被覆盖，v223仍待新板测。

2026-11-02；基线为 `a733-v1.2fix-v221-image-20261102`。保留原v221、v220和v1.1镜像，不占用尚未确认MiMo方案的v222版本号。本轮为源码和软件验证，**未完整链接／封装新镜像，现有v221镜像不包含这些修复**。不推送、不提交PR。

## 最新板测诊断

附件SHA256 `dba4f1d075a260dbd0428e3ea5fb6746b846099bc10236b2a1bf7326e4419676`。
两轮常驻采集均为 `asr=0 accepted16=128000 limited=1`，随后 `overlong utterance discarded`。不是模型加载失败，而是没有触发正常结束，达到8秒硬上限后拒绝发布可能截断的识别结果。没有输出文本不等于已经证明ASR准确率下降。

旧gate的启动阈值为3倍噪声RMS／配置floor，继续语音阈值却为2倍噪声／floor乘约0.707；低于触发标准的室内噪声波动能反复清掉700ms静音计数。修改为同一触发／继续证据强度，保留700ms静音、400ms前滚、160ms启动确认及8秒硬上限。仍只是能量门控，不是训练过的VAD／AEC；弱尾音和不同环境须实测，不承诺从此不存在端点问题。

旧／新生产header在同样合成输入（180RMS背景，1000RMS语音，300～450RMS安静波动，带DC）下对照：旧版输出800块并标超长丢弃；新版在70块安静后结束、154块输出、不标超长。持续强语音8秒仍丢弃，不把截断命令发给电机。增加 `gate-end-v221fix1` 诊断，打印末端噪声、能量、voiced／quiet块计数；没有存储录音。

重启后直接 `aipet audio general-stream 5000 0` 的 `-19` 是另一种情况：通用后端 `current` 未安装，调用尚未进入录音。`mode local`／`output i2s` 不负责为底层测试安装模型。正确隔离命令见下文；之前给用户的简化命令漏掉general-voice初始化，已更正。

后续用户在原v221按正确顺序初始化，固定5秒ASR已成功：`samples16=80000`、`result=0`、`general-asr transcript: 你好你好`，用户确认ASR没有问题。这验证当前麦克风→FIR→NPU ASR链路在该样例可用，不把它扩展为端点修复已板测或全面准确率验收；后续继续只改门控／长播报，不更换ASR模型。

## TTS问题与修复边界

- MCU查询和小步命令已经ACK成功，后面的 `-90` 是播报错误，不是电机命令失败。不要据此手动重复运动。
- `-90`：engine、provider、单文本流播放器仍累计限制10秒，即使文本已分成多段仍会中止。增加显式流式provider入口，允许60秒总音频；原缓冲接口保持10秒，单包仍≤51200源样本，RAM队列和120秒截止保留，取消／错误不伪装成功。
- `-7`：满足127个输入ID不保证声学输出≤861 mel帧。默认按完整词条拆到最多24音素／49个blank展开ID，不改变声学模型、vocoder形状或越界检查。单词条超限仍拒绝，不截断词条。
- 完整中文诊断真实CPU声学执行后得到约20～22秒的mel对应PCM长度，新分段不再触发旧10秒边界；NPU是测试桩，不是实机听音。
- **自然英文仍未实现**：现有Baker词表是中文音素，之前的英文处理是逐字母中文读音。修复只保证这种明确的拼读回退和混排长文本可完整处理，不能宣称Hello等已自然英文发音。需要另行选择并验证双语／英文声学模型，或使用真实云TTS；没有偷偷上云或替换模型。
- 额外修正UTF-8短尾字节验证，拒绝未完成序列，避免越界读取。

## 当前v221可执行的ASR隔离测试

先停止，等 `active=0`；如有运动风险先手动 `aipet mcu stop`。这条测试只打印ASR，不派发动作：

```text
aipet stop
aipet audio service status
aipet audio general-voice /data/npu/general-asr /data/npu/general-tts
aipet audio general-stream 5000 0
```

等 `configure result=0`，再运行第二条；等MIC ON后说短句，随后安静等待。固定5秒、关闭能量端点，预期 `general-asr transcript: ...`。新初始化约20秒只表示模型准备，不是录音正在进行。若 `general-voice` 返回错误，先核对两个模型目录；不要反复在没有后端的状态调用general-stream。

## 文字／语音／电机／摄像头测试顺序

普通文字动作路由也是真实运动，先电机悬空或做好急停。PA9/TX→A7Z PJ25物理18脚/RX；PA10/RX←A7Z PJ24物理16脚/TX；共地、115200、8N1，不共接两板供电。

```text
aipet mode local
aipet audio output i2s
aipet audio volume 50
aipet mcu start
aipet mcu ping
aipet mcu stepper-stat
aipet mcu arm
aipet ask "前进十六步"
aipet mcu stepper-stat
aipet ask "停止"
```

看到 `ack-status=0` 即下位机已接收；后续TTS失败不可重发运动。STOP解除授权是预期行为。单靠日志计数不是实测位移。

```text
aipet run
aipet audio service status
```

等READY／MIC ON，再从另一个shell或同一NSH手动 `aipet mcu arm`，然后说“前进十六步”“停止”。启动服务会disarm，必须READY后重新授权；“向左看／向右看”是舵机，不是底盘。未安装舵机不测试该项。当前半双工，没有AEC／打断，不在扬声器播报时说新指令。

监听中可说“识别物体”，触发单次新帧YOLO＋SPI＋播报。连续摄像头与语音服务互斥：

```text
aipet stop
aipet audio service status
aipet vision once
aipet vision start 30
aipet vision status
aipet vision stop
```

每个阶段等前一步结束，不把整段连续粘贴成并行操作。识图前须等语音active=0；连续摄像头仅画面／框展示，不等于持续播报每一帧。

## 软件证据与恢复

[证据](evidence/voice-v221fix1/README.md)。实际CPU声学＋五组旧黄金输出、NPU桩、真实词典前端、11秒单次流式输出、10／60秒边界、取消／错误／短尾／重采样、端点旧新对照及实际ARM单元编译已执行。没有硬件声学、ASR准确率或新镜像验收。SDK源码未修改，NPU ABI、驱动、模型、人设、全局调度未修改。

代码分支 `preview/v1.2fix-v221-voicefix1`；源码标签 `a733-v221-voicefix1-source-20261102`。下一个镜像需要单独完整构建、门禁、封装和回读，不能把旧v221镜像改名。
