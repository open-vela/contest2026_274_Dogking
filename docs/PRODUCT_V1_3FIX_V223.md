# v1.3fix／v223：语音端点与长播报修复候选

日期：2026-11-02。最新稳定版仍为 **v1.1／v219**；本版需要上板回归，不宣称整机已经通过。

后续真实反馈：独立ASR／长句TTS通过，但常驻短命令仍反复8秒拒绝，第三轮日志末端缺少ASR收尾完成，不能称常驻板测通过。原日志与诊断保留于 [v223板测记录](evidence/voice-v223-board-20261102/README.md)；新 [v1.3fix2／v224修复镜像](PRODUCT_V1_3FIX2_V224.md)独立交付，原v223镜像不改写。

## 交付与版本

新镜像独立命名为 `../openvela-a733-cubie-a7z-sd-v1.3fix-v223-candidate.img`，不是把旧 v221 改名。大小2,147,483,648字节。镜像SHA256及独立Windows校验见[封存证据](evidence/product-v13fix-v223-image/README.md)。

镜像SHA256：`43380317ae7958d0d2eb12e61984603d411b05d478fbfa1db8d7ad20b522bb31`；同目录 `.img.sha256` 与Windows独立Get-FileHash一致。

固件源码：`fb824fbfa9a3db6d4d64fa87be612849b275e49f`；内核SHA256：`828f52d9ec92f3d2e3fab1fd1ad57ad5b58dfa8a4903816a1113add454574c19`。
源码修复过程保留于 [VOICE_V221FIX1](VOICE_V221FIX1.md)。原v219稳定、v220及v221镜像、文档、标签均保留；不提交PR、不推送、不烧写物理SD或UFS。

v222继续留给尚待确认的MiMo方案。本版没有偷偷改用按量付费API，没有新增MiMo全家桶，也没有用LLM密钥冒充独立语音凭据；原有DeepSeek、MiMo LLM及既有语音服务商配置保留。

## 本次修改

1. **常驻ASR端点**：旧版启动与继续阈值不一致，背景波动可能不断清零静音计数；统一证据强度，保留400ms前滚、160ms启动确认、700ms静音和8秒超长拒绝。新增 `gate-end-v221fix1` 末端诊断。持续讲话达到上限仍拒绝发布截断动作指令。
2. **长TTS累计边界**：原engine、provider和单文本播放器仍可能在总计10秒处中断。增加显式60秒流式入口，并统一文本／回复流式总量；旧缓冲接口仍为10秒，不放宽单个vocoder张量检查。
3. **声学分段**：按完整词条最多24音素／49个blank展开ID分段，避免输入合法但声学输出超861 mel帧。词条不半截丢弃，单词条超限明确报错；保留短尾、取消、错误传播、RAM队列与截止时间。
4. **UTF-8边界**：修正不完整尾序列验证，拒绝不完整字节，避免越界读取。
5. **独立v223固件标识**：产品帮助与常驻服务状态均带v1.3fix/v223。仍沿用 `CONFIG_A733_PRODUCT_V12=y` 及独立预览配置；本地上下文默认0，人设、常驻权重、精确前缀KV保留。没有修改全系统CPU调度、ASR模型、NPU ABI或驱动。

**英文限制必须明确**：现有Baker中文模型只能按字母的中文读音拼读Latin文本，本次不等于自然英文朗读。自然双语需另选声学模型并验证，或配置真实云TTS。声学处理及长文本软件测试通过，不等于已经听过新固件输出。

## 软件验证与边界

- 完整ARM固件构建、31项软件门禁、ELF／ISA／NPU ABI／内存布局／摄像头软件审计；SDK暂存316项按字节还原。
- 同输入旧／新端点对照：旧版800块后超长丢弃；新版70块安静后结束，输出154块、不标超长。它仍是能量门控，不是训练VAD、AEC或打断。
- 真实CPU ORT声学执行及5组既有黄金对照；混排长文本约20～22秒的声学长度不再撞旧10秒限制。这里vocoder是NPU测试桩，不是硬件音频验收。
- provider验证旧10秒、流式11秒成功、超过60秒报错；取消／断流／重采样等边界保留。6个修改单元另做真实ARM编译。
- MCU沿用v221固定上游 `efdb437948eda62cbc782692eb8fd291e5d914ae`。真实GGUF数值参考继承v220，仅版本banner允许归一化，其他数学源码逐项同哈希；不将旧oracle冒充新板测。
- 基础门禁第一次因编译配置尚未生成而失败，原始日志保留 `out/v223-voice-base-r1`；随后完整19项重跑于r2，再完成31项产品门禁，没有跳过失败项。
- 新固件实际声学、弱尾音、环境噪声、连续多轮及3～5秒首音仍待实测。原v221固定5秒ASR成功，只证明原链路该样例可用，不能当新端点已经上板验收。

## 接线

合并模块的扬声器和麦克风时钟需要分别连接（若模块未内部共接，不要漏掉另一组）：

| A7Z物理脚／信号 | 模块端子 |
| --- | --- |
| 12／PB5 BCLK | 扬声器BCLK及麦克风SCK |
| 35／PB6 LRCK | 扬声器LRCLK及麦克风WS |
| 40／PB7 DOUT0 | 扬声器DIN |
| 38／PB8 DIN0 | 麦克风SD |
| GND | 模块GND |

注意此前故障是PB5/PB6接反：**PB5不是LRCLK，PB6不是BCLK**。合并模块VCC按已通过的模块供电方案，不把裸ICS43434当5V器件；功放OUTP/OUTN接扬声器，不能将任一端接地。

UART4仍为115200、8N1：物理16／PJ24 TX→STM32 PA10 RX；物理18／PJ25 RX←PA9 TX；共地，不共接两板供电。旧UART TTS模块不能同时占用UART4。

## 上板测试：按阶段执行，不整段并发粘贴

烧录会覆盖所选SD卡；先备份配置，核对目标盘，不选UFS。启动后先确认实际版本，电机保持未授权：

```text
aipet help
aipet audio service status
ufs status
aipet mode local
aipet audio output i2s
aipet audio volume 50
```

帮助应为v1.3fix CANDIDATE/v223；服务状态也应有v223。UFS未挂载时先运行 `ufs start` 并检查状态，不格式化UFS；镜像内SD有MiniCPM5备用模型。检查实际模型路径用 `aipet local status`。

### 1. 隔离ASR与TTS

```text
aipet audio general-voice /data/npu/general-asr /data/npu/general-tts
aipet audio general-stream 5000 0
```

必须等 `configure result=0` 后再执行第二条；等MIC ON后说短句，预期打印真实ASR文本。该测试不执行LLM或电机动作。重启后直接general-stream返回-19通常表示还没有安装通用后端，mode/output不负责这个初始化。

```text
aipet audio local-speak-stream "你好，语音测试成功。"
aipet audio local-speak-stream "Hello world. English TTS test."
aipet audio local-speak-stream "今天的语音测试已经完成，请继续下一步。现在检查长句是否能够完整朗读，不要遗漏后半句，也不要在词语中间停止。"
```

逐条等结束，确认中文完整、长句完整；英文预期是字母拼读，不以它验收自然英文。

### 2. 常驻多轮与停止恢复

```text
aipet local on
aipet local preload
aipet run
aipet audio service status
```

等READY／MIC ON后说“你好，今天心情怎么样”，说完安静1～2秒，等整句读完及再次MIC ON，再说第二句。短句预期 `limited=0`，端点quiet达到70块；如仍超长，保留完整gate诊断，不反复启动。当前半双工，不在播放期间讲话。

上下文默认关闭；先测默认速度。若要验收记忆，停机等active=0后运行 `aipet local context 3`，重启监听，依次说“我叫小明”“我叫什么名字”；测完停机并用 `aipet local context 0` 恢复低延迟设置。切换会清历史，权重保留。

```text
aipet stop
aipet audio service status
```

等active=0后再次 `aipet run`，确认可恢复，最后再stop并等active=0。不要强杀持有NPU的线程。

### 3. MCU与视觉回归

先MCU只读查询：

```text
aipet mcu start
aipet mcu ping
aipet mcu config-get
aipet mcu stepper-stat
aipet mcu link-stat
```

仅在通信有效、fault=0、配置与硬件相符、做好急停后手动arm；语音服务启动会disarm，需READY之后重新 `aipet mcu arm`。然后说“前进十六步”“停止”，用stepper-stat核对。ACK不是运动完成，脉冲计数不是实测位移；播报失败不能据此重发运动。STOP解除授权是预期行为。

监听中说“识别物体”测试单次YOLO＋SPI＋播报。连续视觉要先停止语音并等active=0：

```text
aipet stop
aipet audio service status
aipet vision once
aipet vision start 30
aipet vision status
aipet vision stop
aipet mcu stop
```

连续摄像头与常驻语音互斥；不要把它当作两者已无限并行。联网回归需原有真实凭据，未配云语音不能用本地结果冒充联网验收。

### 4. 延迟记录

保存从启动至两轮完整应答和stop的串口日志。区分冷加载、READY等待、说话结束→ASR结束、LLM首可见、首句合成与真实扬声器起音。软件计时不等于实际声学首音；本次没有新的3～5秒达标证据。

## 构建与恢复

实际完整构建：`tools/build-a733-product-v13fix-v223.sh`，缓存 `quickly-openvela/cmake_out/cubie-a7z_nsh_v223_product_v13fix`。
构建报告 `out/product-v13fix-v223-build-r1`；基础回归 `out/v223-voice-base-r2`；31项产品门禁 `out/v223-product-gates-r1`；完整审计 `out/v223-full-link-r1`。
封装工具 `tools/seal-product-v13fix-v223.py`；封存二进制 `out/v223-product-v13fix-release-r1`；镜像回读 `out/product-v13fix-v223-package-r1`。
上述输出目录已存在；重跑要选新目录并版本化封装路径，不删除旧证据或覆盖旧镜像。

发布恢复标签：`a733-v1.3fix-v223-image-20261102`；Git bundle：`out/checkpoints/a733-v13fix-v223-image-20261102.bundle`。私有模型／SDK／构建输出不包含在Git源码bundle内，保留现有本地资产及旧版完整恢复目录。
