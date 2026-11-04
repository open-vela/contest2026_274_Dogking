# v156：真实语音 prepared 模型与 openvela 候选

后续用户实测：joiner/decoder/vocoder固定CRC通过，encoder因专用池保留页不足返回ENOMEM。
扩池修复与实际分配器回归见 [v157](NPU_TENSOR_POOL_V157.md)，以下保留v156交付时记录。

2026-10-19。Linux 实机已通过四模型固定输入、14组变化输入和六场景关键词搜索。
openvela 新后端及诊断入口已完成候选构建/2GiB镜像校验，**尚未刷卡/运行于 openvela**。
这不是完整本地语音助手验收，也不是中文 ASR 已完成。

此前 v153–v155 的开发记录、失败报告和恢复镜像全部保留。
本轮继续遵守 `embedded-npu-porting` 的模型、运行时、产品链路分层验收要求。

## 本轮实现

- `tools/npu-voice/export-voice-prepared.py`：从已验证的本机 Linux prepare 布局导出
  精确地址 A7TV。不是通用 NBG 解析器/编译器，不能换成任意同名模型。
- `tools/a733-tensor-check.c`：共用实际 NuttX 容器解析器的离线校验，以及 Linux
  所有者私有缓冲区分配/执行诊断。使用本次进程取得的句柄，不复用捕获的句柄或物理地址。
- Linux 固定、动态、完整关键词测试包构建器与有界执行器；结果在本地再次核对全部张量。
- `aipet audio npuref`：通过公共 `system/a733_tensor_client.h` 调用可选 RTOS 后端。
  不依赖云配置，不自动录音、联网、发声或回退；失败不公布输出，不自动重试。
- `tools/audit-a733-staging.py`：逐字节核对构建时暂存的 SDK 文件已恢复。

本轮修正了旧推导中“分配长度比命令长度多56字节，因此跳过56字节”的错误。
本机内核 `vip_drv_task_get_cmd_info()` 返回命令分配基址；这56字节是**尾部容量**。
新导出器从偏移0审查 LOAD_STATE/STALL/END，未知 LINK/任务链/补丁依赖均拒绝。
不更改历史已通过的 YOLO A7PM 导出器及原有 ABI。

## 已验证与未验证

| 层次 | 本轮证据 | 状态边界 |
| --- | --- | --- |
| Linux 单模型 | 四个固定输入，全部42个输出完全匹配厂商基线 | 真实 NPU 执行，不是 CPU mock |
| Linux 动态输入 | 14组，128个输出；74处 encoder 状态回灌 | 各模型至少两组不同输入及不同输出 |
| Linux KWS | 1,728次真实 prepared 调用，三正例/三负例通过 | 预计算 fbank，不是实时麦克风、ASR |
| openvela 软件 | 容器、驱动适配器、客户端、应用入口、C搜索的主机回归 | 模拟错误/所有权测试不等于板测 |
| openvela 硬件 | 实验开关及候选镜像 | 尚未执行新 A7TV 语音模型 |
| 完整语音助手 | ICS43434/I2S、官方 Agent 等既有能力保留 | 新本地 KWS/ASR/TTS 闭环尚未接通 |

KWS 三个命中词为“你好小瑞”“小瑞”“小瑞小瑞”，负例是其他命令、噪声、静音。
这是固定六个样本的回归，不代表广泛准确率；第三个正例概率约0.154，接近0.15门槛。
encoder 标量输入38仍明确使用 FP32 特征偏移0/16/32…，不盲目回灌输出38的+8。
输出1..37才是逐块状态原始字节回灌。

## 构建

默认产品 defconfig **不打开** `CONFIG_A733_NPU_TENSOR_IO`。
以下仅在 SDK 临时暂存的 defconfig 打开实验选项，构建结束自动恢复原 SDK：

```sh
python3 tools/audit-a733-staging.py record "$OPENVELA_WORKSPACE" out/sdk-before-NEW.json
A733_TENSOR_EXPERIMENTAL=1 \
A733_BUILD_DIR="$OPENVELA_WORKSPACE/cmake_out/cubie-a7z_nsh_v156_tensor_candidate" \
JOBS=8 bash tools/build-a733-wapi.sh
python3 tools/audit-a733-staging.py check "$OPENVELA_WORKSPACE" out/sdk-before-NEW.json
```

设置 `OPENVELA_WORKSPACE` 为官方源码树的绝对路径；新建构建目录，不覆盖通过版本。
同目录再次构建可加 `--incremental`，仍须核对 SDK 恢复。
最终产物、布局检查和哈希以 [v156证据](../evidence/local-voice-v153/prepared-v156.md) 为准。
编译通过不是硬件通过，不能称为“全功能通过镜像”。

## openvela 首次板测

当前 Linux 卡和系统不要覆盖。换**另一张卡**测试候选；私有资产包内的 `voice/`
复制到 openvela 的 `/data/npu/voice`，必须先在电脑上核对资产清单和包哈希。
厂商/模型/派生 prepared 资产只保存在本地 `out/`，不加入 Git 或公开镜像发布。

先测设备与最小 joiner，任何失败均停止，保存完整串口/`dmesg`，不要连续盲重试：

```text
npu info
npu selftest
aipet audio npuref joiner /data/npu/voice/joiner.a7tv /data/npu/voice/input/joiner
aipet audio npuref decoder /data/npu/voice/decoder.a7tv /data/npu/voice/input/decoder
aipet audio npuref encoder /data/npu/voice/encoder.a7tv /data/npu/voice/input/encoder
aipet audio npuref vocoder /data/npu/voice/vocoder.a7tv /data/npu/voice/input/vocoder
```

默认固定测试会在驱动内比较捕获的输入/输出 CRC，成功后打印每路完整输出的 raw CRC。
四模型全部通过后，再按资产包 `TEST_COMMANDS.txt` 依次测试14组 `dynamic`，与
`voice/expected-outputs.json` 的每一路 `raw_crc32` 比较，不能只看退出码。
CRC 使用 NuttX raw CRC32：初值0、无 final xor，不是默认 zlib CRC。

把一次完整串口/SSH终端输出存为 UTF-8 日志，电脑上可自动检查所有CRC，避免手工漏看：

```sh
# 仅四个固定检查点（严格按上面的顺序）
python3 tools/npu-voice/audit-openvela-npuref.py \
  out/a733-openvela-voice-assets-v156-r1/voice/expected-outputs.json board-fixed.txt --fixed-only
# 四个固定 + 十四个动态，严格按 TEST_COMMANDS.txt 顺序
python3 tools/npu-voice/audit-openvela-npuref.py \
  out/a733-openvela-voice-assets-v156-r1/voice/expected-outputs.json board-all.txt
```

它会拒绝缺路输出、截断/重复日志、错误顺序、失败记录、长度和CRC不匹配。
该工具核对的是提供的日志，不自行证明日志来源或声学/整机验收。

单次硬件等待上限5秒；权重载入与文件校验额外耗时，初版每次载入权重，不是暖态实时版。
调用期间不要 task_delete/SIGKILL。程序延后 Ctrl+C/取消直到清理完成；硬件故障后隔离，
未确认恢复时不释放可能仍在 DMA 中的底层映射。软件失败/超时 mock 不等于硬件恢复验证。
不要并行运行另一项 NPU 诊断、YOLO 或关键词任务。

## 离线回归与证据复核

```sh
python3 tools/test-voice-prepared.py
bash tools/test-a733-linux-replay.sh
bash tools/test-a733-tensor.sh
bash tools/test-aipet-npuref.sh
bash tools/test-aipet-kws.sh
python3 tools/test-openvela-npuref-log.py
bash tools/test-aipet-audio.sh
bash tools/test-aipet-routes.sh
bash tools/check-openvela-first.sh
```

完整原始报告只在本地私有归档。`audit-prepared-results.py checkpoints` 校验170个输出
与74处状态连接；其 `kws` 模式逐一校验原始输入输出，并用主机编译的同一 C 搜索核心
重算六场景。主机校验不调用 NPU，也不会生成缺失模型输出。

## 后续顺序

1. 换卡完成上述 openvela 真实执行/变化输入门槛，保留原 Linux 卡及恢复镜像。
2. 通过后再做常驻模型、RAM 状态/特征流、冷启动与暖态延迟、取消和长稳测试。
3. 将真实麦克风前端接到本地 KWS，明确唤醒后的路由及麦克风隐私边界。
4. ASR 使用另一个36路状态/混合INT64契约和6254词表，必须先获得正确编译的ASR资产。
   现有 KWS 的39路/263音素不能冒充 ASR。
5. A7Z 上 Matcha CPU声学模型、NPU声码器、连续I2S播放和官方Agent闭环继续单独验收。

此前开发主机 Matcha→Linux NPU声码器的听音通过，不等于 Matcha 已移植到 openvela。
