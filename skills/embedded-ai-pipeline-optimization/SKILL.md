---
name: embedded-ai-pipeline-optimization
description: Optimize and diagnose embedded ASR, TTS, CPU LLM and NPU YOLO pipelines using measured stage timing, resource ownership and numerical or acoustic validation. Use for end-to-end latency, throughput, continuous-turn stalls or hot-path work on an existing supported board, not initial SoC or NPU bring-up.
---

# 嵌入式 AI 全链路优化

把瓶颈定位到真实阶段，在保留正确性、完整输出和可恢复性的前提下减少延迟。适用于 openvela/NuttX 等资源受限系统；A733/VIP2 的记录是案例，不是其他平台的固定参数。

## 从目标和基线开始

- 区分用户要建议、诊断还是实现；只要求诊断时不擅自改驱动、模型或固件。尊重当前冻结版本，独立分支/构建缓存，不覆盖通过版。此项目最终固件基线为 v229，稳定回退的证据指针与最终提交指针分开管理。
- 问题写成可测指标：说完话到第一段真实可听音频、相机每帧推理FPS、LLM首个可见token、连续多轮成功率。模型加载、等待用户说话和实际计算分别统计。
- 先读 [测量与验收](references/measurement.md)。不要把NPU算子耗时当整帧耗时，把ASR收尾当整段RTF，把驱动写入当声学起音。
- 读日志时先核对版本、模型/输入哈希、冷热状态、实际后端、内存、任务/资源所有者。`status=0`、有波形、寄存器已配置均不等于声学或整机验收。

## 按瓶颈选择材料

- 收音准备慢、端点早停、ASR跨块、TTS漏尾/断句或录放切换：读 [ASR与TTS](references/asr-tts.md)。
- 首字慢、多轮逐渐变慢、CPU量化/预填充或上下文：读 [LLM](references/llm.md)。
- 低FPS、CRC/准备/后处理、绑核、双缓冲/同帧显示：读 [YOLO](references/yolo.md)。
- 联网不稳时也读ASR/TTS的连接生命周期部分；云调用慢不能凭感觉归因网络，更不能通过暗中切本地掩盖。

## 共同约束

1. 先记录分段基线，再选择最大的可消除成本；优先复用、合并遍历和有证据的调度，最后才考虑更换模型/精度/时钟。只有用户接受质量与兼容性代价后才走后者。
2. 明确模型、DMA输入/输出、工作区、PCM和显示槽的读写所有权。清零/校验只有在完整覆盖和不变性被证明时才能省；不能跳过仍有多个写者的缓冲区。
3. NPU优化保留原输入/输出形状、量化、全部状态与输出校验；新ASR不能套旧关键词模型ABI。先在厂商Linux运行库获取真实动态输出/跨块oracle，再验证RTOS后端。CPU前端与后端数学优化分别做等价测试。
4. 核心掩码设置成功不等于线程已迁移；在实际工作线程中记录CPU身份/亲和性/频率。硬件CRC先能力检测、逐字节自测、在目标核心实际执行，仍保留软件回退。不要为局部工作改全系统调度。
5. 常驻不是泄漏。停止采用合作取消，唤醒等待者并join；不强杀NPU/DMA所有者。无法证明静止时隔离资源，报告需重启，不在同一硬件上反复reset/retry。
6. `local`不上传录音，`cloud`不悄悄转本地，只有显式`auto`可降级。音频/模型动作不得因重试而重复播放或重复运动。

## 每次交付应回答

- 改了哪一段，为什么它是瓶颈，是否改变模型/精度/音质/记忆语义？
- 同输入冷/热等价、短尾/分块/失败/取消、停止恢复与多轮分别通过什么检查？
- 板上指标的样本数、统计窗口、首尾开销、失败数、实际核心与后端是什么？没有板测时明确标为候选，不用主机模拟测试替代。
- 源码提交、模型/制品哈希、构建和测试报告在哪里，如何回到前一通过版？不把“优化实现”写成“目标已达标”。
