# v160：CRC完整扫描加速候选

2026-10-20。v159已实机通过六场景及五组A/B，见
[原始证据](../evidence/local-voice-v153/openvela-v159-runtime-passed.md)。
本次不SSH、不刷卡、不录音、不上传、不联网、不驱动实物。

## 本轮改变

- 私有BSP软件slicing-by-8 CRC32，每8字节并行查8张只读表；raw seed、不取反，
  与官方NuttX crc32part的多项式/结果相同。没有ARM CRC/NEON等新指令要求。
- 仅张量runtime和公共tensor client采用新实现，官方libc CRC和旧A7PM/YOLO路径不改。
- 仍完整复制每个LOAD区，然后扫描**目标工作区**逐次核对CRC；不以源CRC代替目标CRC，
  不跳过权重、输入、状态、输出、END或guard检查，也不保留上轮可变DMA数据。
- scratch/page padding继续显式清零；命令/MMU/cache/IRQ/reset/时钟/内存arena不变。
- 保留`crclegacy`，仅让本次RUN驱动校验恢复官方算法；应用CRC仍用新算法。
  新RUN标志0x8，ABI v4/A7TV ABI1及结构长度保持不变。旧内核不支持新标志会拒绝，
  不会自动降级；应使用本版整套镜像。
- 原`fullclean`仍仅改变cache维护范围，CPU/quiet参数保留。默认cpu6、bounded、slice8。
  本轮不声称完全省去复制开销，实际ARM64提速仍待板测。

两个编译单元各含8KiB只读CRC表；不动态初始化，CRC算法本身不分配工作堆，不改变线程调度。
循环以明确字节拼接取数，无强制对齐指针类型转换；不足8字节逐字节处理，无越界尾读。

## 烧录与资产

v159通过镜像不覆盖。本版使用独立v160候选镜像。
**整卡烧录前备份整个/data，包含模型、配置、凭据。**
沿用`out/a733-openvela-kws-v158-r2.tar.gz`，SHA256
`231485021a389881d50e1921a07340668a3ef73a3d3f923df8a0f12159239467`。
恢复其voice内容到`/data/npu/voice`，无需重新转换模型。

## 六组主测试

串行执行，不并发其他NPU任务。任何FAULT/超时/负status立即停止，不连续重试/reset，
不强制Ctrl+C/task_delete在途任务。主测试独立保存UTF-8完整日志，不与后续A/B混在一起。

```text
npu info
npu selftest
free
aipet audio npukws /data/npu/voice /data/npu/voice/kws/features0.f32 cpu6 quiet
npu info
free
aipet audio npukws /data/npu/voice /data/npu/voice/kws/features1.f32 cpu6 quiet
npu info
free
aipet audio npukws /data/npu/voice /data/npu/voice/kws/features2.f32 cpu6 quiet
npu info
free
aipet audio npukws /data/npu/voice /data/npu/voice/kws/features3.f32 cpu6 quiet
npu info
free
aipet audio npukws /data/npu/voice /data/npu/voice/kws/features4.f32 cpu6 quiet
npu info
free
aipet audio npukws /data/npu/voice /data/npu/voice/kws/features5.f32 cpu6 quiet
npu info
free
```

每条开头必须显示`driver-crc=slice8 app-crc=slice8`，后面status=0、
affinity-restored=1、completed等于chunks（前四12、后两19），资源正常回收。

| features | calls | hits | chain | 期望 |
| --- | ---: | ---: | --- | --- |
| 0 | 161 | 1 | c0a6f8f2 | 你好小瑞 |
| 1 | 278 | 1 | 871a92c3 | 小瑞 |
| 2 | 298 | 1 | 66236234 | 小瑞小瑞 |
| 3 | 391 | 0 | a1596f25 | 无 |
| 4 | 96 | 0 | 1d4e2db1 | 无 |
| 5 | 547 | 0 | aa07f970 | 无 |

现有profile auditor兼容新增标签，继续核对六组调用/字节/时延：

```sh
python3 -X utf8 tools/npu-voice/audit-openvela-npukws-profile.py \
  out/a733-openvela-kws-v158-r2/application-expected.json /path/to/v160-main.txt --require-crc slice8
```

## CRC A/B：另存日志

同一镜像/核心/输入，先legacy再slice8，比较rebuild、check、chunk P95和超预算数。
每次返回都打印npu info/free，结果仍必须161/1/c0a6f8f2，不减少任何校验范围。

```text
aipet audio npukws /data/npu/voice /data/npu/voice/kws/features0.f32 cpu6 quiet crclegacy
npu info
free
aipet audio npukws /data/npu/voice /data/npu/voice/kws/features0.f32 cpu6 quiet
npu info
free
aipet audio npukws /data/npu/voice /data/npu/voice/kws/features5.f32 cpu6 quiet crclegacy
npu info
free
aipet audio npukws /data/npu/voice /data/npu/voice/kws/features5.f32 cpu6 quiet
npu info
free
```

如要复核cache，附加fullclean可与crclegacy同时使用；不要混淆这两个独立因素。
crclegacy不回退应用CRC，也不是完全回退v159；整版回退须使用保留镜像。

本版仍是固定fbank KWS原生输出检查点，不自动接麦克风；任意未知特征仍ENODATA。
即使本次六组达到160ms，也不能直接宣称实时采集、热稳、ASR或完整语音助手通过。

## 构建复现

在仓库根目录WSL shell中使用原配套SDK：

```sh
bash tools/test-a733-crc.sh
bash tools/test-a733-tensor.sh
bash tools/test-aipet-npuref.sh
python3 tools/test-aipet-npukws.py out/a733-openvela-kws-v158-r1
python3 tools/test-openvela-npukws-profile.py
A733_TENSOR_EXPERIMENTAL=1 \
A733_BUILD_DIR="$(realpath ../../quickly-openvela)/cmake_out/cubie-a7z_nsh_v160_crc_candidate" \
JOBS=8 bash tools/build-a733-wapi.sh
```

构建命令会重建指定生成目录，不要改成源码或旧通过版本目录。
主机FILE后端只回归应用，不运行硬件；源码与构建哈希见
[交付证据](../evidence/local-voice-v153/kws-crc-v160.md)。
