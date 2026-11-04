# Vela v3.0pre / v227：云语音、情绪与动作预览

日期：2026-11-03。稳定版本仍为用户验收的 **v1.1 / v219**；本版是独立候选，不覆盖 v226，不创建 PR、不推送。镜像、完整哈希和软件验收以 `evidence/product-v30pre-v227-image/summary.json` 最终封存结果为准；没有该文件就不能称为封装完成。

整卡镜像烧录会覆盖目标 SD 的分区与配置；烧录前先妥善保留原配置/数据，不要把语音或 LLM 密钥贴入聊天、提交 Git 或发给答辩材料。生成镜像文件不等于已替你烧录，本轮工具不写物理盘或板上 UFS。

## 1. 本轮真实反馈与判断边界

用户在 v226 上确认：Wi-Fi RUNNING、UTC 已设置、ping 外网成功；讯飞 ASR 返回真实中文，而同一配置下讯飞 TTS 连续三次 `-110`，两次调用都未播放任何句子。

这些证据不能证明 TTS 权限、不同服务域名、TCP/TLS/WebSocket 或接收音频中的哪一项有问题。ASR 成功不能证明 `tts-api.xfyun.cn` 路径成功，ping 也不能证明 WSS 服务成功。旧代码的超时返回绕过分段诊断，因此不能把根因简单归咎于账号或 Wi-Fi。本轮电脑到原板端 SSH 地址未能建立连接，未取得板上新 TTS 实测，**不能宣称讯飞真实账号发声问题已经验收解决**。

已在放行诊断网络的环境复核：电脑 WLAN 为192.168.1.152，用户给出的板端10.195.194.171:22未能在3秒内建立连接；没有绕过 SSH 主机验证，也没有从板端导出密钥。

## 2. 云 TTS 传输修复与诊断

- WebSocket 帧头与首段最多1024字节的掩码载荷合并为一次 TLS 写入，减少小记录与 delayed ACK/Nagle 交互的风险。请求开启 TCP_NODELAY（平台支持该选项时生效）；不修改系统全局 TCP 参数，不把源码设置当成板上性能验收。
- 连接阶段20秒预算，与升级成功后的服务预算分开：ASR 总服务预算40秒，TTS 30秒，空闲无进展8秒；真实 I/O 更新空闲预算，但不能无限延长总预算。
- 输出 DNS-ready、UPGRADED、TTS request-sent、first-audio，以及出错阶段、耗时和收发字节；**不输出密钥、签名 URL、请求正文或音频内容**。
- 保留 CA 与主机名校验、TLS1.2下限、客户端掩码、分帧拼接、ping/pong、响应长度/PCM校验；没有改成不安全 HTTP 或忽略证书。
- 认证/协议错误不当作可重试网络错误；仅未播放的句子最多尝试三次，不能重播已播放句子或重发运动。

依据：[讯飞在线 TTS 官方文档](https://www.xfyun.cn/doc/tts/online_tts/API.html)。请求仍为 `wss://tts-api.xfyun.cn/v2/tts`，raw / 16kHz / mono / S16LE，默认 `xiaoyan`，文本一次发送 `status=2`。本版没有更换成超拟人、长文本或其它不兼容产品。

## 3. 在线速度改进

### 3.1 DeepSeek flash 显式关闭思考

[DeepSeek 官方说明](https://api-docs.deepseek.com/guides/thinking_mode/)指出当前默认启用思考。构建时对两个真实请求构造器都加入 `thinking: {type: disabled}`，**仅匹配 `api.deepseek.com` 与 `deepseek-flash`**。其它 DeepSeek 模型、MiMo、火山等提供商不注入该字段，不替换用户已有密钥或模型。日志标识为 `cloud-llm-v227`。

此改动减掉不适合普通口语对话的额外思考成本，但不是离线推理提速，也不能保证公网首音固定在某个秒数内。

### 3.2 TTS 下一完整句预取

两个各320000字节的自有 PCM 槽交替使用：主线程播放当前完整句时，一个可取消、必须 join 的工作线程合成下一完整句。不把正在写入的 PCM 给播放器，不预执行动作，不删除句尾。线程资源不足时退回顺序合成，不静默漏读；句子超过单槽10秒上限仍明确报错，不假装完整播放。

失败、停止和尾句都覆盖软件回归；播放仍为半双工，未实现 AEC 或打断讲话。前台中断、服务 stop 会合作结束；DNS 解析还受平台 resolver 自身超时约束，不承诺所有故障都瞬间返回。

本应用保留现有单个云合成调用150个UTF-8字节的文本上限及10秒PCM槽上限；它比讯飞官方服务自身允许的文本长度更小。过长不可分句会明确返回错误，不应将此错误称为完整播报；本版没有假装解决任意长篇故事的无限流式合成。

### 3.3 没有冒充已完成的优化

- 官方 Agent 桥接仍等待完整 LLM 响应；没有把它标成真实 token 流式 API。
- 云 ASR 当前仍在端点结束后上传保留的 PCM，讯飞按既有40ms节奏发送；本版没有声称边录边上传。
- 首句仍先完整合成、校验再播放；预取主要减少后续句间空隙，不是收到未经校验的任意音频就播放。
- 本地 LLM 数学、tokenizer、NPU ASR/TTS 模型、VIP2 ABI、CPU 全局调度与摄像头采集路径没有改动。

## 4. 云端人格与情感

使用“桌宠 Vela”的云专用指令：具体回应本轮问题、先共情再给一个小建议、分享喜悦、适当俏皮；不使用空泛客服话术，不虚构记忆、视觉、真人关系或排他依赖。默认2–3句完整口语，先给一句完整短回应；故事等长回答须自然收尾。

云回复允许应用识别的表情标签，解析后从朗读文本移除，SPI 表情仍由应用更新。最近三对已成功播报的完整云对话作为有限 RAM 历史，不能作为执行授权；超过桥接4096字节时丢弃完整历史，不切断当前 UTF-8 输入。本地人格及本地默认零历史保持原样。

“情感增强”指回复内容、表情和获授权动作，不意味着普通 xiaoyan 自动具有未购买的情感音库、变声或语音克隆能力。音色不擅自更换，实际自然度仍需要听音评估。

## 5. 云模型动作：沿用 Linux 项目的格式，收紧执行边界

对照用户给出的 `旧linux项目/vision-4/docs/vision4-requirements.md`、`src/action_parser.py` 与现有 openvela 解析器。使用完整回复中的 `[ACTION:...]`，不是声称已经实现任意厂商原生 function calling。

新增持久设置：

```text
aipet model-actions
aipet model-actions on
aipet model-actions off
```

默认 off；修改前先停止常驻。开启后还必须存在本轮“陪我玩、动一动、撒个娇、做个动作”等明确邀请，以及桌宠会话/手动 arm、健康 MCU 心跳和无故障状态。否定、引用、历史、假设或解释类文本不能作为邀请。

本版最多执行一个提议：

| 模型标签 | 参数范围 | 实际行为 |
| --- | --- | --- |
| `motor.forward(N)` / `motor.backward(N)` | 整数1–20 | 乘20成为原始脉冲 |
| `motor.turn_left(N)` / `motor.turn_right(N)` | 整数1–20 | 同上，不把步数当角度 |
| `motor.stop()` | 无参数 | 请求停止 |
| `servo.rotate(A)` | 整数−30…30 | 相对中位角度，500ms |

不执行 LED、nod/shake/wave、任意函数、连续旋转、循环、多动作队列、清故障、修改 Flash 或任意串口字节。与 Linux 旧项目不同，STM32 舵机用相对中位角度，不是0–180绝对角。聊天模型不能据此假装有摄像头画面。

只在完整响应校验后消费一次，UART ACK 超时不会重发；TTS 重试也不会再次动作。返回真实接收/失败报告，ACK 不等于运动完成。没有实测编码器就不能声称移动了多少厘米。

## 6. STM32 步数统一与兼容性变化

旧版：只有明确语音数字×10；手动 `mcu move` 是原始脉冲，易误以为倍率没生效。

本版统一：语音、`aipet mcu move`、工具显式 steps 都是**桌宠步×20**，是旧语音倍率的两倍；无数字短动作由256增为512原始脉冲。默认速度档仍为2，约300Hz；不是偷偷修改 STM32 固件的脉冲频率。

- 用户单次范围改为1–512，保持已限定的原始脉冲上限10240；超限报错，不截断、不拆成多包、不循环。
- 新增 `move-raw` 专供原始脉冲诊断，范围1–10240，不再将两种单位混在同一个命令里。
- 日志打印 input / scale / pulses；100桌宠步应发2000脉冲。若输入100却记录100，说明运行的不是本版命令或走了原始脉冲接口。
- 步数×20只扩大命令脉冲，不保证距离或速度翻倍；电机细分、轮径、减速比、负载与 STM32 配置仍影响实际位移。
- `arm`、故障/失联/NAK处理、STOP、未知 ACK 不重发、能力查询等保留；不是取消底层保护。

## 7. 按顺序板测（先声音、再聊天、最后机械）

若烧录新整卡且没有恢复原安全配置，先完成联网与隐藏输入配置，不能把 credentials=missing 当作传输修复失败：

```text
wifi connect 你的SSID
wifi time
ntpcstatus
aipet init
aipet start
aipet status
aipet audio setup xfyun
```

`aipet init` 选择 DeepSeek，默认模型应为 deepseek-flash；密钥只在板端隐藏输入。讯飞填写在线语音合成/听写对应应用的 APPID、APIKey、APISecret，默认发音人按现有 xiaoyan。没有更换凭据时不要反复 setup。需要 SSH 时沿用 `sshd --setup openvela` 后 `sshd &`，而不是 `sshd start`。

### A. 确认新版本与联网，不重建已有凭据

```text
aipet help
aipet stop
aipet audio service status
wifi time
ifconfig
aipet status
aipet audio providers
aipet audio output i2s
aipet audio volume 50
echo init > /dev/a733-audio
echo tone=1000,500 > /dev/a733-audio
```

help 应显示 v3.0pre(v227)。若 service 仍 active=1，先等待它合作退出，不强杀、不并发重启。保留原讯飞与 DeepSeek 配置，不在日志贴 APIKey/APISecret。

### B. 单独验证讯飞 TTS，保存所有分段日志

```text
aipet audio providers asr xfyun
aipet audio providers tts xfyun
time "aipet audio cloud-speak 你好，我是桌宠Vela。"
aipet audio cloud-speak "今天的语音测试已经完成，请继续下一步。"
aipet audio asr 5000
```

短语是否出声、后半句是否完整都要靠实际听音。若仍失败，看最后阶段：DNS/TCP/TLS/HTTP-upgrade/service-audio，以及 rx/tx；认证失败查讯飞“在线语音合成”对应应用密钥/音库权限，不能用 ASR 成功替代验证。不要关闭证书校验解决问题。

B 未通过时先保存完整分段日志，不进入带动作的联网联动测试；可以切 `aipet mode local` 单独复测已有本地音频和视觉功能。

### C. 情绪对话与连续多轮（先保持模型动作 off）

```text
aipet model-actions off
aipet mode cloud
aipet run
aipet audio service status
```

听到/看到 READY MIC ON 后依次说：“我今天有点难过”“我刚刚完成了作品”“你能用轻松一点的语气鼓励我吗”。每轮等播报结束并恢复 MIC ON。观察 `cloud-llm-v227`、`product-cloud-v227`、`cloud-prefetch-v227`，记录说完话→ASR结果→LLM完成→首声→播报结束，不只记录命令总时长。

```text
aipet stop
aipet audio service status
```

### D. 单位与短动作验证：架空履带或保证桌面安全

```text
aipet mcu start
aipet mcu ping
aipet mcu status
aipet mcu arm session
aipet mcu move forward 2 10
aipet mcu stop
```

应记录 input=10 scale=20 pulses=200。确认停止后如需对照原始脉冲，再显式 arm：

```text
aipet mcu arm session
aipet mcu move-raw forward 2 200
aipet mcu stop
```

不建议上来直接100或512步。先短距离校准，不能通过复用未知 ACK 命令“试到成功”。

### E. 显式开启云动作并检查一次消费

```text
aipet model-actions on
aipet mode cloud
aipet run
```

说“陪我玩，做个小动作”，观察 `product-model-action-v227` 与 MCU 的真实报告；模型未提议动作也可能正常，不承诺每句话必动。再说“不要动，陪我聊聊”，不得出现新运动帧。普通明确“前进三步”仍走低延迟确定性路由，不必等 LLM。

```text
aipet stop
aipet audio service status
aipet mcu stop
aipet model-actions off
```

### F. 回归识物与断网恢复

```text
aipet vision once
aipet mode auto
aipet run
```

先验证真实 YOLO / SPI 同帧框与播报；再在无机械动作待确认时测试断网。沿用 v226 最多三次恢复、本地120秒保持、下一轮重新判路由；同一轮动作后或已播放后不切换后端重播。停止恢复后检查 active=0，不能把所有网络错误都判为 Wi-Fi 模块坏了。

## 8. 接线保持不变

I2S：PB5 / pin12 → BCLK（模块 BCLK 与 SCK），PB6 / pin35 → LRCLK（模块 LRCLK 与 WS），PB7 / pin40 → 扬声器 DIN，PB8 / pin38 ← 麦克风 SD，共地；不要再次把 PB5/PB6 对调。合并模块电源按厂商规格，不把裸 ICS43434 的供电范围等同于整个模块。

UART4：A7Z TX 接 STM32 RX、A7Z RX 接 STM32 TX、GND 共地、115200、3.3V TTL；沿用已通过板测的 UART4 接针，不把曾用于外部 TTS 的线路与 MCU 并接。STM32 ACK 仅接收确认。

## 9. 软件证据与恢复

新分支 `preview/v3.0pre-v227`，v226 完整历史 bundle 已验证。固件与模型读回、SDK 按字节还原、ARM/ABI/ISA/ELF、摄像头/NPU原保护检查、真实本机 TLS 与独立 STM32 编码字节对照都必须通过后才封装。

首轮构建的318项清单漏掉新增暂存的 `packages/ai_agent/src/llm/llm_proxy.c`，最后策略门禁发现残留并阻止交付。已仅撤回本轮注入、将该路径加入 wrapper 备份/还原及字节清单，并要求最终319项还原、重新构建/审计后才能封装；初轮日志保留但不作为最终还原证明。新增门禁同时验证清单覆盖与实际 SDK 不含注入标记。

不能将 localhost TLS、MOCK 云/MIC/PCM、CPU声学参考、已有子系统板测混称为新整机验收。最终镜像清单、源码 commit、Git 标签/bundle 与待测项写入封存证据；v219 稳定指针不变。

## 10. 最终本地交付

已封装独立2GiB镜像：

```text
A733-A7Z-ALL-Files/openvela-a733-cubie-a7z-sd-v3.0pre-v227-candidate.img
SHA256 e4348914e72bcf282f42755ee3ca5e0d48985cfb62eceeab165d98827673ce64
```

内核SHA256：`0321074445ab37ec3b8bf82238af5ed0d7f153e0d0508fcb11923a0b8369b74a`。
最终固件及构建流程冻结：`91ba41760d8a10c2dfa6409d652cc19a5fd29d00`。

41项门禁通过，最终完整 ELF/ABI/ISA/摄像头审计通过，319项 SDK 字节还原并在封装前再次核对，模型/启动文件回读及 Windows 独立整镜像哈希通过。v219/v220/v221/v223/v224/v225/v226七个旧镜像哈希保持不变。

详见[完整封存证据](evidence/product-v30pre-v227-image/README.md)及[机器清单](evidence/product-v30pre-v227-image/summary.json)。恢复标签 `a733-v3.0pre-v227-image-20261103`，bundle `out/checkpoints/a733-v30pre-v227-image-20261103.bundle`。旧版用户PPT、素材和未跟踪副本未纳入本次固件提交，也未删除。

本版仍为预览候选：**讯飞真实发声、联网情绪/动作体验与首音时延未在新固件上实测通过**。电脑与给定板端地址之间SSH不可达，软件门禁不能替代这些板测；请从第7节B的讯飞短句开始，不直接跳到运动联动。
