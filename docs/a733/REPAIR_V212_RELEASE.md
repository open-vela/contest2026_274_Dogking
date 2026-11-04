# v212 修复候选镜像与恢复

2026-10-30。固件源码：024a9bd3ab19b28df7f8b2bd9b36e9d9a8a6ddd5。
本版已完成离线修复、完整构建与封装，尚未进行新实机验收。
不能据此声称I2S已恢复出声、YOLO长时间通过或语音3–5秒达标。
现有v204通过基线、v209/v210/v211镜像与Git历史、全部开发过程文档均保留。

## 镜像

仓库父目录：`openvela-a733-cubie-a7z-sd-v212-repair-candidate.img`

- 整卡大小：2,147,483,648字节。
- SHA256：`3935195bf22940517ceae1a352dbf062e661bffaec70293b297b998dffefb728`。
- 内核：7,160,456字节；SHA256：
  `991784d227c4f8adef3face70827ecc65ef19a154192e9558b04e6793d0b7045`。
- Windows原生.NET流式SHA256与Linux打包校验一致；旁边有`.img.sha256`。
- 保持24个构造器，runtime end `0x45de6000`；NPU硬件ABI/模型/全局调度不变。
- 不写物理SD/UFS，不格式化，不迁移模型，不删除旧U-Boot。
- 烧录整卡前备份SD上的个人文件/模型/配置；已有UFS模型不会被打包工具改动。
  MiniCPM5权重不随镜像分发，仍使用原有UFS或SD文件。

这是v210已封存普通镜像的受控派生，保留全部模型并逐个回读49个私有文件、
3条标准ASR WAV；检查GPT/保护区、根文件系统、内核、引导脚本及全镜像哈希。
自动引导脚本为camera-v212，SWOUT1读/受控开/读回护栏保留。
boot188、live209、voice210等部分诊断前缀沿用历史名称，不能据此判断刷错版本。

## 修复内容与测试

修复设计及完整上板命令见[REPAIR_V212.md](REPAIR_V212.md)。

- CSI去掉21ms短休眠误超时，保留整帧2秒、清理独立2秒及全部数据/所有权护栏。
  控制broker不再意外继承CPU6单核限制，新增调度耗时快照。
- LLM工作线程创建时设置明确绑核、局部RR、32KiB栈，首次执行跨yield确认身份。
  回复等待最多15秒；超时报告线程阶段，保留工作线程可能使用的内存并要求重启。
  输出先写入池缓冲，全部完成后才发布给调用方，不以释放内存强行脱困。
- I2S每次完整恢复TX配置，记录播放期间PB5/6/7、CTL/CLK/DIV及真实PCM幅度。
  继续保留录音/空闲PB7低电平，防止原电流声问题回归；不随意操作模块SD引脚。
- 离线预热失败明确标记LLM阶段、麦克风未开启，不再将其误报为缺少麦克风。

有效验证结果：

- `out/voice-regression-v212-r1`：15组语音回归、16个真实SDK/ARM编译单元。
- `out/repair-check-v212-r2`：22组摄像头回归、目标对象/最终ELF/ISA/ABI检查。
- `out/voice-build-v212-r2`：完整构建；SDK298路径逐字节恢复。
- CSI共280项事务/错误检查，包含259项回调生效后故障注入、新增35ms调度延迟、
  真正截止时间到期和冻结时钟测试；64次暂停/重装载及原隔离/清理测试保留。
- 执行真实LLM调度函数的合成测试：单调时钟/EINTR/超时、未完成输出不发布、
  工作线程缓冲保留、后续任务拒绝；原量化/数值行循环一致性测试保留。
- 执行真实I2S TX配置/启动函数的MMIO模型：完整配置恢复、无关引脚/RX保留、
  非零/峰值统计及错误读回停止；这些不是实际时钟波形或声音证明。

第一轮构建用于编译检查；最终镜像只使用冻结源码后的r2构建。
首轮完整审计r1发现旧工具错误假定绑核函数内联；v212新工具按真实
`pool_worker -> llm_affinity -> llm_aff_id`链路验证，并保留两次MPIDR/一次MIDR、
阻塞yield、RR/绑核属性和sem_clockwait检查。r2全套通过后才封装。
早期输出保留，不作为最终通过证据。

最终证据封存在`docs/evidence/repair-v212-final`，183份原始日志/JSON/栈记录；
不含模型、WAV或可执行文件。修复前实机日志另存于
`docs/evidence/repair-v212/board-v210-before-repair.txt`。

## 重现构建

已有输出不可覆盖，重跑必须换新的输出目录/镜像名。WSL Ubuntu、本地SDK和
锁定的ORT静态库，不联网下载模型。实际使用的命令：

```sh
A733_V212_BUILD_DIR=/mnt/d/My-Program/AI-Agent-Program/GPTsprogram/Openvela/quickly-openvela/cmake_out/cubie-a7z_nsh_v205_camera_perf bash tools/build-a733-repair-v212.sh out/voice-build-v212-r2 out/ort-static-v185-r2 --incremental
python3 tools/test-voice-v212.py out/voice-regression-v212-r1 --build ../../quickly-openvela/cmake_out/cubie-a7z_nsh_v205_camera_perf
python3 tools/check-a733-repair-v212.py --build ../../quickly-openvela/cmake_out/cubie-a7z_nsh_v205_camera_perf --build-report out/voice-build-v212-r2 --output out/repair-check-v212-r2 --firmware-commit 024a9bd3ab19b28df7f8b2bd9b36e9d9a8a6ddd5
python3 tools/package-a733-repair-v212.py out/v212-release-20261030 ../openvela-a733-cubie-a7z-sd-v212-repair-candidate.img out/repair-package-v212-r1 --firmware-commit 024a9bd3ab19b28df7f8b2bd9b36e9d9a8a6ddd5 --voice-gates out/voice-regression-v212-r1/summary.json --full-gates out/repair-check-v212-r2/summary.json
```

封存目录`out/v212-release-20261030`共10件：ELF/bin/map/config、模式及许可3件、
脚本/manifest3件。必须从本次精确ELF生成脚本，不能沿用旧CRC和符号地址。

## 本地恢复

- 修复前：`a733-v212-pre-fix-20261030`（9d6261f）。
- 固件源码：`a733-v212-repair-source-20261030`（024a9bd）。
- 最终封存：`a733-v212-repair-sealed-20261030`。
- 独立恢复归档：`out/checkpoints/a733-v212-repair-recovery-20261030`。

恢复工具继承v211所有外部文件引用并增加v212镜像/封存/报告。归档保留Git历史、
工作目录原始字节，包括未跟踪的`PROJECT_FULL_SUMMARY_20261030_V211.md`，
不会为追求干净状态删除它。只允许恢复到全新目录，不reset当前工作树。

```text
python tools/checkpoint-a733-project.py verify out/checkpoints/a733-v212-repair-recovery-20261030 --check-references
python tools/checkpoint-a733-project.py restore out/checkpoints/a733-v212-repair-recovery-20261030 ../contest2026_274_Dogking-restore-v212-20261030 --with-git
```

依赖私有模型/大镜像以绝对路径和哈希引用，须保留原文件。没有推送GitHub。
新板测先音频，再另一次冷启动测试LLM，再另一次冷启动测试摄像头；
不要在摄像头失败后直接连跑全部闭环。要求重启时不重复初始化、不强制释放。
