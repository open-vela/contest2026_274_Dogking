# v3.0pre-fix2 / v229：云语音连接生命周期修复候选

2026-11-04。稳定版仍为 v1.1/v219，v228 镜像及恢复点保留。不推送、不创建 PR，不写物理 SD/UFS。

已封装独立2GiB镜像，SHA256 `5d8ae2fe97176b05a411f0e38a5ff5db5e0a6c519a47f07207a35f2d53e400f1`。44项继承/新增门禁、完整链接审计、SDK320项还原、镜像回读及Windows独立哈希通过；[发布与恢复证据](evidence/product-v30prefix2-v229-image/README.md)。新版真实多轮板测仍待完成，不晋升稳定。

## 现场证据与结论边界

v228 第一轮云 ASR、DeepSeek、讯飞两句 TTS 成功，下一轮新连接在约 172–275ms 出现 `poll raw=8 status=-104`，尚未出现 UPGRADED。原 `rx=0 tx=0` 只计 TLS 解密后的应用数据，不能解释为握手没有网络收发。新日志分别记录应用字节和 TLS 底层字节。

用户补充快照有 3 个 TIME_WAIT、1 个 LAST_ACK、1 个 ESTABLISHED；UDP 表为空，IOB 128 个中空闲 124，内存仍充裕。当前证据不支持“已经证明资源耗尽”“已证明 DNS 泄漏触发”或“CPU/NPU调度造成”。DeepSeek仍成功也不能证明讯飞端点及其 TCP/TLS 路径一定正常。

以下修正针对独立审查确认的软件缺陷。**哪一项触发板上第二轮故障仍未证实，新候选必须连续多轮板测。**

## 修复内容

1. socket()/fcntl()/connect() 保留实际错误，失败不再可能把零状态与无效句柄继续交给 TLS。poll 错误读取 SO_ERROR，保留 ECONNREFUSED 等真实原因，不把所有事件改写成 ECONNRESET；输出当时的 TCP/TLS/HTTP/服务阶段、远端IP、本地端口、fd及拥有的WSS连接数，不输出签名URL、密钥或请求正文。
2. DNS 最多保留4个 IPv4 地址，依序尝试 TCP **以及 TLS**，总建立预算仍20秒，每地址有界；失败连接完整释放 SSL/config/fd 后才尝试下一地址。证书或主机名不可信、取消、进程资源不足即停止；始终使用原主机名的 SNI/证书校验，不固定云 IP、不关闭认证。仅握手前重选端点，不重放 ASR/LLM/动作语义。
3. 已升级的连接完成有界 WebSocket 正常关闭和 TLS close_notify，总关闭预算500ms，poll等待裁剪到剩余预算（实际返回仍受调度影响）。坏连接和取消不无限等待，fd仅关闭一次；成功音频不因关闭阶段失败而重播。失败构造及析构也释放自己的fd，计数归还；不盲目重试close(EINTR)，不误关复用fd。
4. 修正 SDK DNS 中 IPv4/IPv6 两条“截断 UDP → TCP”路径：跳转前先关闭旧UDP fd。修改通过项目工具临时暂存，构建结束逐字节恢复 SDK；不是手改上游。此缺陷在可控模拟中复现，但现场UDP空表，不能认定是此次板故障根因。
5. TLS BIO 层记录真实网络字节并刷新空闲进度，避免一个 TLS record 尚未完整读出时把持续收包误算为完全空闲。硬总时限仍有效。

保留 v228 严格 CLOUD：网络/服务故障绝不转本地，不加载本地语音模型。AUTO 才允许回退。本地推理数学、NPU/I2S/摄像头驱动、全局调度、运动授权均不改。

## 软件测试与构建

新增测试直接编译实际 cloud_voice_socket.cxx，使用真实 mbedTLS+本地认证 TLS 服务，不调用付费 API；模拟响应不是 ASR/TTS 模型验收：

- 同一进程50轮，每轮1条ASR式连接+2条TTS式连接，共150连接；主线程与合成工作线程各自独占连接，全部关闭后fd数无增长，ASan/UBSan检查。
- 真实连接拒绝保留 SO_ERROR=ECONNREFUSED、注入 socket EMFILE、取消、正常WS/TLS关闭、对方不回复关闭的500ms预算。
- 首地址TCP成功而TLS被重置后，在相同SNI与认证策略下切换第二个DNS地址成功。
- 从实际SDK抽取DNS callback，原版8个泄漏复现；修正版成功/失败各100次TCP回退、opens==closes，测试不修改SDK。
- 全部42项继承门禁继续执行，新版合计44项；完整ARM链接、ABI/ISA/摄像头审计、临时SDK320项还原、镜像文件回读和Windows独立哈希以最终封存报告为准。未封存前不把进行中当成通过。

构建：`bash tools/build-a733-product-v30prefix2-v229.sh out/product-v30prefix2-v229-build-r1 out/ort-static-v185-r2`。门禁：`tools/test-product-gates-v229.py`，全链接：`tools/check-product-full-v229.py`，封存：`tools/seal-product-v30prefix2-v229.py`。固件源码冻结提交、制品哈希和已运行结果见最终发布证据。

## 板上最小复测顺序

当前故障版若stop不能退出，不强杀音频/NPU所有者；先保存串口日志再正常重启。升级需确认启动的确是 v229；整卡烧录覆盖目标卡配置，先备份。已用v228且能可靠编辑SD第3分区ext4时，可在关机后备份原 `/boot/openvela/a733/Image` 再仅替换封存的同名内核，保留SD第4分区配置；不要改DTB/UFS。不得把Image放到FAT `/data`就当成升级。

```text
wifi time
aipet status
aipet audio providers
aipet audio output i2s
aipet mode cloud
aipet mode
aipet audio cloud-speak "你好，这是联网连接测试。"
aipet run
aipet audio service status
```

凭据已配置则不要重输或公开密钥。`cloud-voice-v218`、`cloud-wss-v227` 等是沿用的子模块标签，不单凭它们判断系统版本；以服务状态的 `v3.0pre-fix2(v229)` 和新增 `cloud-transport-v229` 为准。等待READY后连续说10轮不同短句，逐轮等播报结束再说；包括两句长回答、识物、调音量。每轮确认仍是forced-cloud，不应有本地模型预加载。保留从第一轮开始的完整串口日志，新 `cloud-transport-v229` 应有 TCP-ready/TLS-ready/closed；所有拥有者结束后 owned-WSS归零。并行预取时暂时为1或2是允许的，不能只截某一瞬间判断泄漏。

```text
aipet stop
aipet audio service status
ps
cat /proc/net/tcp
cat /proc/net/udp
cat /proc/iobinfo
free
```

等待active=0，再run测试5轮。TIME_WAIT正常、LAST_ACK不等于已证明泄漏。若仍失败，新的phase、SO_ERROR、wire-rx/wire-tx、peer/local-port能区分DNS、建连、TLS握手、HTTP升级或服务读写；不再重复笼统归因“网络波动”。没有真实云账号/板上连续测试前，本版不晋升稳定，不承诺问题已在硬件上消失。
