# v159：全校验 KWS 优化候选交付证据

2026-10-20。遵循 embedded-npu-porting 的恢复点、原生输出对照与分层验收要求。
本轮没有 SSH、开发板录音/联网/上传、重启、烧录或真实 NPU 调用。
v158 六场景实机通过证据已另存：[原始日志与核验](openvela-v158-runtime-passed.md)。
所有历史文档和通过版本保留，不以新候选覆盖旧成果。

## 改动与不变项

仅实验 A7TV 路径缩小 cache clean 到当前模型完整映射池和全部已用 STLB slot，
提供 `fullclean` 恢复旧清理范围的对照选项。每轮仍复制、校验 LOAD 区，显式清零
scratch/page padding、重建 END 尾页，不复用上次 NPU 修改后的工作区。
应用通过公共 BSP 接口复用官方 CRC 实现，建立有界参考索引；默认调用线程临时
使用 CPU6/A76，可选 CPU7/auto，关闭客户端后恢复原亲和性。

ABI v4 / A7TV ABI1 / 原 RUN 结构不变，独立 PROFILE ioctl 返回当前 fd 上次 RUN
九阶段耗时及字节计数。新增 chunk P50/P95/max、160ms 超预算计数及应用校验/搜索耗时。
模型、beam、阈值、关键词、输入/输出 CRC、全状态回灌、guard、changed、超时和隔离
均保留。默认产品实验开关仍关闭；没有增加 NPU 频率、扩大固定32MiB DMA arena，
也没有打开实时麦克风、云端或 Agent 动作。

## 软件证据（不是新实机速度证据）

- manifest/39张量/状态8组、实际驱动 adapter18组、公共 client10组通过 ASan/UBSan。
  包括快照切换、LOAD损坏、scratch/padding污染后清零、范围外canary、fullclean、
  profile元数据、输出损坏、超时/隔离和关闭清理。
- 实际 reserved-page allocator 接实际 client：旧1MiB与实验2MiB各16轮；
  旧模式重现encoder分配不足，新模式模拟执行/释放且保留页不变。
- 官方CRC fast/slow分别对独立逐位实现比较2073组长度/seed/错位/分段组合。
- 实际 KWS 应用与相同 C 搜索核心：六组原生张量文件对照、1771次模拟调用、
  三正例/三负例的calls/hits/chain保持一致；另测亲和性/quiet/fullclean及11类失败。
  文件后端明确只用于主机测试，不进入板端模型包。
- npuref旧11项、音频配置/重采样/路由/完整TTS、搜索8组、prepared格式5组通过；
  音频/云服务在测试中为mock，不声称本轮重新验收声学或网络。
- 新profiling日志核验器6组、原KWS日志核验器5组通过；用户v158原始日志独立复核
  `NPUKWS_SUPPLIED_LOGS_MATCH`，6场景1771次调用。
- ARM64真编译器：runtime默认关闭/实验启用、plan/client/搜索五个C对象通过。
  最终实际KWS C++对象通过，9个静态栈帧最大1152字节，无动态栈。
- 最终暂存的15个关键文件与仓库一致，记录于`v159-staged-sources-final.json`。
  主机错误诊断、快照scratch/padding防御修订已纳入最终增量构建，不使用首轮草案内核。

关键原始输出均保留在`out/`：

```text
v159-tensor-final.log
v159-crc-regression.log
v159-kws-host-delivery.log
v159-npuref-delivery.log
v159-audio-regression.log
v159-search-regression.log
v159-prepared-regression.log
v159-profile-auditor-delivery.log
v159-result-auditor-delivery.log
v158-user-kws-audit-final.json
tensor-v159-target-final/compile-summary.json
kws-v159-target-delivery/summary.json
v159-staged-sources-final.json
build-v159-kws-optimized.log
build-v159-kws-final.log
sdk-before-v159.json
v159-sdk-restoration-final.log
```

早期mock头缺少FAR声明的失败、修正后回归以及第一轮完整构建日志仍保留。
WSL Git将既有CRLF视为尾空格的检查输出也保留；未批量改写用户文件换行，
Windows原生Git差异格式检查通过。

## 资产、恢复点与验收边界

沿用v158-r2包（6175926字节），SHA256：
`231485021a389881d50e1921a07340668a3ef73a3d3f923df8a0f12159239467`。
模型未重新转换，仍为Zoo1.1.0 / CID1000003b的prepared FP32原生I/O检查点；
模型哈希、来源与转换链见[v158交付](resident-kws-v158.md)及[v156记录](prepared-v156.md)。
包中只有三模型、六组fbank、关键词及CRC对照，不含Linux输出张量或host-native-map。

v158原镜像SHA保持：
`44a1d2bce28ef6776f25d3a9f219b364595809026c06d4ff5bafbcbf911b9bf8`。
v158封存目录、旧源码包和v157恢复版本不改写。

v159性能/启动/资源回收仍待用户板测。工作量减少不等于已测速度提升；
固定特征满足160ms预算也不等于实时采集、fbank、热稳或端到端语音助手验收。
按[全部测试步骤](../../a733/LOCAL_NPU_KWS_V159_OPTIMIZATION.md)保存六场景日志，
再用新auditor核对正确性、字节范围、调用数、亲和性恢复与时延。

后续顺序：先以相同六场景取得优化前后与P95数据，再接ICS43434采集/fbank队列，
最后处理连续输入、热稳和应用事件。任意音频不在当前golden索引中会明确ENODATA；
未来实时入口需要独立的动态输入验证，不能把这个错误改成成功来宣称已接入麦克风。
中文ASR还需其对应已转换模型与端到端识别验证，不能复用263词元KWS模型冒充。

## 最终构建与镜像封存

全新构建目录为`quickly-openvela/cmake_out/cubie-a7z_nsh_v159_kws_optimized_candidate`。
先完整2972目标构建，再完成最终修订的2002目标增量依赖构建；两次均成功。
最终布局file=2668352、runtime=36958208字节，相较v158运行占用增加8192字节。
SDK的148个暂存路径最终逐字节恢复；构建时15个关键文件匹配当前源码。
仍有既有第三方签名比较、UNUSED宏、zblue poll和build-id警告，不是“零警告”。

候选整卡镜像（2147483648字节）：
`../openvela-a733-cubie-a7z-sd-v159-kws-optimized-candidate.img`。

- 镜像SHA256：`4140c934bcd929b042382156f0aca4517043c19f028f0623288984de7aed4c93`
- 最终内核SHA256：`d01fb9981d1908eb0f6ac8129dd82a5262c3d7c49eb3f978803e7c7bf6ae63a0`
- v158复用资产SHA256：`231485021a389881d50e1921a07340668a3ef73a3d3f923df8a0f12159239467`

WSL打包后提取内嵌内核SHA一致，ext4/e2fsck和GPT检查通过；原GPT的partition4
末端未2048扇区对齐提示保持，不做无关重分区。Windows独立Get-FileHash得到相同整卡SHA。
从v158镜像复制，仅替换partition3内的`/boot/openvela/a733/Image`；
整卡烧录依然会覆盖/data，必须先备份模型、配置及凭据。

partition3以外逐字节比较通过：前348127232字节和从469762048字节起的后缀均一致；
原v158镜像SHA与通过版本一致。最终封存在`out/v159-candidate-20261020/`，
manifest记录Image/ELF/config/System.map及逐文件源码SHA，另附测试命令、日志和文档。

源码包为`out/a733-v159-source-20261020.tar.gz`，包含401个源码/工具文件，SHA256：
`17b5d645ec51fc0064ac516a375eecf6147fcd5ebe4cb2b403d7e1bb4a5296f7`。
不含完整SDK、私有模型和原生Linux输出张量；文档和实机日志另保留在仓库/交付目录。
最终ELF SHA256为`a7f5efcaff00837389297a56fbdea80d18f3463d577a7be45810edf9215f87f9`。
Windows独立核对源码包哈希一致。本轮没有新实机执行，manifest明确保持
hardware_executed/boot_passed/live_microphone/asr/end_to_end_voice_assistant为false。
