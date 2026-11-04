# v157：encoder ENOMEM专用I/O池修复

后续实机已通过（2026-10-20 归档）：18 次调用、170 路固定/动态输出全部匹配，
资源恢复正常。见 [v157 实机归档](../evidence/local-voice-v153/openvela-v157-runtime-passed.md)。
下文保留交付时的候选/待测历史，不代表当前仍未通过。

2026-10-19。按 `embedded-npu-porting` 分层验收保留通过和失败记录。
用户已将同一张卡改烧openvela；此前Linux原始报告已回传保留，但板端Linux不可再假定在线。
本轮没有SSH连接板端，也没有再次要求Linux重测。

## v156实际板测结果

[用户串口记录](../evidence/local-voice-v153/openvela-v156-user-fixed.txt)：
NSH启动和NPU selftest通过，CID=1000003b。三个固定模型返回的全部输出CRC匹配：

| 模型 | CRC | 命令 elapsed-us |
| --- | --- | ---: |
| joiner | 7529f5ca | 35624 |
| decoder | dd1e9df7 | 98752 |
| vocoder | 4883df06 | 1343721 |

这三项现为openvela真实硬件的固定检查点通过，不再只是Linux证据。
耗时包含包载入/CRC/复制，不是暖态纯NPU时间；vocoder包约8MB，不能直接把1.34秒
都算成计算。encoder两次返回 `-12/ENOMEM`，没有输出；四模型整体仍未通过。
没有动态/多块KWS/实时麦克风/ASR/完整助手验收。

## 根因与最小作用域

系统约4GB空闲不代表NPU专用I/O池也空闲。驱动原池为1MiB/256页：
页0留给selftest，页16..122永久保留旧LeNet固定常量，共107页。
因此查询的可分配空间为148页/606208字节，最大单段为133页。
`buffers=16`是句柄上限，不是本次根因；公共客户端将39路切片合并为两个句柄。

encoder原始输入303684字节、输出299524字节；每路64字节对齐后303744/299584，
两个独立分配再按4096页取整，分别75/74页，合计149页/610304字节。
第二个分配必然失败。合并成单个148页缓冲也不能解决旧池只有133页连续空间的问题。

仅在 `CONFIG_A733_NPU_TENSOR_IO=y` 时把普通I/O池改为2MiB：

- 原产品关闭实验开关时仍是原1MiB，旧ABI v4和16句柄不变；A7TV ABI仍为1。
- 保留LeNet固定VIP地址/常量页，保留自测页，不缩小/挪用这些保留区。
- stage VIP和STLB起点随池尾自动从0x02100000/256移到0x02200000/512，
  不与新池重叠，末端仍处于同一个16MiB MMU窗口，STLB范围仍小于4096条。
- 固定32MiB低地址DMA arena、24MiB prepared池、MMU条目格式、IRQ、时钟、
  引导/SMP、模型和所有golden文件均不改变。新分配总预算仍在原arena内。

## 回归方法

```sh
python3 tools/test-a733-runtime-pool.py
bash tools/test-a733-tensor.sh
bash tools/test-aipet-npuref.sh
python3 tools/test-voice-prepared.py
bash tools/check-openvela-first.sh
```

新增测试不是无限制calloc桩：从当前实际驱动提取分配器、保留页、释放/关闭和所有权
函数，接上实际公共客户端，以及应用真实39路长度。
旧1MiB模式连续16轮重现ENOMEM且RUN次数0；新2MiB模式16轮分配/模拟执行/释放通过，
每轮空闲页恢复、旧常量逐字节不变，另测16句柄耗尽、stage映射边界与arena预算。
使用ASan/UBSan/严格警告；模拟输出不是encoder硬件推理。
修复前主机测试只mock成功分配，没有实际保留页布局，这是此前漏检原因。

## 新镜像板测

构建及交付以 [v157证据](../evidence/local-voice-v153/tensor-pool-v157.md) 为准。
候选已完成构建/布局/整卡检查：
`openvela-a733-cubie-a7z-sd-v157-npu-pool-candidate.img`，尚未实机验收。
模型资产与v156相同，不重新转换。重烧整卡会覆盖data分区，须先在电脑备份整个data分区
内容（包括voice、应用配置和凭据等），再按v156步骤放回 `/data/npu/voice`。

```text
npu info
npu selftest
free
aipet audio npuref joiner /data/npu/voice/joiner.a7tv /data/npu/voice/input/joiner
aipet audio npuref decoder /data/npu/voice/decoder.a7tv /data/npu/voice/input/decoder
aipet audio npuref encoder /data/npu/voice/encoder.a7tv /data/npu/voice/input/encoder
aipet audio npuref vocoder /data/npu/voice/vocoder.a7tv /data/npu/voice/input/vocoder
npu info
free
```

空载 `npu info` 预期 `pool=2093056 free=1654784 buffers=16`；这不是系统总RAM。
每个模型调用结束应回到相同free值。固定模型通过后再按
[v156动态顺序](LOCAL_NPU_PREPARED_V156.md) 测14组，保留完整UTF-8日志供逐路CRC检查。
任一硬件错误/FAULT/超时即停，不连续重试、不后台并行，不用Ctrl+C杀在途任务。
主机验证通过与修复镜像生成均不代表encoder硬件已经通过，须等新串口输出。
