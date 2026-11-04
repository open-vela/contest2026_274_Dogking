# v3.0pre / v227 本地镜像封存

2026-11-03。软件候选，不是新云账号、声学、机械或延迟验收。使用说明及顺序命令见 [v227](../../PRODUCT_V3_0PRE_V227.md)。

## 制品

- 新镜像：仓库外同级 `openvela-a733-cubie-a7z-sd-v3.0pre-v227-candidate.img`，2,147,483,648字节，另有 `.sha256`。
- 镜像 SHA256：`e4348914e72bcf282f42755ee3ca5e0d48985cfb62eceeab165d98827673ce64`。
- 内核 SHA256：`0321074445ab37ec3b8bf82238af5ed0d7f153e0d0508fcb11923a0b8369b74a`。
- 最终固件/构建流程冻结：`91ba41760d8a10c2dfa6409d652cc19a5fd29d00`；封装时代码提交 `f77db6251b7088d71ecb78a5d7ae690fca45fab3`。
- 最终构建：`out/product-v30pre-v227-build-r2`；41门禁：`out/v227-product-gates-r3`；完整审计：`out/v227-full-link-r2`。
- 封存固件：`out/v227-product-v30pre-release-r1`；镜像回读：`out/product-v30pre-v227-package-r1`。
- 分支：`preview/v3.0pre-v227`；发布恢复标签：`a733-v3.0pre-v227-image-20261103`；bundle：`out/checkpoints/a733-v30pre-v227-image-20261103.bundle`。

## 实际通过的软件检查

41项门禁保留本地语音、真实 CPU 声学参考、捕获张量重放、ARM 栈/ABI、旧路由/启动/取消/租约检查，再覆盖：

- 真实 localhost mbedTLS/WebSocket：证书与主机校验、合并客户端帧、分帧/ping、100k响应、空闲与总预算、延迟首包、取消、EOF与非法/超大帧。
- 真实提供商/JSON/加密代码，独立 Python HMAC 对照讯飞 ASR **及TTS** 的日期、host、path和签名；传输替身明确标记，不是云账号调用。
- 实际 RuntimePorts：云人设、持久 opt-in/off、否定邀请、多动作拒绝、未知 ACK 一次消费、TTS失败不能重发动作；Agent/UART/PCM为替身。
- 真 MCU 编码器603帧字节对照、PTY协议与故障/晚ACK；新单位独立 LE16 示例100→2000、512→10240，513拒绝，底层单包脉冲上限不扩大。
- 两槽完整句 PCM 预取的真实并发、失败尾句不重播与取消 join。
- DeepSeek官方host/flash模型精确匹配，两个请求构造器均非思考，其它提供商不注入字段。

完整最终 ELF、NPU ABI、ISA、摄像头/显示/CRC/绑核/流水线保护审计通过；本地 LLM 数学与模型不变。模型、词表、许可证、MiniCPM5、启动脚本等从最终镜像回读校验，Windows独立整镜像哈希一致。

## 失败记录与 SDK 边界

首轮旧边界测试仍把1024桌宠步当有效输入，被512新上限拒绝；已改为512正例，并补513及1024拒绝，保留 `rejected-old-boundary-r1.log`。

第二轮最后门禁发现初轮 SDK 清单漏掉新暂存的 `llm_proxy.c`，阻止发布；保留 `rejected-sdk-inventory-r2.log`。只撤回本轮注入，补入备份/还原清单后重建并完整复验，不能把原318项检查当最终证明。

最终319项按字节还原，并在封装前再次全量检查。`sdk-llm-restore.json`记录新增路径构建前及封装时相同 SHA256 `f7b20668564bd05913ed5a49fd8c5c9aba185f2ac12ba9f93b8b5ae8af58e92a`，实际 SDK 不残留注入标记。新功能只存在于 v227 构建制品，不永久污染官方 SDK。

## 仍需实机确认

用户v226真实反馈是 ASR成功、TTS超时，见[原反馈](../voice-v226-xfyun-20261103/README.md)。本轮电脑与给出的板端不在同一网段，SSH没有连接成功；没有付费云调用，没有直接驱动电机。

因此讯飞真实 TTS 发声、情绪自然度、模型动作机械效果、断网长稳与端到端首音时延均待板测。ASR仍为端点后上传，LLM仍完整响应后进入TTS，未假装已实现真正云端录音/LLM token 全流水或固定3–5秒。

v219稳定指针未改变。v219/v220/v221/v223/v224/v225/v226七个旧镜像哈希重新核对一致；本轮未烧写物理盘或 UFS，未创建PR或推送。模型动作默认off，显式开启后仍需本轮邀请、健康链路与桌宠/arm同意；确定性规则仍精确匹配，不能笼统声称所有模型动作均来自“精确原文参数”。
