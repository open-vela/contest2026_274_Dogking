# v189：TTS 后半句被截断的应用层修复候选

2026-10-27。继承 v188 已板测启动的内核初始化流程；本版流式 TTS
修改尚未上板。未改 NPU 模型、MMU、DMA、I2S 寄存器、采样率或 UFS 数据。

## 现场与范围

原始用户日志按字节保存在 `docs/evidence/voice-v189/v188-board-test.txt`，
SHA256 `b4dd97dba2508953a2887e156ac59edd67af9f6384e750a28db969a439442391`。
v188 的 7 次次核启动调用返回 0，24 个构造函数完成，进入 NSH，
`npu selftest` 通过。它现在是**一次启动板测通过**，不是稳定性或语音闭环通过，
也未单独实验证明 v185 卡死的唯一根因。

`local-speak-stream "你好，语音测试成功。"` 返回 `-32/EPIPE`，
`packets=1 samples=24576 frames=53280`，用户仅听到“你好”。
代码将播放队列暂时为空直接设为致命 `-EPIPE`，并传回合成取消回调，
使仍在计算后半句的声学模型提前结束。100 ms 首播预缓冲不能保证
后续每句 CPU 合成都快于前句播放速度。

主机按 10 ms/480 帧模拟消费，在后续生产前加入 400 ms 延迟，
旧代码可稳定复现 `result=-32 frames=9120 bytes=18240`。
修复后同样条件返回 0，输出完整 48000 帧/96000 字节，与无延迟基准
**逐字节相等**，不是忽略错误、丢后半句或用静音填充冒充语音。
实机驱动自身也可能返回 EPIPE；此项改动不屏蔽真实设备错误。

## 实现

- 暂时缺 PCM 时只结束当前设备段，由既有同步接口正常排空 FIFO、
  停止 TX 并恢复静音引脚；随后重新预缓冲 100 ms，再启动下一段。
- 整条 utterance 的完成取决于生产者完成且全部队列数据播放结束，
  不能把当前设备段结束误判为整句结束。
- 重采样器只创建一次，跨段保留相位和尾样本；帧数累计，不重放前缀。
- 初始等待和补缓冲在打开设备事务前完成；消费线程是唯一读者，
  避免尾音排空、生产者晚关闭时再打开一个空事务。所有设备错误均保留，
  包括 ENODATA、EPIPE 和 close 失败；不归零、不重试失败段。
  合成错误、取消、超时、无进度均失败退出。
- 保持 3 秒有界 RAM 队列、总源音频最多 10 秒、操作总 deadline 120 秒，
  保持 join 失败后的隔离所有权语义。
- 新增 `local-tts-buffer-v189`：设备段数、补缓冲次数与软件等待时间。
  补缓冲可能导致句间停顿；这些数字不是物理首音频时延。

## 主机验证

旧应用层测试曾因未补齐 v183/v185 新增 general-provider 的测试替身而链接失败。
已在**仅用于主机的测试文件**中补齐：显式 general 请求返回 ENOTSUP，
不伪造识别/合成。生产固件仍链接真实 provider；这些应用 mock 回归不验证 NPU。

```sh
bash tools/build-voice-stream-v181.sh out/voice-v189-regression-r4
bash tools/test-aipet-audio.sh
python3 tools/test-i2s-stream-v181.py
```

覆盖有间隔完整输出、无间隔输出相等、生产者晚关闭、短尾帧、真正的
设备/生产错误、等待期间取消、无进度适配器、后续再次播放及 join 失败。
另保留重采样、分句、文字前端、流式 ASR 模拟回归。主机模拟不等于声学通过。

上述最终源码的三组回归均通过。原始日志保存在
`docs/evidence/voice-v189/stream-regression-r4.log`、`audio-app-r3.log`、
`i2s-mmio-r2.log`；旧版失败复现另存 `old-gap-reproducer.log`。

## 构建、打包和回退

新 SDK 构建目录独立，v188 ELF 不覆盖：

```sh
bash tools/build-a733-voice-v189.sh out/general-voice-build-v189-r2 out/ort-static-v185-r2
python3 tools/package-a733-boot-v188.py out/general-voice-build-v189-r2 ../../quickly-openvela/cmake_out/cubie-a7z_nsh_v189_tts ../openvela-a733-cubie-a7z-sd-v189-tts-rebuffer-candidate.img out/voice-image-v189-r1 --base-version 188 --purpose tts-rebuffer
```

最终功能源码提交 `2e5530ca5ed70c45c7f7518e083340016897720b`。
本机先完成 r1 全量构建，再收紧“设备事务前预缓冲、所有设备错误保留”的
边界，使用显式 `--incremental` 构建 r2。全新 SDK 用上述命令作全量构建；
已存在的 v189 构建必须显式选择 `--incremental`，不可隐式删除重建。
首轮 ELF/bin/map/config 保存在 `out/general-voice-build-v189-r1/kernel-first-pass`，
对应源码 `b3efeaf`，bin SHA256 为
`2a0cddd62c85b364f3a235cfcce1d1d8d4b7ced56cb3a92ccb64a1fc8e1bd8a2`，
未生成该中间版镜像供用户测试；不能把它当成最终 r2。

打包使用摘要锁定的、已启动的 v188 镜像，不改其模型数据分区。内核启动
标记仍为继承的 `M0 v188-boot-candidate`；新应用由 `local-tts-buffer-v189`
输出标识。烧录前先备份现有 SD 的模型、配置、录音。

最终 r2 交叉链接和布局检查通过：file=7007952、runtime=92004352 字节，
SDK 暂存的 280 个路径逐字节恢复。构建日志保存在
`out/general-voice-build-v189-r2/build.log`，SHA256
`9bc668cbb91c43a7d07c29f39306ce709b4b461997e0a85b6906ab9b7a769b7b`。

镜像 `../openvela-a733-cubie-a7z-sd-v189-tts-rebuffer-candidate.img`，
2147483648 字节，SHA256：
`c56e2b4c42bd921a9edd8a69a91e05f39000de5c2d8aea50465b5ef66ee0c463`。
内核 SHA256：
`4b0974d8ddf82b682fec27e06378e35dd149e70cd76c139d281d02ed68b4c884`；
ELF SHA256：
`84f7114f982050645c02bb0def95209fe570d59013f79c8c53d7a3501a753cd5`。
打包、GPT、ext4 检查、内核读回、启动调用关系及非内核分区逐字节摘要检查通过；
模型 FAT 未改变。原 v188 镜像摘要仍为
`a18142fda0ed79896af9a357a190034668089368809f255cac841085cc41c44b`。
详见 `docs/evidence/voice-v189/image-summary.json` 和 `image-package.log`。
**这些是构建/制品检查，不是新版实机启动或听音通过。**

本地版本标签及 checkpoint 名为 `a733-v189-tts-gap-candidate-20261027`。
恢复工具保存源码归档、Git 历史 bundle 和四个外部制品的摘要，不把大镜像
塞入 Git；新目录恢复不覆盖当前工程：

```sh
python3 tools/checkpoint-a733-project.py verify out/checkpoints/a733-v189-tts-gap-candidate-20261027 --check-references
python3 tools/checkpoint-a733-project.py restore out/checkpoints/a733-v189-tts-gap-candidate-20261027 out/restore-check-v189-r1 --with-git
```

## 上板命令

```text
aipet audio output i2s
aipet audio volume 50
aipet audio gain 5
aipet audio general-voice /data/npu/general-asr /data/npu/general-tts
aipet audio local-speak "你好，语音测试成功。"
aipet audio local-speak-stream "你好，语音测试成功。"
cat /dev/a733-audio
aipet audio local-speak-stream "第一句测试。第二句继续。第三句结束。"
cat /dev/a733-audio
aipet audio local-speak-stream "重复播放测试，不能丢失后半句。"
free
```

第一条非流式播报是对照；后两次应播全且返回 0。记录 packets、frames、
segments、rebuffers。若仍返回 -32，保留紧随其后的音频驱动状态，不应
直接认为它还是应用队列错误。再测 Ctrl+C 及取消后的下一次播放。

`local-tts-stream` 的 frames 是整条 utterance 累计；音频驱动诊断节点
保留的是**最后一个设备段**的 frames/elapsed/INTSTA，二者不应强行相等。

该日志中另外两项尚未解决：通用 ASR 转写为空（listen=-61），以及本地
LLM 找不到模型（-2）。不能用本次 TTS 修复宣称整个语音链路已通过。

## 本轮镜像清理

用户授权清理无用镜像后，核对用途、引用、大小、SHA256，再直接删除
两份明确失败的 `v185-kernel-only-incomplete*.img`；共释放 4294967296
字节（4 GiB）。未进回收站，不能从回收站恢复；它们不是通过版本，
不是任何保留 checkpoint 的引用。删除摘要在 image-cleanup.json。
当前 v188、v185 完整打包基线、v182 以及其他历史镜像/源码/模型均保留。
新 v189 镜像会另占 2 GiB，不把删除量冒充本轮净释放量。
