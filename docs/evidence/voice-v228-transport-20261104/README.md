# v228 连续云对话故障：输入证据与分层结论

来源为用户2026-11-04串口附件，不是主机模拟输出。原文件只读保留于本地附件目录；下列哈希用于区分输入，本文只摘录传输及资源字段，不复制对话全文、云签名或凭据。

| 附件目录ID | SHA256 |
|---|---|
| `5b56ac86-6278-4eee-a519-3767fcbd60c2` | `e2d62c28db40e39fe645526367270b8b46de2c8cff5d45e2185331a2ebb2f19b` |
| `520634ed-03d1-42c3-aa55-7f7ae29a28ec` | `16b7603547321e2208e68cac003a7f87cd26fd93d464a52d5dde7fa50b1356da` |

第二份记录确认：第一轮forced-cloud，ASR成功，DeepSeek约1941ms，云TTS两句成功；下一轮新请求升级前约172–275ms出现 `cloud-wss-v227: stage=poll raw=8 errno=11 status=-104 ... rx=0 tx=0`。旧日志的errno可能是EAGAIN遗留值，旧状态把所有poll错误包装为ECONNRESET，不能据此确定远端真的发RST。rx/tx只计TLS解密后的应用字节，不包含握手线缆字节。

资源快照：TCP有1个0a/LAST_ACK（uack1/nrt5）、1个04/ESTABLISHED、3个09/TIME_WAIT；UDP表为空。IOB总128、空闲124、等待0。内存总4179623936、已用2365520、空闲4177258416。PID285/entry 0x402c04dc 对照同版ELF是 `aipet::(anonymous namespace)::consume_live(void*)`，不是已证实的泄漏TTS线程。

因此现有快照**不证明**内存、IOB、UDP连接或线程耗尽；TIME_WAIT本身正常。LAST_ACK可能是未确认的关闭，值得修正协议关闭并观察，但不能直接称为泄漏。长日志一次DNS `raw=1 errno=12`，实际SDK EAI_AGAIN=1、EAI_MEMORY=5，不能只凭errno称为已证实OOM。

分层修复见 [v229候选说明](../../PRODUCT_V3_0PRE_FIX2_V229.md)。修正发现的独立代码缺陷，并增加phase/SO_ERROR/wire字节/端点/拥有计数；尚无v229真实板测，不能将主机50轮测试当作板上根因已确认。保持严格CLOUD，禁止隐式本地回退。
