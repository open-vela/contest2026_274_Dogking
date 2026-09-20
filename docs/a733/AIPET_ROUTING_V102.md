# v102：接通 Linux vision-4 的完整文字路由

## 已验证基线

用户 v101 实机：`aipet init` 成功，官方 Agent ready，连续两次中文对话成功，首次 7490 ms、第二次故事 1829 ms。继续沿用此初始化、加密和自启动实现。

本轮以只读原开发目录 `D:/My-Program/AI-Agent-Program/cc-program/Radxa_A7Z_Sencond_Pro/Now-Program/vision-4` 的 `src/routing.py`、`src/rule_engine.py` 及配置中的 **rules 部分** 为依据。没有改动该目录，也没有复制其中的私人凭证。旧 goal 原型不是此次路由基准。

## 决策顺序

1. 无内容/全空白 → “嗯？我没听清，再说一次吧~”。
2. 按配置顺序匹配直接规则 → 随机选择回复，展开时间/日期/星期模板，不调用任何 LLM。
3. 非直接请求 → 联网时全部走官方 Agent 的云端入口，包括原配置中的笑话/自我介绍关键词。
4. 无 Wi-Fi IPv4 → 本地接口。
5. 云端返回失败 → 本地接口，不重复发送收费请求。
6. 完整回复统一做文字/表情/动作标签解析，输出路由、降级标记、耗时。

“联网”当前使用已有 wlan0 IPv4 状态作低开销判断，不能把它当成互联网可达性证明。DNS/TLS/API 故障由实际云端请求诊断后降级；不在每次提问前增加 ping 或 HTTPS 探测。旧移植核心曾将命中本地关键词的在线请求送本地，本轮修正为最新版 vision-4 行为。

默认直接规则覆盖时间、问候、告别、感谢、天气，保持原配置顺序及回复。原版天气规则本来就是“暂时查不到天气”的直接回复，不冒称查询成功。英语关键词保持原版子串匹配（如 hi），不是另加 NLP 意图识别；Unicode 大小写转换尚不是完整 Python lower 等价实现。

## 使用和测试

```sh
aipet status
aipet route "你好"
aipet ask "你好"
aipet ask "现在几点了"
aipet route "给我讲个笑话"
aipet ask "给我讲个笑话"
aipet ask "给我讲个短故事"
aipet cloud "请查询天气"
```

- `ask`：执行完整路由。
- `route`：只检查决策，不提交云端消息、不运行本地模型；直接规则会展示预选回复，云端/本地路由展示检查提示。
- `cloud`：忽略直接/本地关键词，尝试官方 Agent 云端入口；仍需要初始化就绪及有效网络，也不保证工具服务已配置。
- `cancel`：保持既有取消接口；本轮没有宣称完成所有强制信号退出与官方后端中断场景的全面审计。

输出示例：

```text
你好呀！今天想聊什么？
[emotion=普通 actions=0]
[route=local_rule fallback=0 elapsed=0 ms]
```

直接规则可在 Agent 尚未 ready、断网时使用。检查联网笑话应选择 cloud；断网普通请求应选择 local_llm。

## 可修改规则

可选文件 `/data/ai_agent/config/routes.json`，未创建时使用内置原版规则。每次命令读取，因此改动无需重启。样例在仓库 `assets/aipet-online/routes.example.json`。只传这个无凭证样例，不要传整份 Linux 私人 user_config.json。

例如先上传 FTP 根目录，再执行：

```sh
cp /data/models/routes.example.json /data/ai_agent/config/routes.json
```

JSON 顶层为 `direct_rules` 与 `local_llm_rules`，对象顺序影响优先级。下划线开头注释项跳过；每条规则有 keywords 数组，直接规则另有 actions 回复数组。保留 `{time}`、`{date}`、`{weekday}` 模板。删除自定义文件恢复内置默认（先按需备份）。

文件最大 16 KiB，每组最多 64 条、每数组最多 32 项、每项最多 512 字节；非法结构/编码不会覆盖运行规则，存在但内容错误的文件会阻止请求并明确报错。配置载入使用堆缓冲，避免把 16 KiB 缓冲压入 16 KiB 应用栈。

## 暂停与真实接口边界

本地 LLM 仍按用户之前要求暂停，不自动装载 1.5B 模型。接口 `aipet::set_local_route_backend` 允许后续模块注册进程生命周期有效、线程安全的生成回调。未注册时返回 ENOSYS，并明确显示“本地 LLM 暂停”，不会拿预设句子冒充本地推理。测试中的回调只是主机 mock，不是已启用的板端模型。

本轮完成的是 **文字输入到决策、官方云端执行/本地预留接口、统一回复解析和终端输出**。USB 麦克风、在线 ASR、UART4 TTS、显示和执行器输出不因此自动完成；动作仍仅解析，不操作硬件。原版完整语音/具身流水线仍需继续逐项接入和板测。

既有桥接层按请求生成不同 chat_id，防止迟到回复被下一次请求接收；当前并未把这些独立请求合并为同一持久会话。因此不能宣称跨命令追问已保留上一轮上下文。后续需分离响应关联标识与官方会话历史标识，并验证取消/超时后迟到响应隔离。本轮不以改变关联机制来换取未经审计的连续记忆。

默认不输出输入原文或凭证调试 dump。后台路由阶段诊断仍由既有 `pet_debug.hxx` 的构建开关控制；终端路由/耗时摘要用于本阶段验收，未声称已经隐藏最终产品所有诊断信息。

## 文件和测试

- `pet_routes.cxx/.hxx`：默认规则、边界校验、JSON 载入、模板、板端 Ports、CLI 路由执行、本地接口。
- `pet_core.cxx`：最新版在线路由优先行为。
- `aipet_main.cxx`：ask/route/cloud 命令封装。
- `tools/test-aipet-routes.sh`：ASAN/UBSAN 规则加载、模板、原始顺序、失败不覆盖、直接规则、云端失败、本地回调边界；core parser/routing/fallback/state 回归。
- 既有 Agent 桥接、首次初始化、密文、防篡改、HTTP framing 测试继续运行。

Git 提交与构建前 bundle 已保存；镜像/内核在仓库外的 A733-A7Z-ALL-Files 中，不提交私人配置。最终构建与镜像哈希见本文件后续记录；未经过用户板测前只能称候选镜像。

## 最终产物

- 最终源码提交：路由实现 `35518b8`（文档随后单独提交）。
- 构建日志：`../a733-online-agent-v102-build.log`，exit=0。
- 内核/ELF/System.map：`../a733-online-agent-v102-kernel/`。
- 内核 SHA256：`414837c5ab51c00d001ca993bbe914da31824e0dc4cf6bbf2035db70db0cef2b`。
- 镜像：`../openvela-a733-cubie-a7z-sd-online-agent-v102-candidate.img`，2147483648 字节。
- **最终**镜像 SHA256：`598b8436c68f8ea7de28859e41174908971bc6eca4860f4f6b29b2a01c2edaab`。以此为准，不使用打包日志中加入 CA/SOUL 之前的中间哈希。
- 打包日志：`../a733-online-agent-v102-image.log`，exit=0；GPT/ext4、嵌入内核和 CA 读回校验通过；Windows 独立哈希一致。
- 恢复 bundle：`../a733-online-agent-v102-final.bundle`。

官方工作区临时 aipet 目录已移除；与 v100 恢复基线比对的 41 个文件哈希一致。源 Linux 开发目录未修改。本轮主机测试全部通过，但 v102 尚待实机验收。整卡烧录前务必备份数据分区，尤其私人配置和对应 device.key；烧录后先联网、执行 `aipet init`，再做本文件中的路由测试。
