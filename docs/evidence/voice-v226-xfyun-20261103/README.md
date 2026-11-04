# v226：讯飞 ASR 成功、TTS 超时的用户板测反馈

来源：2026-11-03 用户本轮串口日志；不是代理本轮 SSH 复现。以下不保存 APIKey、APISecret、密码或完整鉴权 URL。

观察到：

- FCU760K `wlan0 RUNNING`，DHCP 私网地址已分配，系统时间2026-11-03 UTC。
- SSH 服务 listening；官方 Agent ready；Xfyun ASR/TTS 均为 credentials=present。后者只说明已配置，不代表服务验收。
- 两次 `aipet audio cloud-speak "你好，我是桌宠Vela。"`，各三次 TTS 调用均 `-110`；`clauses=0/1`，没有音频成功播放。
- `ping baidu.com`：4发4收，0%丢包，往返57–66ms。ICMP成功不能证明另一个域名的 WSS 连接、鉴权或音频接收正常。
- `aipet audio asr 5000`：16000Hz mono S16，160000字节，provider=xfyun status=0，返回“你好，你是谁？我是谁。”。

不能据此确定：TTS 的具体超时阶段、对应服务权限、证书/网络路径或账号根因；不能将本轮 ASR 成功当成 TTS 或整机云对话通过。

v227 根据此证据加入合并 WS/TLS 小写入、TCP_NODELAY、连接/服务/空闲分离预算及不泄密的阶段日志，并增加真实 localhost TLS 回归。真实云账号声学确认仍待新镜像；详见 [v3.0pre 测试说明](../../PRODUCT_V3_0PRE_V227.md)。
