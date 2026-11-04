# v218 多服务商云语音扩展

2026-10-31；基线为封存的 v1.0／v217-audit2。本扩展是**软件候选**，不是云账号或整机验收结论。不覆盖旧镜像，不提交 PR，不推送远端。

## 支持范围

| 服务 | ASR | TTS | 方式及限制 |
| --- | --- | --- | --- |
| 火山引擎 | 保留官方接口 | 保留官方接口 | 默认项；独立语音凭据，与 LLM 分离 |
| 讯飞开放平台 | IAT v2 | 在线 TTS v2 | 板端原生 WSS；APPID＋APIKey＋APISecret |
| 腾讯云 | SentenceRecognition | TextToVoice | 板端原生 HTTPS／TC3；SecretId＋SecretKey |
| Microsoft Edge TTS | **无** | 实验选项 | **需要电脑／服务器 HTTPS 网关**运行 edge-tts＋FFmpeg，不是板端原生 Edge |

ASR、TTS 独立选择，可混用。DeepSeek／MiMo 仍只负责 LLM，不能把其密钥用作语音密钥。
本轮没有新增服务的真实账号调用、扣费调用或板上声学验收。缺少凭据时继续本地模式即可，不需要现在提供密钥。

Edge 第三方库通常无需 API key，但这不代表微软承诺永久免费、商业授权或长期稳定。它不是 Azure Speech 正式开发接口；正式产品须另行核对条款。讯飞／腾讯／火山试用额度及计费由账号决定，不自动申请试用或切换计费账号。

## 配置与测试命令

仅适用于包含本扩展的新固件。停止常驻服务并等待 `active=0` 后配置；服务运行时拒绝修改，避免同一轮切换服务商。

```text
aipet audio service stop
aipet audio service status
aipet audio providers
```

只配置自己已有账号的服务商：

配置入口沿用现有 Agent-ready 门禁；先通过 `aipet start`／`aipet status` 确认官方引擎已就绪。该启动流程需要原有 LLM 配置、网络、时钟和 CA；这不表示必须购买火山语音，也不表示可以复用 LLM 密钥。

```text
aipet audio setup volc
aipet audio setup xfyun
aipet audio setup tencent
```

不必全部执行。旧 `aipet audio setup` 仍等于火山配置；配置和选择分开，保存凭据不会开始录音／联网／改变产品模式。输入隐藏密钥，Enter 保留，Ctrl+C 取消；成功后一次保存。讯飞默认普通话／xiaoyan，腾讯默认 16k_zh／VoiceType=1001；自定义音色必须有账号权限。

例如“腾讯识别＋讯飞合成”：

```text
aipet audio providers asr tencent
aipet audio providers tts xfyun
aipet audio providers
```

支持 `asr=volc|xfyun|tencent`、`tts=volc|xfyun|tencent|edge`。选择 Edge ASR 会被拒绝。
`credentials=present` 只表示配置存在可读，不是网络／权限／额度／声音已通过。

账号准备好之后再执行以下会上传文字／录音、可能计费的显式测试：

```text
aipet audio output i2s
aipet audio volume 50
aipet audio cloud-speak "你好，中文和 English 朗读测试。"
aipet audio asr 5000
```

`cloud-speak` 始终使用所选云 TTS，即使本地声学模型已加载。普通 `audio speak` 为兼容旧用法仍可能使用本地 TTS。`audio asr` 只识别打印，不执行电机。
随后可在云 LLM、网络、时钟和 CA 均已配置时测试常驻在线链路：

```text
aipet mode cloud
aipet audio service start
aipet audio service status
```

没有语音凭据时在录音前明确失败。恢复本地时先停止并等待 `active=0`，再执行 `aipet mode local`、`aipet audio service start`。也可停止后设 `aipet mode auto`，沿用每轮冻结整条路由、仅语义分发前安全回退、不重放动作／已播报内容的策略。

## Edge 可选电脑网关

`tools/cloud-voice/edge_gateway.py` 不在 NuttX 运行。它需要额外常在线电脑；不想依赖电脑请选择原生火山／讯飞／腾讯。
可在隔离 Python 环境显式安装 `tools/cloud-voice/requirements-edge.txt`，另行安装 FFmpeg；脚本不自动安装、下载或试音。
准备 TLS 服务端证书和私钥，证书 SAN 必须包含开发板使用的主机名／地址。把其 CA **合并到**板上 `/data/ai_agent/ca.pem`，保留原有根证书，不覆盖整个 CA 包，不关闭证书验证，不把私钥放到板上或 Git。

主机环境设置随机至少 32 字符的 `AIPET_EDGE_GATEWAY_TOKEN`，避免写入命令历史／日志，再显式启动：

```text
python tools/cloud-voice/edge_gateway.py --bind <电脑局域网地址> --port 8443 --cert <server.pem> --key <server-key.pem>
```

默认只绑定 127.0.0.1；必须显式填 LAN 地址才能供板端使用。仅开放所需局域网访问，不直接暴露公网。
网关采用 HTTPS＋Bearer 验证、单请求处理、输入／音频上限、超时及 no-store，不记录文字／音频／令牌，不写音频文件。本轮不安装可选依赖，不启动长期服务；真实 Edge 合成、FFmpeg 解码和听感仍待验收。

```text
aipet audio setup edge
aipet audio providers tts edge
aipet audio providers asr xfyun
aipet audio cloud-speak "你好，这是 Edge 网关测试。"
```

主机名不带 `https://`、端口或路径，端口单独填 8443；Token 隐藏输入；默认音色 `zh-CN-XiaoxiaoNeural` 可更换。ASR 仍需另一家账号。

## 数据、安全和性能边界

- 云语音合同统一为 16000 Hz／单声道／S16LE PCM；现有板上 48k I2S 桥接不变。
- ASR 最多 256000 字节／8 秒；常驻产品先完成端点录音再调用云端。讯飞按 40ms 帧上传并交错收包，但**不是边录边上传**；腾讯是短音频接口，不是实时流式 API。
- TTS 每完整短语最多 10 秒／320000 字节；超长、错误、缺少终包、无效 UTF-8／Base64 不发布残缺音频。腾讯本版保守限制每短语 150 个 Unicode 字符，不截断单词。
- 讯飞收齐 `data.status=2` 才提交结果；ASR 按 `sn` 组装并校验可选 `rpl/rg`；TTS 当前完整短语接收后播放，不把流式收包宣称为实时首包播放。
- WSS 单 TLS 所有者，验证 CA／主机名／Upgrade Accept，处理分片／Ping、掩码和长度；客户端掩码来自安全随机源。
- WSS socket／TLS 循环共享 60 秒单调时钟截止时间，DNS 仍用平台解析器超时。腾讯／Edge HTTPS 沿用项目受控 TLS。停止是协作式，网络请求中可能等待请求返回／超时，不承诺立即停止。
- 密钥使用现有板载文件密钥＋AES-GCM，不是硬件安全元件；丢失 `device.key` 无法解密旧凭据，公开提交不得包含它或私有配置。
- 单字段上限 255 字节；腾讯可选 STS token 超长会报错，不截断，不自动续期；输入 `-` 可清除旧 token。长期专用最小权限 SecretId/SecretKey 不需 STS token。
- 不改全系统 CPU 调度、NPU DMA/MMU/ABI、摄像头硬件、UFS 初始化或电机 arm 机制。
- **没有新的整机首音测量，不能声称已达 3–5 秒。**

## 实现、构建及验证

应用源码位于 overlay 的 `apps/system/aipet/`：`cloud_voice.h/.cxx` 负责选择和适配，`cloud_voice_protocol.*` 负责签名／完整结果校验，`cloud_voice_socket.*` 负责 WSS；`agent_setup.c` 负责配置；`product_cloud_config.c` 为 C-only 配置桥；手动和常驻链路都通过这些接口调用。

```sh
bash tools/build-a733-cloud-v218.sh out/cloud-voice-v218-build-user1 out/ort-static-v185-r2
python3 tools/test-cloud-voice-v218.py --output out/cloud-voice-v218-host-user1
python3 tools/test-edge-gateway-v218.py --output out/cloud-voice-v218-edge-user1
python3 tools/check-cloud-arm-v218.py --build ../../quickly-openvela/cmake_out/cubie-a7z_nsh_v218_cloud_voice --output out/cloud-voice-v218-arm-user1
bash tools/test-aipet-setup.sh
bash tools/test-aipet-audio.sh
python3 tools/test-product-service-v1.py
bash tools/test-product-v1.sh
```

构建缓存为独立 `cubie-a7z_nsh_v218_cloud_voice`；测试输出目录必须是新的。
签名测试用真实 mbedTLS 与独立 Python hashlib/HMAC 对照；提供商测试的 HTTPS/WSS 响应是明确 MOCK，不是账号验收。
`test-cloud-socket-v218.py` 可链接本机 mbedTLS，在 localhost 验证真实 TLS／错误证书拒绝／帧分片／Ping／掩码／超限，也不是云厂商或板测。

### 本轮已完成的软件证据

运行时代码恢复点为 `28929a2999586b621230729b74a205c132294ecb`。
独立 v218 完整固件构建、13 个实际 NuttX/AArch64 编译单元、ELF／ABI／ISA／内存布局检查、既有音频／产品服务／摄像头回归均通过；SDK 暂存的 315 个路径已按字节恢复。
签名和提供商模拟测试、本机真实 TLS/WebSocket 及 HTTPS 网关处理测试通过；网关上游是 MOCK，没有调用真实 Edge 或验证 MP3 解码。
报告与回归日志封存在 [cloud-voice-v218](evidence/cloud-voice-v218/README.md)，不依赖保留整个 `out/` 才能查看结论。

候选产物为 SDK `cmake_out/cubie-a7z_nsh_v218_cloud_voice/nuttx.bin`，大小 7,234,408 字节；SHA256 为 `c4cca0df64af446e51abc82c950284221d26008d7af2d4d84c9d4282bb0726be`。
**本轮没有生成新 SD 烧录镜像，也没有进行真实云账号、板上声音或整机延迟验收。** 不能直接把这个候选状态当成旧 v217 镜像已经支持新增命令。

## 外部接口依据

核对于 2026-10-31：讯飞 [IAT v2](https://www.xfyun.cn/doc/asr/voicedictation/API.html)、[TTS v2](https://www.xfyun.cn/doc/tts/online_tts/API.html)；腾讯 [SentenceRecognition](https://cloud.tencent.com/document/api/1093/35646)、[TextToVoice](https://cloud.tencent.com/document/api/1073/37995)、[TC3](https://cloud.tencent.com/document/api/1093/35641)；[第三方 Edge 项目](https://github.com/rany2/edge-tts)与[微软正式 TTS REST](https://learn.microsoft.com/azure/ai-services/speech-service/rest-text-to-speech)。服务文档、账号权限和条款可能变化。
