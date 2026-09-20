# v100 联网后自动初始化候选

## 实现范围

沿用官方 AI Agent 的推理、工具、消息总线及 TLS 连接池，新增板级启动适配，不自行替换引擎。

`service --boot` 同时启动已有 Wi-Fi 自动连接和新的 `aipet --boot` 后台等待任务，不阻塞 NSH、SSH 或 FTP。Agent 开机自启动默认开启；没有私人配置时只等待，不发送云端请求。等待周期从 1 秒递增到最多 8 秒，条件变化时才打印日志。

等待条件：wlan0 具有 IPv4 地址；板端 `/data/ai_agent/config/config.json` 可读取、为不超过 4096 字节的 JSON 且含非空非 REPLACE 占位符 api_key；CA 文件可读；系统时间不早于 2025-01-01。日期检查只是时钟合理性门槛，不是 NTP 已认证或准确的证明。HTTPS 仍执行严格证书及主机名校验；应查看 `wifi time` 和 `ntpcstatus`。

条件满足后只发起一次官方 `ai_agent` 启动。自动和手动 `ai_agent &` 经过同一个互斥所有权门，不重复初始化总线或线程。`ready` 仅在桌宠消息通道成功 attach 后显示，官方启动日志中的通用 ready 不替代这个判断。

首次请求仍按需建立 DNS/TCP/TLS 连接，无静默付费模型预热，也未新增 TLS 预连接。因此本功能将本地初始化放到用户请求之前，不保证消除 v99 首次约 7 秒的 TLS 握手或云端生成时间。连接池仍用于后续请求加速。

## 使用

```text
aipet status
service agent status
service agent on
service agent off
```

开关保存在板端 `/data/system/agent.auto`，作用于下次开机，不停止当前 Agent 或当前等待任务。缺失时默认 on；损坏的开关记录不会默认启用。

关闭自启动后，仍可手动启动后台初始化器：

```text
aipet start
aipet status
```

正常 `ready` 后使用：

```text
aipet ask "请只回答：启动测试成功"
```

## 板测步骤

1. 备份私人配置并烧录新候选。镜像不含 API Key，旧 v99 保留。
2. 未联网时输入 `aipet status`，应显示等待 network，串口仍正常响应。
3. `wifi connect <SSID>`，检查 DHCP；没有私人配置时应等待 private-config，不产生模型请求。
4. 上传私人 config.json 到 `/data/models`，复制到 `/data/ai_agent/config/config.json`。不需要手动执行 ai_agent。
5. 检查时间和 CA；最长一个等待周期后查询 `aipet status`，应由 starting 变为 ready。执行一次 ask 并保留日志。
6. 再执行 `ai_agent &` 和 `aipet start`，`ps` 中应只有一个实际引擎及其线程组，日志应拒绝重复引擎初始化。
7. 关闭自启动并重启：`service agent off`、`reboot`。`aipet status` 应 not-started；`aipet start` 可重新触发等待。恢复 `service agent on` 后再次重启检查自动启动。
8. 保存 Wi-Fi 自动连接配置后重启测试：网络、时间、私人配置就绪后无需手工启动 Agent。首次模型请求和后续请求分别计时。

## 限制与恢复

- 不提供未经审计的热停止/自动重新启动引擎。失败或退出后保持 failed/stopped，需保存日志再重启系统。强制 kill 可能留下 starting 状态，也应重启，不能绕过所有权门重新初始化共享状态。
- 配置变化需重启系统使官方引擎重新加载。`aipet start` 不是配置热重载命令。
- 不代表 USB 麦克风、UART TTS、I2S、多轮记忆或网络长期稳定性已验收。
- 所有新增源码保存在仓库 overlay，官方 main 的单实例入口通过 `patches/ai-agent-single-instance.patch` 临时应用，构建后恢复。
- 构建前已保存 v100 Git bundle 快照。主机 ASan/UBSan 测试覆盖 8 线程并发所有权竞争、ready/failed 状态及原有消息边界；这不等于板测通过。

## 构建错误记录

首轮新增启动模块编译失败：`fatal error: cJSON.h: No such file or directory`。原因是应用目标没有继承官方 Agent 的私有 cJSON include 路径；修复为在 aipet 的 CMakeLists 中显式增加 `${NUTTX_APPS_DIR}/netutils/cjson/cJSON`，重新配置并构建。首轮日志保留于 `A733-A7Z-ALL-Files/a733-online-agent-v100-build.log`，重构建日志为 `a733-online-agent-v100-rebuild.log`。

## 最终产物与验证

重构建及打包退出码均为 0。新镜像位于 `A733-A7Z-ALL-Files/openvela-a733-cubie-a7z-sd-online-agent-v100-candidate.img`，大小 2,147,483,648 字节，最终 SHA256：`a41e36567384b03774659d8a9bd45986abd56e48cefbf9a413d43e9f464b7380`。

内核位于 `A733-A7Z-ALL-Files/a733-online-agent-v100-kernel/nuttx.bin`，1,891,608 字节，SHA256：`426b1f435091862094788b898476b29d53ee64ab7b1feb9456e50c5c97fdda8d`，同目录保留 ELF 和 System.map。启动标识 `41 52 4d 64` 正确，符号包含 aipet_startup、aipet_agent_claim、aipet_main、ai_agent_main。

ext4、GPT、内核和 CA 回读校验通过；既有分区 4 末尾未按 2048 扇区对齐提示不代表 GPT 校验错误。官方 Agent 的 7 个临时修改文件、服务管理器的 2 个暂存文件均与构建前备份哈希一致，官方 aipet 暂存目录已撤除。

最终源码恢复快照：`A733-A7Z-ALL-Files/a733-online-agent-v100-final.bundle`。镜像不含私人 Key，v99 未覆盖。自动初始化的运行行为仍等待上文实机验收，不能用构建成功替代板测。
