# v2.1pre／v226：联网优先与可恢复语音服务

2026-11-03，基于v225。预览版，不替代v1.1/v219稳定版；v225镜像、标签和全部历史制品保留，不提交PR或推送。本轮不更改NPU驱动、模型、全局CPU调度、UFS分区或Wi-Fi射频／电源寄存器。

## 实际故障与修正

用户v225日志证明：云ASR/DeepSeek/讯飞TTS均成功过，强制cloud第二轮ASR=-5却直接退出；TTS失败设置30秒冷却后，下一轮把冷却误作不可用并以-101退出。auto虽不退出，却反复冷加载本地ASR。换网络后同一账号仍能成功，旧日志不足以确定每个-5的底层原因。

本版常驻 `aipet run` 下 `mode cloud` 改为**联网优先、恢复失败可回退本地**，不再是遇到云错误即退出；`local`仍严格本地、不上传。独立文本的强制cloud请求仍按原接口明确报错，不冒充成功。

- 链路/服务失败：最多3次尝试（包含第一次），间隔1秒、2秒；非瞬态鉴权、协议、非法参数等不盲目重试。
- 重试会重新调用云API，可能额外消耗ASR/TTS额度；不会因为客户端没听到声音就保证云端没有计费。测试先用免费额度或小预算。
- Wi-Fi：产品检查真实carrier及IPv4，不把残留IP当连接成功。仅掉线时创建独立 `wifi --recover` 任务，使用保存配置、串行化操作，无密码提示；WPA2/受控端口/DHCP均健康就不扫描、不重连。每个Wi-Fi尝试受55秒监控，可协作取消；连接本身保留既有扫描/45秒WPA2-DHCP边界。无USB重置、安全降级或随机改时钟/功耗。**这是恢复机制，不是RF/AP压力验收。**
- 云ASR：最多3次发送同一段保留PCM，均在语义分发前；仍失败则同一PCM在本地识别一次，不反复执行动作。
- 云TTS：最多3次合成尚未播放的句段。整段PCM校验完成才送设备，失败不重播前半句，不重发运动，也不拿本地音频冒充云端结果。LLM/工具请求分发后不自动重放。
- 失败回退本地后保持120秒，避免每句都在云/本地间抖动和重复加载；到期后的下一轮可再次选云，录音中不强行换后端。恢复成功清除失败窗口；本地/云聊天历史仍分离。
- 可恢复的云错误不结束常驻。缺失本地模型时MIC保持OFF、可停止地等待恢复，不谎称可用；NPU所有权异常仍隔离，不能以“联网健壮性”为理由杀线程/重置设备。
- Wi-Fi恢复子任务显式解除从语音父任务继承的SIGINT屏蔽，停止请求只通知本次创建的Wi-Fi任务，不影响其他任务；主机测试实际执行spawn信号属性并验证子任务可接收SIGINT。凭据/Agent仍就绪但carrier已掉线的分支也有独立三次恢复及恢复成功回云测试。
- 讯飞WSS：IPv4连接、20秒socket/TLS请求预算、DNS/TCP/TLS/HTTP升级分段数字错误日志、TLS读写取消检查，EOF单独报告。URL、密钥、签名、音频不写错误日志；官方DNS和其他云SDK内部调用仍受各自平台超时约束，不承诺任意阶段100ms停止。

## 配置和测试

沿用v225接线及凭据格式，未更换讯飞账号/API类型。镜像不包含你的私人凭据；**整卡烧录会覆盖SD上的配置和数据**，必须先备份，烧录后在同一开发板恢复原配置，或重新执行原有setup。凭据备份包含秘密，不提交Git、不公开上传；不要把“兼容旧配置”理解为刷卡自动保留配置。UFS数据不在本轮修改范围。

```text
wifi status
ifconfig
wifi time
aipet start
aipet status
aipet audio providers
aipet audio output i2s
aipet audio volume 50
aipet mode cloud
aipet run
aipet audio service status
```

看到READY MIC ON后测试：连续聊天五轮→“识别前面物体”→“前进三步”→“停止”→“音量五十”。步数×10、300Hz档、会话免反复arm均保留。动作ACK不是实际位移证明。

故障恢复顺序：

1. 正常AP先连续对话；保存 `cloud-recovery-v226`、`cloud-wss-v226`、`cloud-xfyun-v226` 日志，确认不再单轮退出。
2. 断开AP/关闭热点后说话，观察最多3次恢复尝试，不强制杀任务。恢复耗时包含DNS、TCP、Wi-Fi关联和DHCP，不能把重连等待当模型推理。
3. 恢复耗尽后应有明确local路由、准备日志，再MIC ON；本地模型集不完整时应明确MIC OFF并等待，不伪报READY。
4. 恢复AP；本地保持窗口到期后的下一轮应回云，不中断已经开始的本地录音。
5. 说一次动作命令，故意在确认播报时断网；检查STM32计数只增加一次，已播片段不从头重播。
6. 在重试/本地等待阶段执行 `aipet stop`，检查active最终变0；随后重新run。不要强杀NPU任务。

```text
aipet audio service status
aipet mcu status
wifi status
aipet stop
aipet audio service status
```

日志里的error是最近一次失败信息，不等于服务已经退出；必须同时看active/stage。普通ping路由器成功也不代表外网DNS、云TCP、TLS都正常。

版本以 `aipet status` 产品横幅和 `voice-service-v2.1pre(v226)` 为准。部分未改的功能日志仍带v219/v225标签，保留其来源；新增恢复日志为 `cloud-recovery-v226`／`wifi-recovery-v226`，不要仅凭继承日志标签判断刷入了旧固件。

自启动沿用 `service wifi on`、`service ssh on`、`service agent on`、`service ufs on` 和 `aipet autostart on`，默认监听策略不偷偷更改。初次完整配置见[v225说明](PRODUCT_V2_0PRE_V225.md)；本版boot也采用恢复/回退策略，欢迎语失败不从头重复播放。

## 验证和交付边界

构建入口 `tools/build-a733-product-v21pre-v226.sh`；门禁 `tools/test-product-gates-v226.py` 在36项基础上加入4项（实际常驻恢复、Wi-Fi健康判定、本机真实TLS/WebSocket及云协议），合计40项；完整链接检查和镜像封装均为独立v226路径。主机测试中的网络/MIC/LLM/UART替身明确标记，不代替实机。

新增真实云账号连续多轮、AP断电/重连、机械/声学及首音延迟均需上板测试；不能预先称为稳定版或保证3–5秒。

## 本地交付与恢复点

- 镜像：仓库外同级 `openvela-a733-cubie-a7z-sd-v2.1pre-v226-candidate.img`，2,147,483,648字节；旁有`.sha256`。
- 镜像SHA256：`63aa91f38a6223e0b3ae0d549f3be17020916824aa5273c15906697188799683`。
- 内核SHA256：`cff8aa15e8e0c243a78970f416f03be2ead9ad053da4a2ee0a1c1b3038c6eb30`。
- 固件冻结源码：`935a7b10c301f52b00f190d518751576def8583c`；分支 `preview/v2.1pre-v226`。
- 最终恢复标签：`a733-v2.1pre-v226-image-20261103`；bundle：`out/checkpoints/a733-v21pre-v226-image-20261103.bundle`。初始a17源码标签保留为过程记录，不冒充最终固件。
- 实际构建：`out/product-v21pre-v226-build-r2`，SDK316项按字节恢复；40门禁：`out/v226-product-gates-r1`；最终完整审计：`out/v226-full-link-r4`。
- 封存固件：`out/v226-product-v21pre-release-r1`；镜像回读：`out/product-v21pre-v226-package-r1`；[证据与交付边界](evidence/product-v21pre-v226-image/README.md)。Windows独立哈希与Linux封装结果一致。

首次封存仅在复制继承日志时因错误的v226文件名中断；修正为真实的v225继承日志后，通过`--resume-verified-package`重新验证并补齐证据，没有重建或重写镜像。失败日志保留，不作为通过证据。v219/v220/v221/v223/v224/v225六个旧镜像哈希保持，稳定指针未变；没有物理盘/UFS写入、PR或推送。
