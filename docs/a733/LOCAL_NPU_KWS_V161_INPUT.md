# v161：真实 WAV / ICS43434 分段录音 → 本地 NPU KWS

## 本阶段做什么

保留v160通过版本。新增本地真实音频入口：WAV或ICS43434录音 → CPU FIR/fbank →
实际encoder状态回灌 → decoder/joiner搜索 → 终端候选关键词。不是中文全文ASR，
不调用云端、Agent、TTS或下位机，不自动录音/自启动。麦克风只在显式命令期间工作。

现有公共采集接口是同步整段录音，故本版是**录完再推理**，不是同时采集推理或连续实时监听。
冷模型准备也在录音结束之后；命令耗时包括采集、DSP、模型准备和推理。
未知声音没有逐调用Linux golden，不应将status=0或候选关键词当作识别准确率验收。

## 烧录与资产

新镜像位于仓库上一级 `openvela-a733-cubie-a7z-sd-v161-buffered-kws-candidate.img`。
整卡烧录会覆盖全部/data，先备份模型、网络/SSH/Agent配置与凭据，不仅是boot分区。
已通过v160镜像、源码和所有历史文档保留。新候选尚待上板。

模型仍用 `out/a733-openvela-kws-v158-r2.tar.gz`，SHA256：
`231485021a389881d50e1921a07340668a3ef73a3d3f923df8a0f12159239467`。
补充WAV包 `out/a733-kws-wav-v161-r2.tar.gz`，190709字节，SHA256：
`774c6cc022e10624e0f16e802f573000bd4e3bd64c7fe9ca189685f1a44d267d`。
在电脑解压，复制两包voice目录内容到板上/data/npu/voice，不能多嵌套一级voice。
新包只有六个WAV和清单，不含模型、冻结原生输出或API key；原模型包无需更换。

## 1. 基础与兼容性：单独保存日志

```text
npu info
npu selftest
free
ls -l /data/npu/voice/kws/wav
aipet audio npukws /data/npu/voice /data/npu/voice/kws/features0.f32 cpu6 quiet
npu info
free
aipet audio kwsinput /data/npu/voice /data/npu/voice/kws/features0.f32 cpu6 quiet
npu info
free
```

旧入口仍需161调用、1命中、chain=c0a6f8f2；新入口需status=0、calls=known=161、
unknown=0、candidates=1、相同chain。旧npukws仍在未知输入时于NPU执行前拒绝ENODATA。
新kwsinput才明确允许有限未知输入，不能把两个模式混为一谈。

## 2. 六组 WAV：另存一份完整日志

```text
aipet audio kwswav /data/npu/voice /data/npu/voice/kws/wav/wave0.wav
npu info
free
aipet audio kwswav /data/npu/voice /data/npu/voice/kws/wav/wave1.wav
npu info
free
aipet audio kwswav /data/npu/voice /data/npu/voice/kws/wav/wave2.wav
npu info
free
aipet audio kwswav /data/npu/voice /data/npu/voice/kws/wav/wave3.wav
npu info
free
aipet audio kwswav /data/npu/voice /data/npu/voice/kws/wav/wave4.wav
npu info
free
aipet audio kwswav /data/npu/voice /data/npu/voice/kws/wav/wave5.wav
npu info
free
```

| WAV | frames / feature-crc | calls / candidates | chain |
| --- | --- | --- | --- |
| 0 | 210 / 6a969644 | 161 / 1（你好小瑞） | c0a6f8f2 |
| 1 | 210 / b9197206 | 278 / 1（小瑞） | 871a92c3 |
| 2 | 210 / ea0f1594 | 298 / 1（小瑞小瑞） | 66236234 |
| 3 | 210 / d6d3798e | 391 / 0 | a1596f25 |
| 4 | 332 / f7b2359f | 96 / 0 | 1d4e2db1 |
| 5 | 332 / 27184580 | 547 / 0 | aa07f970 |

每组必须status=0、known=calls、unknown=0，前四组unused-tail=5，后两组=15。
若固定WAV出现unknown或feature-crc不同，停止并保留日志，不能算golden通过。
NPU空闲应回到1654784字节；查看堆是否持续增长。六个WAV来自原Linux验证PCM，
已包含原5120样本静音尾，不是本版悄悄新增填充。

电脑仓库根目录验证完整六组日志（不要混入第1步或麦克风日志）：

```sh
python3 -X utf8 tools/npu-voice/audit-openvela-kws-wav.py \
  out/a733-openvela-kws-v158-r2/application-expected.json \
  out/a733-kws-wav-v161-r2/manifest.json /path/to/v161-six-wav.txt
```

## 3. 真麦克风：六组通过后再试，另存日志

保持此前已验证的ICS43434接线、电源、公共音频节点。无需开扬声器。

```text
aipet audio status
aipet audio gain 5
time "aipet audio kwsmic /data/npu/voice 5000"
npu info
free
time "aipet audio kwsmic /data/npu/voice 5000"
npu info
free
time "aipet audio kwsmic /data/npu/voice 5000"
npu info
free
time "aipet audio kwsmic /data/npu/voice 5000"
npu info
free
```

第1次安静；后3次在打印recording后约0.5秒开始，分别说“你好小瑞”“小瑞”“小瑞小瑞”，
在第3秒前说完，尾部留安静。gain=5是之前板测合适的默认，命令沿用持久配置。
此入口只保留RAM，不生成WAV、不上传。录音完全停止后才显示buffered frontend并运行NPU。
真实声音unknown>0正常，终端会明确提示没有golden准确性证明。候选只是实验性KWS结果，
不是全文识别，也不会触发任何设备动作。没识别到时先回传完整日志，不提高阈值凑命中。

Ctrl+C会在采集返回或当前NPU RUN完成后的边界处理，清理resident并恢复亲和性后返回；
不是强行删除任务、复位NPU或自动重试。失败不发布部分候选。返回后检查npu info/free。

## 输入边界和待办

WAV仅/data/普通文件、PCM标准16字节fmt、S16LE单声道16k/48k、0.5–10秒；不猜声道/格式。
48k走已有63-tap FIR降至16k；实际fbank为80维、25ms/10ms、S16/32768归一化。
特征29–1000帧、finite且abs≤1000；录音500–10000ms，默认5000ms。
模型每16帧推进，最多61块、6000调用、64 decoder上下文、32候选，推理阶段60秒截止。
不隐式补齐末尾，unused-tail明确报告。输出finite、全CRC、guard及既有隔离保留。
候选在完整执行和清理成功后才发布；显式缓冲擦除不代表第三方分配器安全擦除保证。
受限输入降低内存风险，但第三方fbank内部C++分配未新增完整OOM异常恢复保证。

后续顺序：先六WAV板测 → 真麦克风声学复核 → 阈值/误唤醒和热稳测试 →
独立扩展连续采集接口与有界队列，再谈持续监听、ASR和Agent联动。v160仍有chunk超160ms，
不能凭平均吞吐量宣称硬实时。

## 开发复现

```sh
python3 tools/test-aipet-npukws.py out/a733-openvela-kws-v158-r1
bash tools/test-aipet-audio.sh
bash tools/test-kws-wave.sh out/a733-kws-wav-v161-r2/voice/kws/wav out/a733-openvela-kws-v158-r2/voice/kws
python3 tools/test-kws-wav-audit.py
bash tools/test-a733-tensor.sh
bash tools/test-a733-crc.sh
A733_TENSOR_EXPERIMENTAL=1 \
A733_BUILD_DIR="$(realpath ../../quickly-openvela)/cmake_out/cubie-a7z_nsh_v161_buffered_kws_candidate" \
JOBS=8 bash tools/build-a733-wapi.sh
```

使用配套原SDK；构建目录不要指向旧通过版本。主机模拟与目标编译不算真实NPU/声学验收。
构建封存和本轮软件证据见[交付记录](../evidence/local-voice-v153/kws-input-v161.md)。
