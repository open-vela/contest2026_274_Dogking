# 联网 Agent 集成板测（不是完整桌宠验收）

## 镜像范围

此轮目标是官方 AI Agent 后台 + `aipet ask` 文本闭环。只有构建、符号、镜像回读校验通过后才生成候选镜像。USB 麦克风、UART4、屏幕和真实动作不在当前已验证范围。

2026-09-17 ARM64 构建通过，内核 1,887,448 字节，SHA256：
`868018554c8986f166ff873276c78911f559793237f579c76ad04096e9aa39c3`。
已检查 `aipet_main`、`ai_agent_main`、`aipet_agent_attach` 符号和 ARM64 Image 魔数。
配置保留 CONFIG_SMP_NCPUS=8，启用 CONFIG_EXAMPLES_AI_AGENT_VELA，禁用 Agent shell 工具。
候选镜像名为 `openvela-a733-cubie-a7z-sd-online-agent-v98-candidate.img`，在比赛仓库的上一级目录；
镜像生成校验日志 `a733-online-agent-v98-image.log` 与镜像同目录。
最终镜像 2,147,483,648 字节；加入 CA/SOUL 后的 SHA256：
`d9e27f996c12d0b47a65e573afa3914f1a26e797cbcad24ae0abf16e3ac68fee`。
GPT、ext4 检查通过，内核与 CA 回读一致。日志中较早的 f55b... 是添加资源前的中间镜像哈希，不用于最终校验。
完整烧录测试尚未进行，不代表已达到长期稳定、低延迟或完整语音桌宠验收。

1. 烧录候选镜像前备份 TF 卡数据；烧录会覆盖模型、网络配置和 SSH/FTP 配置。
2. 启动后先执行 `aipet`，确认命令存在；`aipet ask "你好"` 在 Agent 未启动时必须明确失败，不应崩溃。
3. `wifi connect <SSID>` 联网，然后 `ifconfig`、ping 网关、DNS 测试。执行 `wifi time` / `ntpcstatus` 确认日期正确。
4. 通过现有 FTP 服务上传私有云端配置到 `/data/ai_agent/config/config.json`。参考仓库的占位符模板，在自己的电脑上另存编辑，不把真实凭证提交仓库。
5. 镜像内需要 `/data/ai_agent/ca.pem`；只有可信根证书，无私有 API key。
6. 执行 `ai_agent &`，等待 ready 日志后再执行 `aipet ask "你好，请简短介绍自己"`。不要同时重复启动 Agent。
7. 保存 `dmesg`、`ps`、`free` 和完整错误返回，不发送 API key。
8. 连续 20 轮测试，间隔穿插 Wi-Fi ping；对比首轮和第 20 轮内存。当前 elapsed 是完整回复延迟，不是首 token 延迟。
9. 拔掉/关闭热点后再请求，必须有界返回错误。不自动重试可能带来副作用的工具请求。
10. 当前 Ctrl+C 强制结束前台命令不等于停止云端推理。被中断后执行 `aipet cancel` 清除待呈现状态；不可声称云端已取消。

更换根证书或私有配置后，在重启 Agent 前保存当前日志。此版本的官方线程退出同步尚待审查，不用反复 kill/启动冒充可靠服务管理。

## 开发检查中新发现的限制

本轮编译错误与修复记录：

- 固件禁用 C++ 异常，原桌宠和本地 LLM 接口含无条件 throw/catch：改为显式错误检查，异常恢复仅在启用异常的主机编译中保留。
- ARM64 mallinfo 字段为 size_t，官方日志使用 `%d`：板级补丁改为 `%zu` 与显式类型转换。
- 官方提醒回复和技能路径固定缓冲区过小：扩大到可容纳既定输入上限的大小，不关闭编译告警。
- 官方技能摘要直接累加 snprintf 返回值（所需长度，不是实际写入长度），可能越界：按剩余容量检查，截断时停在终止字节位置。
- 固件 libc++ 没有完整 regex/locale 链接实现：桌宠固定表情和动作标签改用有界轻量解析器，保留原始语法和动作白名单；主机异常开/关两种配置及 sanitizer 回归通过。
- 官方‘正在处理’提示也会走 outbound：pet 渠道不发送该提示，避免桥接当作最终回复；后端失败用明确错误标记返回。
- 官方默认启动 Wi-Fi 重连和无认证 WebSocket，不适合当前镜像：复用现有联网服务，等待网络就绪后启动推理，不重复关联，不默认监听 WebSocket。

- 官方 `vela_tls` 原使用 VERIFY_OPTIONAL：板级补丁改为强 CA/主机名验证，缺少 CA 应失败。
- 官方 Media 未启用时存在弱 stub，返回失败而非真实录音/播放；不能把成功链接误判为语音链路成功。
- 官方 iconv 当前拒绝 GB2312 作为目标编码，不能直接用 iconv_open("GB2312", "UTF-8") 完成 TTS 转码。需要单独受测转换器或合适的官方其他实现。
- A7Z Linux DTS 的 UART4 引脚为 PJ24..PJ27；当前 PJ27 被风扇 PWM 使用。UART4 两线/流控实际接法和 pinmux 必须核对，不能盲目占用四线。
- 官方 tap 仅输出完整回复；流式低延迟需要官方引擎扩展，不靠缩小 poll 间隔达成。
- 唯一 chat_id 用于隔离迟到回复，目前会拆分官方会话；多轮桌宠记忆尚未接回。

以上问题都属于未完成项，不应在提交材料或演示说明中写成已全部解决。
