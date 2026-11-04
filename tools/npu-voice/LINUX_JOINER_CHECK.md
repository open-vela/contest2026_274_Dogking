# Linux 第二阶段：joiner 输入敏感性

本包承接 2026-10-19 的 Linux 首测，不修改 openvela 固件、SD 卡或系统驱动。
厂商模型与库仅复制到本机私有测试包，不加入 Git，也不授权再次分发。

## 已确认的第一阶段

回传 `voice-report-tCO4OwuR.tar.gz`，SHA-256：
`dd481ad1387fbe167aec984d35bede9e89135085f57e0751be0d278f9d4f9e82`。
Debian 11/aarch64，Linux `5.15.147-21-a733`；运行库报告
`2.0.3.2-AW-2024-08-30`，内核启动日志驱动报告 `2.0.3.0-AW-2024-05-29`。
两者分别记录，不认定为同一版本。7 个资产校验通过，4 模型接口查询通过。
joiner 零输入两次为 253/269 us，每次 1052 字节（263 个有效 FP32 数值），
字节完全一致，SHA-256：
`b2a50b97a684a67b8957eb54674aea44f5a9d395101d7c175654f9316161fff3`。
这是小型 joiner 的运行调用时间，不是 ASR 速度或暖态性能统计。
第一阶段没有执行 encoder、decoder、vocoder，也没有真实语音识别。

## 执行

另用官方 Linux 卡，关闭所有其他 NPU 程序，解压新包进入其目录：

```sh
sudo bash test-linux-joiner.sh --run --npu-idle-confirmed
```

也可只采环境（不调用 NPU）：

```sh
bash test-linux-joiner.sh --env-only
```

要求 `python3`（仅标准库），其余依赖及超时/归档策略同
[第一阶段说明](LINUX_BOARD_CHECK.md)。不安装软件、不联网、不改设备权限、不自动复位或重启。
脚本共 7 次厂商进程调用：一次 joiner 接口查询，6 次推理。
没有向扬声器播放测试结果，没有保存或上传麦克风数据。

## 测试内容

原生接口必须精确为：2 输入/1 输出，FP32、无量化，输入各 `320x1`/1280 字节，
输出 `263x1`/1052 字节。拒绝维度顺序、类型、长度、清理状态或计数不匹配。
使用现有已通过的二进制和固定模型，不重新编译/替换厂商运行库。

| 顺序 | 第一路（encoder 向量） | 第二路（decoder 向量） | 检查 |
|---|---|---|---|
| zero-a | 全零 | 全零 | 与首次板测 SHA-256 一致 |
| encoder-a | 有界梯度 A | 全零 | 相对 zero-a 改变 |
| decoder-a | 全零 | 有界梯度 B | 相对 zero-a 改变 |
| both-a | 梯度 A | 梯度 B | 相对前两种单路输入分别改变 |
| encoder-b | 梯度 A | 全零 | 与 encoder-a 完全一致 |
| zero-b | 全零 | 全零 | 与 zero-a/首次板测完全一致 |

梯度为精确二进制分数，不随机、不当作真实 encoder/decoder 输出。
每次运行前重建期望字节核对所有输入，运行后检查日志输入 CRC、输出文件长度、
原始 CRC（与 runner 相同初值/终值）、prefill 改变字节数、263 个浮点数有限性。
缺失/重复记录拒绝，发现错误立即停止后续调用。即使不同输入输出不变也不强行判通过；
这是待诊断/不确定，不一定单凭它就能认定硬件故障。

每次独立进程重新加载网络，不是同一网络连续暖态测试，不证明 warm cache/状态回灌正确。
超时策略沿用每进程 60 秒、TERM 后 5 秒 KILL、网络内设置 5 秒超时。
超时保留日志并冷重启后再判断；不能认为杀进程等于 NPU 复位。

结束时回传 `REPORT:` 指向的 `results/voice-report-*.tar.gz`。
它包含输入文件、输入清单、每次完整原始输出/日志、动态对比摘要、环境和 dmesg。
通过标签为 `PASS_SYNTHETIC_JOINER_ONLY`，**不是 KWS/ASR/TTS/真实语音或 openvela 通过**。

## 研发验证与后续

```sh
bash -n tools/npu-voice/test-linux-voice.sh
bash -n tools/npu-voice/test-linux-joiner.sh
python3 tools/test-linux-voice-script.py
python3 tools/test-linux-joiner.py
```

这两组共 10 个主机测试（含多个故障子用例），不调用真实 NPU。
第二阶段实际板测待回传。第一阶段原始包/回传文件保持原样。
后续真实语音应按参考源码核验 39 路 encoder 初态、逐块输入和输出状态回灌、
decoder token 输入的实际预处理及词表语义，再比较真实 KWS 结果。
ASR 36 路模型/6254 输出不是这套 KWS 39 路/263 输出，不得混用。
vocoder 的接口已查询，真实 mel 布局和 Matcha 输出连接尚待验证，不能把零 mel 冒充 TTS。
