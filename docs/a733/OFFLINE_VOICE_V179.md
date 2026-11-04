# v179：离线语音软件核心与统一验收顺序

2026-10-25。按用户要求，先补齐 LLM、通用 ASR、TTS，再安排整机本地全链路测试。
本阶段**不要求换卡、不下发板测任务、不发新的中间测试镜像**。不测试或更改 STM32
协议/arm，不修改 UFS/相机/全局内存布局。主机数值回归及编译检查继续进行。

## 当前状态——尚不能宣称全链路完成

| 环节 | 已有证据 | 仍缺什么 |
| --- | --- | --- |
| MiniCPM5 CPU LLM | v178 真实 GGUF 与参考完整 logits 对齐、原 Qwen 回归、ARM 编译/候选镜像 | 板上冷/热首段时间与质量；1–2秒尚未验收 |
| 通用 ASR CPU 算法 | 三份固定 ONNX、真实音频、35个 recurrent state 回灌；C 贪心解码与独立 Python token/帧位置一致 | 新 NBG 的原生动态张量/layout golden、target prepared 包和具体 provider |
| Matcha 文字处理 | C++ 词典/短语/音素前端与参考12句逐ID一致 | 完整 TN/FST 未移植；未知文字报错，不静默丢字 |
| Matcha 声学 | 两句真实主机 CPU ONNX 生成 mel | **RTOS CPU 声学执行器未移植**；不能加载 Linux `.so` 或 Windows ORT 冒充 |
| HiFi-GAN vocoder | 之前 Linux/Openvela 单模型证据保留；本版补 mel 分块/PCM转换 | 与任意 target acoustic 输出对接和整体资源验收 |
| 应用路径 | 显式 local-ASR → local-LLM → local-TTS → I2S 已接线；mock故障回归通过 | 合格 target provider 默认不存在，所以明确返回 `-ENODEV`，不伪造 ready |

`ICS43434 → NPU 通用ASR → MiniCPM5 → Matcha CPU → NPU vocoder → I2S`
**尚未完成**。本版不是“离线助手已通过”或“仅剩接线”。不能用 KWS、预录 WAV、
固定转写、云端补答替代上述缺口。主机 ONNX 输出不是厂商 NBG 原生布局 golden。

## 新增实现

- `voice_asr_decode.c/h`：6254类、512维、context=2 的通用 Zipformer 贪心解码，
  每encoder帧最多一个symbol，与厂商示例的搜索策略一致；空白/unknown跳过，
  异常special marker失败。1024帧/token上限，先检查有限值，回调前后取消检查。
  后端失败后会话不可复用；只有成功finish才可发布token，不产生半条控制指令。
- `voice_tts_frontend.cxx/hxx`：有界模型词表/词典读取，拒绝重复ID、NUL、无效UTF8；
  最长10字短语优先、词典首发音、标点分句和blank=1插入。数字逐位读中文，
  **不是**完整日期/金额/单位规范化。未知非空白字符使整句失败，不使用原参考的丢OOV策略。
  换行、tab和常见Unicode空白映射为空格。没有重分发词典或非商用模型权重。
- mel分块保留201/224/401等长度的全部尾帧，使用-11.4填充，最后一块需裁为
  `kept_frames*256`样本；不得悄悄丢掉201–224帧尾部。非有限mel/PCM拒绝，S16转换饱和。
- `voice_local.cxx/hxx`：真实本地provider生命周期与CPU/NPU能力分类、独占、取消、
  120秒绝对截止时间及粘性sink错误；具体provider必须自行约束底层硬件等待。
  无provider默认不可用，不因为有云凭据就把本地命令转云。
- 显式 `audio local-status/local-asr/local-listen/local-speak` 接口。录音最长8秒，
  预留前端尾部；本地TTS按22.05k单声道S16接收，最多10秒。合成全部成功后才整体
  320/147有理重采样到48k播放，避免每块重置相位或后半失败仍播放残缺前缀。
  超限报错，不截断。录音/ASR锁在LLM/播放前释放，原音量设置与I2S接口继续使用。
- `routed_offline_ask` 强制本地LLM＋本地TTS，不读取旧MCU结果，不执行动作，不回退云。
  原普通ask/cloud及STM32路径不改语义；显式离线路径只增加新的选择，不默认启用。

在禁C++异常的NuttX配置中，词典/STL/前端分配仍可能由系统OOM策略中止进程；
有界输入不等于所有内存耗尽均能返回错误。实际预算与资源验收仍需完成。

## 已完成的主机验证

模型沿用v178源图固定提交 `e2382758de9a0219b4efe682b95af30b399db3b8`，
逐份SHA不变。ASR vocab SHA256：
`a8e0e4ec53810e433789b54a5c0134a7eaa2ffca595a6334d54c00da858841d3`。

`test-host-speech-v179.py` 使用本地既有onnxruntime 1.23.2，禁下载/安装/板卡访问。
同一生产Kaldi前端将S16/32768处理，真实静音波形补尾，不用零mel替代。
encoder 36入/36出全部状态回灌，并记录每个输入/输出的逻辑SHA。

| 真实音频 | CPU转写 | C解码对照 |
| --- | --- | --- |
| cmd_open_light_16k.wav | 帮我打开灯 | token和帧位置完全一致 |
| cmd_time_16k.wav | 现在几点了 | token和帧位置完全一致 |
| neg_silence_3s_16k.wav | 空 | 完全一致，不发空命令 |

12句TTS参考包含“你好”“测试成功”“重庆银行”“中华人民共和国”“123”“音量50”等。
全部音素ID/blank/分句与参考一致；前两句通过真实CPU声学模型得到有限mel。
模型随机噪声导致mel不能在不同运行间简单要求逐字节相同，不能将主机mel SHA当成
固定的NPU执行CRC。声学模型SHA256：
`ff1cc7f7abdedc4eeec668a4a0dc2e72a0c314c5637d5fce885345b490bd08de`。
Matcha-baker训练数据限非商用，原许可证/NOTICE继续保留。

另以v178真实LLM曾返回的 `1+1=2` 检查，当前前端返回 `-ENODATA`：数学符号尚未
规范化，**这仍是完成前必须修复的语义缺口**，不是“数字样例通过所以任意LLM回复可播”。
证据 `arithmetic-known-gap.json`。当前选择整句失败，不偷偷删掉加号/等号后误读。

声学运行库静态审计保存在 `docs/evidence/local-voice-v179/matcha-runtime-requirements.json`。
固定图75624188B、4802节点、opset14，无Loop/If/Scan子图，但含动态长度及
RandomNormalLike；原始initializer约74269068B，**不代表峰值运行内存**。
复现工具 `tools/npu-voice/audit-matcha-runtime.py` 仅对锁定SHA源图读取，不转换/执行。

消毒测试覆盖上下文更新、blank/unknown、NaN/Inf、失败后不发布、取消、超限、
UTF8、OOV、mel尾部、S16饱和、provider占用/卸载、缺引擎不录音、无云回退、
合成失败不播放半条语音。mock应用测试不作为ASR/听音/真实硬件验收。

最终完整2026目标增量链接、121份实际暂存源摘要、五个真实ARM64编译单元及栈检查通过，
最大静态栈帧2384B；ELF内存布局通过，SDK232路径逐字节恢复。
签收 `docs/evidence/local-voice-v179/build-signoff.json`。内核2792992B，SHA256
`b900487edad586031f7a1b43c83e0df99c3653a30ca774257db3225cb0c2239d`。
该内核仅封存在 `out/v179-offline-core-20261025/`，没有制作SD镜像，未启动板卡。
第一轮构建及审计失败记录保留；最终使用r2构建、r4 ARM审计、r5真实主机对照，
不把较早源码的r1链接结果充当最终版本。

开发者内部复现（不是要求用户板测）：

```sh
bash tools/build-voice-core-v179.sh
bash tools/test-aipet-audio.sh
bash tools/test-aipet-routes.sh
python3 tools/check-a733-voice-arm-v179.py out/NEW_ARM_DIRECTORY
bash tools/build-a733-voice-v179.sh out/NEW_BUILD_RECORD
```

Windows现有Python＋本地ORT依赖可执行：
`python tools/npu-voice/test-host-speech-v179.py out/NEW_HOST_DIRECTORY`。
Linux可使用自己已安装并锁定的同版ORT；脚本不自动安装。源码/主机编译均无需联网。
失败目录保留，修复后另选新输出目录。

## 接下来的完成门禁

1. 完成新ASR NBG原生layout、类型、状态映射、padding及动态输出取证，再生成prepared包。
   已知encoder I/O约4.13MB、NBG约41.2MB，超出现2MiB tensor池和24MiB prepared区。
   不在无实证时直接扩大/改写全局MMU布局；该取证属于移植前置，不是整机体验测试。
2. 给Matcha声学实现真正能在NuttX工作的CPU执行器及动态长度/算子路径，
   保留文字→音素→mel→vocoder语义；不能拿主机ONNX运行结果当RTOS完成。
   同时补齐数学符号、日期/单位等本项目常见回复规范化，并覆盖真实LLM生成文本；
   当前逐位数字和12个样例不是完整文字规范化验收。
3. 具体provider完成模型/布局/hash/资源验证后才安装到`voice_local_install`，
   否则保持`not-installed`。接通全链路后再一次性交付用户整机测试矩阵。
4. 统一验收断网、真实说话、空输入/静音、连续轮次、取消、失败恢复、冷/热加载、
   首token/ASR RTF/TTS耗时、内存与温度。1–2秒目标必须实测，不能用缓存命中冒充冷启动。

## 保存与回退

v178基线已正式落本地Git：`f638874`，标签`a733-v178-llm-candidate-20261025`。
v179另建提交和标签，不amend、不覆盖旧标签，不推送远端。
源码提交为 `45317e1`，构建签收/补充证据使用后续普通提交；最终版本标签包含两者。
模型、镜像、日志大制品不进Git，使用摘要关联；全部开发过程文档不删除。
每个候选保留Git bundle与完整工程checkpoint，并向新目录演练恢复。
旧v178镜像仍是旧源码对应制品，不会因本版源码改变而重打相同文件名。
