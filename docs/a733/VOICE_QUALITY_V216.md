# v216 本地语音：完整回复、连续朗读与桌宠回应

2026-10-30。以 v215 `416648cf20bfb5e3bac213bacb9dce301bdd117d` 为恢复基线；保留 v204、v214、v215 镜像和全部过程文档。本版本先解决回复截断、历史污染和 TTS 碎片化，不宣称已通过新板测。

## 板测证据及原因

用户日志：附件 `1fe878cc-2ad3-499e-9c99-ebc6da1e5360/已粘贴的文本.txt`。v215 已能输出准备日志、READY、完成八轮并正常停止，不能再把本轮质量问题归结为之前 console 挂起。

- ASR 能正确识别；录音结束后的识别收尾约 0.85～1.6 秒，采集队列峰值 47～85。准确率是用户本次反馈，不是统计测评。
- 长回复反复恰好 `generated=32`，末尾停在“帮助小明解决”，仍显示 complete，并写入后续历史。生成上限和 EOS 混为一谈，是确定错误。
- 后续不同问题重复同一故事；历史滚动后前缀命中缩短，最新输入预填充变长，首字延迟最高到 55 秒。不是一句“模型已驻留”就表示上下文推理免费。
- TTS 按首片 8 个估计单位、后片 32 单位硬切，英语字母成本乘四，单词可能被分到两次合成。每片还要合成并播放完才准备下一片。
- 全角括号落入 OOV 分支，读成“未知字符编码……”；这不是用户想听的自然语音。

## 本轮改动

### LLM 完整性与上下文

1. 默认最大生成量改为 128，允许配置到 256。旧 16/24/32/64 等实验值在正常对话按最低 128 执行并打印迁移日志；预热仍只运行一个 token，不读写对话历史。
2. 明确打印 `finish=eos` 或 `finish=length`。达到硬上限返回 `-EOVERFLOW`，不再标成完整、不提交该历史对、不朗读尚未完成的尾片。已经播出的完整前缀不能撤回，也不声称可以撤回。
3. 相邻不同问题的回复如果与上一条长回复共享相同开头，先暂存这段前缀，不急着播；发现复读时只做一次无历史重新生成。仍然复读则明确失败。用户明确要求“再说／再讲／重复”时不拦截复述。这是保守质量门禁，不是通用语义判断器。
4. 增强桌宠人格，但不假装提示词能改变 1B 模型的所有固有偏好。“你是谁”等精确身份问答属于应用事实，明确日志标为 application identity，非模型生成；涉及底层模型的开放问题仍走模型。
5. 用真实 GGUF 和独立 llama.cpp 检查三对历史、当前问题、冷／热 KV 和 batch 1/4。该验证检查计算等价，不把主机速度、文本看起来合理当作板上接受标准。
6. 上下文仍是有界 RAM 会话，不是长期记忆；更长上下文仍会增加时延。没有扩大 NPU 区域，也没有改全局 CPU 调度。

### TTS 完整性与播放

- 只在句末或足够长的逗号分句，不再按字符数量切断中文词和英文单词；保留 UTF-8、角色／工具标记拒绝、队列与总字节上限。
- 一条回复使用同一个 PCM 队列、重采样相位和播放所有者，下一句合成可以与当前句播放重叠。文本源是非阻塞回调，空队列也能响应停止和设备错误。
- 每次合成仍限制十秒，但一条分句回复允许累计六十秒；设备事务仍按十秒边界收束，整体期限仍为 120 秒。边界不增加伪音频、不重放、不丢掉尾帧。超限明确失败，不静默截断。
- vocoder 长输入窗口增加左右各 16 帧上下文，输出仅裁掉重叠上下文；每一帧真实 mel 恰好输出一次。短输入保持原生 golden；全部返回浮点仍校验有限性。16 帧是本版上下文策略，不宣称已经证明覆盖模型全部感受野，连接处听感仍需板测。
- 全角括号和引号转换为停顿、保留内部文字；有限的常见非语言 emoji 转为停顿，不读编码。英文仍是现有中文声学模型的字母拼读能力，不宣称自然英语发音。
- 保留 producer starvation 的停钟／再缓冲策略及错误隔离；当模型产出跟不上实时播放时仍可能停顿，不能用静音填充伪造低延迟。

### 桌宠情绪与联网

本地提示词、已知身份回应、屏幕的保守情境回退和联网 `SOUL.md` 一并更新。显式模型情绪标签优先；没有标签时，用户明确表达难过／开心等再给出安慰／开心 UI 回退。它不是情感识别模型，可能误判，硬件动作权限完全不变。

联网 persona 会写入新镜像的 `/data/ai_agent/config/SOUL.md` 并回读验证；旧板若只替换内核，不会自动覆盖用户自定义文件。联网 API 实际效果需要有效配置后实测。本轮没有调用付费云端。

**表达更温暖不等于声线富有情绪。现有声学模型没有新增情感训练或情绪音色控制，不能声称已经实现情感 TTS。**

## 验证与恢复

保留旧版恢复点；新候选冻结源码后进行 19 组主机／mock 回归、18 个实际 NuttX ARM 编译单元、完整链接／ISA／ABI／摄像头保护验证，以及 GPT/ext4/模型回读检查。最终证据以 `docs/evidence/voice-v216` 的产物为准；测试日志明确区分真实 CPU、mock NPU 和硬件。

```sh
python3 tools/test-voice-v216.py out/NEW-regression --build ../../quickly-openvela/cmake_out/cubie-a7z_nsh_v205_camera_perf
A733_V216_BUILD_DIR=/absolute/path/to/existing-qualified-build bash tools/build-a733-voice-v216.sh out/NEW-build out/ort-static-v185-r2 --incremental
python3 tools/check-a733-voice-v216.py --build /absolute/path/to/build --build-report out/NEW-build --output out/NEW-full --firmware-commit FROZEN_COMMIT
```

## 板测顺序

先备份配置／凭据再烧录；不要覆写已通过的 v215 文件，不需要重格式化 UFS。

```text
ufs start
ufs status
```

等待挂载 `ready=1`，再执行：

```text
aipet local model auto
aipet local on
aipet local tokens 128
aipet local context 3
aipet local clear
aipet audio output i2s
aipet audio volume 50
aipet audio gain 5
echo init > /dev/a733-audio
echo tone=1000,500 > /dev/a733-audio
aipet audio general-voice /data/npu/general-asr /data/npu/general-tts
aipet audio local-speak-stream "你好（小派），今天的语音测试已经完成，请继续下一步。"
aipet audio local-speak-stream "这里是MiniCPM，现在接着读完整的后半句，不要漏掉最后这几个字。"
aipet local ask "一加一等于几？只回答结果"
aipet local ask "我叫小明，请记住。"
aipet local ask "我叫什么名字？"
aipet local ask "讲一个三句话、有结尾的小故事。"
aipet local ask "换个话题，二加二等于几？只回答结果。"
aipet audio service start
aipet audio service status
```

等 READY 后依次说：你是谁；我叫小明；我叫什么名字；我今天有点难过；给我一个三句话的小故事；一加一等于几。不要边播边说，本版仍是半双工。每句应语义对应当前问题，故事有结尾，后句不会复读故事；特别核对每次回复的最后一个词。

```text
aipet audio service stop
aipet audio service status
```

等 `active=0` 后重新 start，测空闲时停止、合成时停止、连续十轮，再回传完整日志与录音。记录说话结束、首字、首句合成、first-driver-pull 和实际扬声器首音时间；driver-pull 不是物理首音。

ASR 模型／decoder 本轮保持准确性路径不变，没有宣称 ASR 已提速，也没有宣称整链路 3～5 秒达标。

## 本地交付记录

- 冻结固件源码：`1b8a7fc7cce886b752dcb0e236d35e3f318095a3`；实现恢复点 `68b80e860e4f804aba8a565ba278d095e0f2a1c4`。
- 镜像：`../openvela-a733-cubie-a7z-sd-v216-voice-quality-candidate.img`，2147483648 字节。
- 镜像 SHA256：`14aaa2dad1c0b0edd0814f2680ea712a8a8b02695047c5dbe3dafc899ccc7e40`，已用 Windows Get-FileHash 独立核对。
- 内核：7185088 字节，SHA256 `4d722dad15168e2ea4eb84a5d46f54ec62c6af5b82f7413414bdc6db966eb4c6`。
- `out/voice-regression-v216-r2`：19 组通过，含 18 个真实 NuttX ARM 编译单元；`out/voice-full-v216-r1` 完整链接、ISA、ABI、摄像头保护检查通过。
- `out/voice-build-v216-r2`：2057 个 Ninja 目标，SDK 302 个路径逐字节恢复。
- `out/voice-package-v216-r1`：GPT/ext4/内核/启动脚本回读通过，49 项私有语音资产与三个 WAV 逐项回读；联网 SOUL 写入新镜像后也回读比对。
- `out/llm-quality-v216-r4`：真实 GGUF 三对历史，冷／热／batch1/4 六次 logits 与独立 llama.cpp 完全一致；两条实际多轮输出分别为 `1+1=2` 和 `小明！很高兴认识你！`；短故事 46 token 正常 EOS，有结尾。不是语义准确率统计，也不是板上速度测量。
- 候选标签：`a733-v216-voice-quality-candidate-20261030`；恢复包：`out/checkpoints/a733-v216-voice-quality-recovery-20261030`。
- v204、v214、v215 原镜像均仍存在；v215 SHA256 重新核对仍为 `a2cb7afe4bf003dfba82f9798e35cd76cfa4918438efebb2f5c393de7ccca771`。

证据在 `docs/evidence/voice-v216`。本轮未推送远端、未写物理 SD/UFS，也未进行新镜像的板上录放或物理首音时延验收。未知字符／复杂英语及模型的事实性仍有能力边界，不承诺根除所有幻觉或达到情感声线效果。
