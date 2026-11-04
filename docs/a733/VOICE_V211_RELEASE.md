# v211 语音工作区与收尾优化候选

2026-10-30。固件源码：d2a061da58aed59b5f7564221a90a6c2dcf841b3。
v210及全部旧镜像、Git历史和过程文档保留。本次不推送远端、不访问开发板。

## 本版范围

NPU通用ASR、CPU MiniCPM5、CPU声学模型＋NPU vocoder及I2S播放均保留。
改动与代价见VOICE_V211.md；上板顺序见VOICE_V211_TEST.md。
这不是“整个TTS跑NPU”，也不是整机3–5秒通过版。

镜像在仓库父目录：
openvela-a733-cubie-a7z-sd-v211-voice-workspace-candidate.img（2,147,483,648字节）。
SHA256：c168b667d0a22f171d2a2763158de4d05c00898d9c38f55b8fd2f04fd50b2f99。
内核7,160,424字节，SHA256：
374976b6cec37718154a83e84e7b2708af03f9253ddeba32f6b58c48ddc0b289；
CRC32：90cb428c。Linux与Windows独立全镜像SHA一致。

- ASR合并40ms PCM批次，结束时排空；跳过不会再被使用的尾部FFT。
- LLM复用最多256KiB Q8工作区，每次仍清零并重新量化、完成相同行运算。
- TTS引擎内复用371200字节工作区，关闭时清零。
- 首次40ms真实PCM起播，补缓冲仍100ms；保留短尾/断流/取消/设备错误。
- 模型、精度、时钟、NPU硬件执行、公共ABI及摄像头功能不改。

## 软件验证边界

最终语音回归目录为out/voice-regression-v211-r3：
14组回归及16个语音真实SDK/ARM编译单元。
最终完整链接目录为out/voice-check-v211-r1：
22组摄像头回归、最终ELF/ISA/内存布局检查。
完整构建为out/voice-build-v211-r1，SDK298路径逐字节恢复。
24个构造器，runtime end=0x45de6000；RAW池0x45913000/4MiB。

ASR原版/新版90组边界对照的encoder输入浮点字节/调用数量一致，
合计少算4932个无用零尾帧；主机计时只作诊断，不能换算为A733提速。
216条Linux捕获重放及35状态反馈、真实CPU声学对照继续保留。
Q8实际GGML量化器覆盖零块/混合零/随机数据和10000次复用。
50ms源PCM暂停生产的专项测试证明40ms门槛可以起播，
随后完整播放与原PCM逐字节相同；这不是实测扬声器起音。

仍需实机验证：任意语音识别率、NN真实执行/连续轮次、A76执行身份、
声学起音、录放切换、停止/异常后资源状态及端到端3–5秒。
第一次冷加载、后续热轮、上下文长度须分别记录，不用空白或固定前缀抢首音。

## 构建与封装

WSL Ubuntu，使用锁定的本地SDK、ORT静态库、私有模型/词表。
下列目录已经存在，复跑必须使用全新输出目录/镜像名：

```sh
A733_V211_BUILD_DIR="$PWD/../../quickly-openvela/cmake_out/cubie-a7z_nsh_v205_camera_perf" bash tools/build-a733-voice-v211.sh out/voice-build-v211-r1 out/ort-static-v185-r2 --incremental
python3 tools/test-voice-v211.py out/voice-regression-v211-r3 --build ../../quickly-openvela/cmake_out/cubie-a7z_nsh_v205_camera_perf
python3 tools/check-a733-voice-v211.py --build ../../quickly-openvela/cmake_out/cubie-a7z_nsh_v205_camera_perf --build-report out/voice-build-v211-r1 --output out/voice-check-v211-r1 --firmware-commit d2a061da58aed59b5f7564221a90a6c2dcf841b3
python3 tools/package-a733-voice-v211.py out/v211-release-20261030 ../openvela-a733-cubie-a7z-sd-v211-voice-workspace-candidate.img out/voice-package-v211-r1 --firmware-commit d2a061da58aed59b5f7564221a90a6c2dcf841b3 --voice-gates out/voice-regression-v211-r3/summary.json --full-gates out/voice-check-v211-r1/summary.json
```

封存目录out/v211-release-20261030：ELF/bin/map/config四件、模式/许可三件，
加精确ELF生成的camera-v211脚本/manifest三件，共10件。
不能直接沿用旧脚本的内核CRC。旧boot188/M0启动前缀仍是历史诊断。

镜像仅为新普通文件；源v210全镜像SHA先锁定再更新根分区，
三条标准WAV及49个私有模型/词表/许可文件逐个回读。
保护区/GPT、e2fsck、内核和Linux/Windows全镜像SHA均需一致。
没有物理SD/UFS写入、格式化、模型迁移或旧U-Boot删除。
这是整卡镜像，烧录前请备份SD数据；MiniCPM5权重不随镜像分发。

## 本地恢复

源码标签：a733-v211-voice-source-20261030。
封存标签：a733-v211-voice-sealed-20261030。
恢复标签：a733-v211-voice-recovery-r2-20261030（r1保留）。
checkpoint：out/checkpoints/a733-v211-voice-recovery-r2-20261030。
继承v210-r6的全部外部文件锁定引用，再增加v211镜像/校验和/10件封存。
归档包含源码和Git历史；大模型/镜像/库为外部哈希引用，必须保留原文件。

```sh
python3 tools/checkpoint-a733-project.py verify out/checkpoints/a733-v211-voice-recovery-r2-20261030 --check-references
python3 tools/checkpoint-a733-project.py restore out/checkpoints/a733-v211-voice-recovery-r2-20261030 ../contest2026_274_Dogking-restore-v211-r2-20261030 --with-git
```

只恢复到全新目录；绝不reset当前仓库。恢复后运行test-voice-restored-v211.py，
重跑14组语音/22组摄像头/阶段解析器并校验每个归档原始字节与git fsck。
历史CRLF文件若在WSL Git显示修改，只接受完整归档哈希相同且严格EOL-only；
明确记录而不重写历史记录、不谎称WSL Git完全干净。

r1实际新目录已完成14组语音、16个ARM单元和22组摄像头回归，
但恢复工具最后误把沿用的v210延迟解析测试改名成不存在的v211文件。
r2只修正该引用、恢复工具标签及本文档，不改固件/镜像/既有封存证据。
新的恢复目录为contest2026_274_Dogking-restore-v211-r2-20261030。
