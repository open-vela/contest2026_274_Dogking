# 桌宠联网链路：官方 Agent 优先

## 当前目标

暂停本地 LLM / ASR 移植，仅保留现有接口。联网版本保持原桌宠规则路由、表情、动作白名单和 UART4 TTS。I2S 不启用。

本轮新增 `apps/system/aipet/agent_bridge.{h,c}`（位于 board overlay）。它直接使用官方 `packages/ai_agent/src/core/message_bus.h` 和 `message_bus_tap.h`，不是重新实现 Agent 或仅套用云端 HTTP 请求。

## 运行契约

1. 官方 Agent 先初始化消息总线、启动 agent_loop 和 outbound_dispatch_task。
2. 桌宠单一业务所有者调用 attach；不要重复初始化总线，也不要抢读官方 outbound 队列。
3. 规则直接回复可在桌宠处理；需要云端推理时 submit，交给官方 Agent 的模型、会话与工具流程。
4. pet channel 回调只复制完整文本，不调用 TTS、网络或动作硬件。业务任务非阻塞 poll 后解析和输出。
5. 最多一个请求和一个待消费回复，拒绝并发覆盖。输入/输出上限 4096 字节，不静默截断。超时采用单调时钟，不受 NTP 校时影响。
6. 每次请求使用唯一 chat_id，隔离取消/超时后的迟到回复。当前因此不复用官方多轮 session；接入显式历史或官方 request-id 扩展前不能声称已保留多轮记忆。
7. cancel 仅停止本地呈现，不取消官方正在执行的云端请求。超时后不可自动无条件重试，以免重复工具操作或重复计费。

tap 注销不等待已开始的回调，适配器采用进程生命周期静态存储，不能动态卸载其代码。官方引擎关闭前应 detach。此接口没有引擎就绪探测，启动服务必须保证顺序。

## 尚未完成的集成与验收

- 官方 Agent、桥接源和 `aipet` 已加入候选构建；完整 ARM64 链接与板端验收仍须通过。
- 当前只注册时间、天气内置工具；动态扩展和后续桌宠动作仍须单独审查。
- 官方通用 TLS 已改为严格 CA 和主机名验证；独立在线语音传输仍待审计。
- 真正增量 token/句子输出：目前 tap 提供完整回复，不是流式 token；需扩展官方推理输出回调，不能把完整回复计时说成首 token 延迟。
- 官方在线 ASR、USB 麦克风驱动与 UART4 板端发声仍待打通；本轮可先验收键盘在线对话。
- 无网/断网反馈不阻塞界面；本地接口保留但不保证自动离线识别或对话。
- Wi-Fi 在 SMP/云端负载下做长时间丢包、重连、ARP 与资源泄漏测试。现有 IPv4 不代表云端服务可达。
- 记录 DNS、TCP、TLS、模型首 token、句尾到 UART 发出、总耗时与 P50/P95；日志默认只记录轮次、状态、耗时，不记录口令、API key 或完整对话。

## 当前验证范围

新增 NSH 命令 `aipet ask "你好"` 与 `aipet cancel`；使用官方后台 `ai_agent &`，不再启动官方会抢读 NSH 串口的 CLI 线程。Agent 等待现有 Wi-Fi 服务提供网络，启动推理循环后 attach，退出清理前 detach，不重复重连 Wi-Fi。默认不启动未认证的 WebSocket 监听服务。当前呈现仅终端文本和解析后的表情/动作数量，不执行动作、不假装已发声。

凭证只放板端 `/data/ai_agent/config/config.json`；参考 `assets/aipet-online/config.example.json`，必须替换占位符，不能提交实际 key，也不要把 key 放命令行或日志。配置应在 Agent 启动前上传。官方配置存储复用该文件。CA bundle 放 `/data/ai_agent/ca.pem`，使用可信操作系统维护的公开根证书；缺失或无效则 TLS 明确失败。确认 `wifi time` 的系统时间可信后再使用 HTTPS。

这次板级集成补丁仅允许官方时间和天气工具，禁止文件读写、shell、音乐、QuickApp 等无关工具。TLS 改为 VERIFY_REQUIRED 与主机名校验；目前在线 ASR 的独立 WebSocket TLS 还需单独审计，不能将此修复扩大宣称为所有网络服务已安全。

主机验证命令：`bash tools/test-aipet-agent.sh`。构建结束自动恢复官方文件；WSL 强制断电等异常终止仍可能跳过 EXIT，必须检查临时 staging 后再开始下一次构建。

`tools/test-aipet-agent.c` 使用真实官方 tap 实现和替代 inbound 入队函数，覆盖所有权、并发拒绝、短缓冲重取、迟到回复隔离、取消、入队失败、超时与注销。不代表已完成云端推理或板端稳定性测试。所有产品改动留在代码仓库，没有改写官方环境。
