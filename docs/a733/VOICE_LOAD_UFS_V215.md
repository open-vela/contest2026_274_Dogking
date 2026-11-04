# v215：加载诊断、LLM 启动门禁与 UFS 语音模型读取

2026-10-30。以 `ffce863` / v214 为恢复基线；旧镜像、源码、开发文档均保留。
恢复标签 `a733-v215-before-load-ufs-20261030`。本页是软件候选记录，不是板测通过记录。

## 真实问题与本轮边界

用户 v214 板测：服务停在 preparing 180 秒且 stop 不退出；分步 CPU TTS 配置成功，
用时 20.5871 秒；LLM preload 返回 -2。随后用户确认 UFS 尚未挂载
(`attempted=0 ready=0 stage=not-started`)，SD models 只有 transfer-check.txt，
`ufs start` 后可见 688065920 字节的 MiniCPM5，另有一个 0 字节旧 Qwen 文件。

因此 -2 的直接原因是 UFS 未挂载时选择了 SD 的缺失 Qwen 文件，不能归因于 NPU 内存。
后续用户提供预加载成功日志：UFS 加载 9.8027 秒，preload 总计 18.2635 秒，
LLM/CPU TTS 已驻留。service PID86 仍等待 Semaphore，栈 262016 字节，SIGINT 阻塞，
5 分钟没有 READY。排除了模型缺失是这次挂起的充分解释。

进一步发现明确的输出路径错误：v214 将 worker stdout/stderr 接到 `/dev/console`，
但板上 NSH 的 ALTSTDOUT/ALTSTDERR 是 `/dev/a7zconsole`。后者是已通过的 polling
驱动；`a733_serial.c` 明确关闭 IER、隔离未完成 IRQ 验证的 generic 16550 console。
generic TX 环形缓冲区满时会等待发送信号量，因此日志输出本身可以阻塞模型准备和 stop。
v215 改为 `/dev/a7zconsole`，stdin 仍是 `/dev/null`，不继承 SSH socket、不抢 NSH 输入。
增加节点与 defconfig 一致性、实际 polling write 的 >1MiB/CRLF 测试（底层 putc 为 mock）。
这是已修正的确定代码错误、与 ps 吻合的挂起原因；真实 READY/stop 恢复仍须新镜像板测。
若仍有阻塞，再看最后一个 BEGIN、phase age 和 ps；不声称仅凭主机测试已通过板测。

未改 UFS PHY/Gear/通道、PMIC、块写/回读/flush、分区、格式化、NPU MMU/DMA 预留、
摄像头算法、原语音模型/CRC 值。没有写板子或自动迁移/删除任何模型。

## 新日志和门禁

- `voice-load-v215: BEGIN/PROGRESS/END`：阶段、路径、字节数、耗时、结果；每条 flush。
- 后台日志固定写已通过的 `/dev/a7zconsole`，不能再误接 `/dev/console`。
- 服务 status 增加 load-stage、stage-age-ms 和 since-start-ms。
- TTS 分出 tokens/lexicon、65 MB acoustic-read-crc、acoustic-ORT-session。
- acoustic 每约 8 MiB 报一次进度，CRC 随读取增量计算，仍逐字节完整校验原 CRC。
  不是跳过校验；增量 CRC 对照独立 bit oracle 和原五条声学 golden。
- LLM 每约 64 MiB 及末块报进度；缓存命中不再读模型。保留原加载总耗时。
- ASR 的三个模型、vocoder 分别在 native-query/prepare 前后打印，不只在结束时打印。
- `aipet local check`：仅配置/文件/头检查，不加载权重；错误输出实际路径和存储策略。
- service start 先做 LLM check；缺失模型不创建后台任务、不进入 ORT/NPU 初始化。
  worker 内再次检查，避免启动前检查后文件被替换而盲目继续。
- 启动准备期间仍是 MIC OFF，真正 READY 之后才能说话。stop 仍为协作取消，
  **不能强行中断阻塞的内核 read 或 ORT CreateSession**；没有以超时强杀 owner 来伪造恢复。

旧 `voice-service-v214` 和驱动/boot 标签保留；用新增 `voice-load-v215` 识别本次固件。
阶段时间包括读取、CPU、内存及校验；不能当作纯 UFS 吞吐或真实声学延迟。

## 先修复当前 LLM 路径（v214 也可做）

先确认常驻服务 inactive；如果仍 active/stop 不退出，不并发做以下操作或强杀，回传 ps。

```text
ufs status
ufs start
ufs status
ls -l /ufs/models/MiniCPM5-1B-Q4_K_M.gguf
ufs autostart on
aipet local model auto
aipet local status
```

**等待 `ufs status` 的 ready=1**。应选 `/ufs/models/MiniCPM5-1B-Q4_K_M.gguf`，
大小 688065920。auto 未选中时先排查挂载/文件，不加载那个 0 字节 Qwen。
不要卸载正在使用的 UFS，不要重复初始化/格式化。

v215 再执行：

```text
aipet local check
time "aipet local preload"
```

v214 没有 check 命令，使用 status + preload。挂载开关仍遵从板上持久配置；
本版不为了语音擅自覆盖用户关闭 UFS 的配置。自动挂载可能尚在后台运行，
service 的快速 preflight 此时可拒绝；应等 ready 后显式 start，不盲目重试。

## ASR/TTS 的 UFS 支持

底层仅对 tensor 接口放行四个精确路径：

```text
/ufs/npu/general-asr/asr-encoder.a7tv
/ufs/npu/general-asr/asr-decoder.a7tv
/ufs/npu/general-asr/asr-joiner.a7tv
/ufs/npu/general-tts/vocoder.a7tv
```

不放行任意 `/ufs/` 路径、不改变旧 `/data/` 校验器、不允许遍历/后缀伪装。
QUERY、PREPARE、冷 RUN、resident RUN 都走相同 gate。所有 package 头/CRC/CID/
张量/所有权/guard/恢复隔离检查保留。CPU ORT 已支持常规只读 UFS 文件。

首次自动配置时：只有官方 storage ready 且八项必要文件都存在、是可读普通非空文件，
才优先选 UFS。元数据检查不等于完整验证；实际加载仍检查模型。
UFS 未挂载或八项全无时用原 SD；部分复制、I/O 错误或错误大小明确失败，不静默切 SD。
已显式配置的 RAM provider 不在运行中偷换路径。

### 无损复制：保留 SD、每个文件单独等完成

只在 service inactive 且 UFS ready 时执行；已有目录的 EEXIST 无需删除。

```text
mkdir /ufs/npu
mkdir /ufs/npu/general-asr
mkdir /ufs/npu/general-tts
```

下面**每一条 copy 都是后台任务**。每执行一条，用 `ufs status` 等到
`active=0 phase=complete result=0 verified=1` 再执行下一条，不能整块盲贴。

```text
ufs copy /data/npu/general-asr/tokens.txt /ufs/npu/general-asr/tokens.txt
ufs copy /data/npu/general-asr/asr-encoder.a7tv /ufs/npu/general-asr/asr-encoder.a7tv
ufs copy /data/npu/general-asr/asr-decoder.a7tv /ufs/npu/general-asr/asr-decoder.a7tv
ufs copy /data/npu/general-asr/asr-joiner.a7tv /ufs/npu/general-asr/asr-joiner.a7tv
ufs copy /data/npu/general-tts/tokens.txt /ufs/npu/general-tts/tokens.txt
ufs copy /data/npu/general-tts/lexicon.txt /ufs/npu/general-tts/lexicon.txt
ufs copy /data/npu/general-tts/model-steps-3.basic.ort /ufs/npu/general-tts/model-steps-3.basic.ort
ufs copy /data/npu/general-tts/vocoder.a7tv /ufs/npu/general-tts/vocoder.a7tv
```

`ufs copy` 拒绝覆盖旧文件。如果已有文件，用对应 `ufs compare <SD文件> <UFS文件>`
逐个等 `verified=1`；失败保留旧文件和日志，不直接覆盖/删除。复制许可证：

```text
ufs copy /data/npu/general-tts/ORT-LICENSE /ufs/npu/general-tts/ORT-LICENSE
ufs copy /data/npu/general-tts/ORT-ThirdPartyNotices.txt /ufs/npu/general-tts/ORT-ThirdPartyNotices.txt
```

许可证也每条等完成。模型及其他源许可证仍保留，不公开重新分发私有模型。

显式验证 UFS 路径（v215，不能用于旧版 tensor 白名单）：

```text
time "aipet audio general-voice /ufs/npu/general-asr /ufs/npu/general-tts"
aipet audio output i2s
aipet audio volume 50
aipet audio gain 5
aipet local on
aipet audio service start
aipet audio service status
```

等 READY 再说话。若 BEGIN 后长时间无 END，另一个 shell 查 status/ps/free/dmesg，
保留最后阶段和已等待时长，不强杀 owner。停服务、等 inactive 后可明确退回 SD：

```text
aipet audio general-voice /data/npu/general-asr /data/npu/general-tts
```

## 1000 MB/s 目标：不能当作本版承诺

历史实测：128 MiB / 1894 ms ≈ 67.58 MiB/s，见 `UFS_AUTO_V175.md` 及原始证据。
旧引导日志曾报告 Gear4、两通道、FAST、rate B；**不是本次冷启动的链路/吞吐验收**。

Radxa 产品简介列出双通道 UFS 3.0；焊接 UFS 3.1 器件不自动改变主控能力。
链路规格本身不能排除 1000 MB/s 目标，但 Kingston 128 GB 实物、板级链路、控制器/
DMA、CPU、FAT、大文件碎片、温度都会限制实际吞吐，不能直接承诺。

- 目前单槽串行、64 KiB 传输上限、100 us 轮询等待；每次命令带多次 HCI 快照、
  cache 维护和完整 DMA guard/未授权修改扫描，有明显软件开销。
- 下一阶段应先测分段时间，再验证更大 DMA/PRDT、受控 guard 扫描优化、队列/IRQ；
  每步保留冷/热字节比较与故障恢复，不能靠删除校验/flush 假装提速。
- 本版不改链路档位、不扩大 DMA，不把旧 67.58 MiB/s 当作新版本结果。
- UFS 仅改善冷加载；已在 RAM 中的 ASR/LLM/TTS 计算和首音频并不会按磁盘速度同比提升。

无损文件读基准：停止语音/摄像头/复制后运行，每次等任务结束：

```text
ufs readbench /ufs/models/MiniCPM5-1B-Q4_K_M.gguf 128
ufs status
```

按同样方法重复三次，记录 KiB/s、总字节和耗时；有缓存的 warm 测试与重启后的 cold
测试分列。不用 `time ufs readbench` 充当吞吐，也不把 copy 的 SD+SHA+写后校验当纯读。

官方资料：
[Radxa A7Z 产品简介](https://dl.radxa.com/cubie/a7z/docs/rad-doc-0152_radxa_cubie_a7z_product_brief__revision_1.1_g2545be1.pdf)、
[KIOXIA UFS FAQ](https://americas.kioxia.com/content/dam/kioxia/en-us/business/memory/mlc-nand/asset/KIOXIA_UFS_FAQ.pdf)。

## 软件证据、构建和下一步

新增 selector 缺失/完整/部分/I/O/坏文件分支、严格 UFS 名称/有界字符串、
实际 runtime adapter 的四路径 query/prepare/resident/损坏拒绝，及增量 CRC 等价测试。
保留 v214 全部语音/原五条 CPU acoustic golden、ARM 单元、摄像头/ISA/ABI/完整链接和
镜像/模型回读。主机 NPU 仍是显式 mock，不冒充真实音频或物理吞吐。

```bash
python3 tools/test-voice-v215.py out/NEW-v215-regression --build <build>
A733_V215_BUILD_DIR=<同目录> bash tools/build-a733-voice-v215.sh out/NEW-v215-build out/ort-static-v185-r2 --incremental
python3 tools/check-a733-voice-v215.py --build <同目录> --build-report out/NEW-v215-build --output out/NEW-v215-full --firmware-commit <源码提交>
python3 tools/package-a733-voice-v215.py <NEW-seal> ../NEW-v215.img out/NEW-v215-package --firmware-commit <源码提交> --voice-gates out/NEW-v215-regression/summary.json --full-gates out/NEW-v215-full/summary.json
```

每次新目录，测试暂存与完整构建不并发；SDK 应逐字节恢复。真实板测尚需：SD/UFS 两种
语音路径同音频转写/全部朗读、多轮/stop、加载分段耗时、冷启动 UFS ready 顺序、长稳和
真实首音频；不宣称整机 3–5 秒或存储 1000 MB/s 已达标。

## 本轮发布封存

- 固件源码：`2bba18123bb93f133cc15decf9ca94e081c6ff02`（包含 console 修复）；
  `5c3d406` 保留首次诊断/UFS 实现。旧中间构建 r1 不用于发布。
- 镜像：`../openvela-a733-cubie-a7z-sd-v215-voice-load-ufs-candidate.img`，2147483648 字节。
- SHA256：`a2cb7afe4bf003dfba82f9798e35cd76cfa4918438efebb2f5c393de7ccca771`。
- 内核：7176896 字节，SHA256 `c238b0d990060d935ea49adc2bf1db494d5f06456ff63c11e246947eb952e6d3`。
- Windows 原生 SHA 与 WSL 打包器一致；v214 镜像 SHA 仍为
  `c9569622ad928b0730bd79c8be6f17156249b5f5f9e4e858ea3aa1df9ce0bfa6`。
- 最终报告：`out/voice-regression-v215-r3`、`out/voice-build-v215-r2`、
  `out/voice-full-v215-r1`、`out/voice-package-v215-r1`；精简证据在 `docs/evidence/voice-v215`。
- 18 项语音回归、17 个实际 ARM 单元通过；最大静态栈帧 2384 字节。
  tensor adapter 保留原测试并增加到 25 组，public client 11 组；NPU 为明确 mock。
- 摄像头 22 项主机回归、完整链接、ISA/亲和性/有界等待/ABI 检查通过。
  完整构建 2057 项，SDK 301 路径逐字节恢复；constructor 24，runtime end=0x45dea000。
- GPT/ext4/内嵌内核/启动脚本、49 项私有资产和 3 条标准 WAV 回读校验通过。
- 主机/镜像通过不等于板测：READY、stop、多轮、真实声学及首音频延迟仍未验收；
  UFS 驱动速度没有在本版修改，没有实测 1000 MB/s。

封存 `out/v215-release-20261030`，恢复标签
`a733-v215-voice-load-ufs-candidate-20261030`；恢复包使用
`tools/checkpoint-voice-v215.py` 创建，目录
`out/checkpoints/a733-v215-voice-load-ufs-recovery-20261030`。
保留 v213/v214、失败/中间报告、用户未跟踪总结，不自动推送。

```text
python tools/checkpoint-a733-project.py verify out/checkpoints/a733-v215-voice-load-ufs-recovery-20261030 --check-references
python tools/checkpoint-a733-project.py restore out/checkpoints/a733-v215-voice-load-ufs-recovery-20261030 out/NEW-restore-v215 --with-git
```

先备份 SD 配置/凭据再烧录。**首次板测只修正 UFS 挂载和后台 console，不同时迁移语音模型**：

```text
ufs start
ufs status
```

等 ready=1，再执行：

```text
ufs autostart on
aipet local model auto
aipet local on
aipet local check
aipet audio output i2s
aipet audio volume 50
aipet audio gain 5
aipet audio service start
aipet audio service status
```

应该看到新 `voice-load-v215` 的 BEGIN/END/PROGRESS，而不是无输出地等待数分钟。
按前页流程测 READY、连续多轮与 stop。回传从 start 到 READY/失败的完整日志和 ps，
不要仅截最后一条 status；若仍卡住，以最后一个 BEGIN 的阶段继续定位。
