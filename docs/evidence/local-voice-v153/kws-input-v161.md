# v161 分段真实音频输入：实现与交付证据

2026-10-20，使用embedded-npu-porting技能：保持模型来源/布局、实际状态回灌和golden边界，
不把未知输入或模拟后端当成识别正确性证明。本轮不SSH、不运行开发板、不采集/上传声音，
不烧录、不改旧Linux开发目录。v160真实通过证据及原镜像另行保留，所有历史文档不删除。

## 数据路径与可信边界

新voice_kws_input.inc通过公共a733_pcm_capture获取完整48k单声道PCM，或严格读取显式
/data/的16k/48k单声道S16LE WAV，CPU前端输出实际归一化fbank，再交给真实NPU客户端。
旧npukws严格输入输出oracle不放宽；新kwsinput/kwswav/kwsmic显式区分known与unknown。
已知调用仍比较所有输出CRC，未知调用仍做finite/完整CRC/guard等检查，但没有原生golden，
因此终端明确标注acoustic accuracy NOT accepted。没有冻结模型输出或故障时回退CPU伪结果。

有限输入、调用/上下文/候选/推理时间边界，SIGINT边界检查及失败不发布候选。
所有候选在完整成功、resident释放、亲和性恢复后才打印；不接动作、Agent、上传或播放。
模型、VIP2 ABI4、驱动和DSP参数未更换；没有重新转换、重新量化或修改beam/阈值。
公共同步采集不支持并发流，本版明确是先录后推理。新麦克风声学与持续实时性待上板。

## 软件回归

- 实际KWS应用+相同C搜索，旧六场景FILE oracle保持1771调用；动态known六场景也1771次，
  每个调用原生输出及最终calls/命中/chain不变。模拟未知输出只用于分支/清理测试。
- 原11类错误与模式选项保持；未知NaN、输入NaN/abs1001/超长输入、真实pending SIGINT
  拒绝，不发布候选，fd和CPU亲和性恢复。
- 实际audio_app接公共采集mock：RAM录音、gain、边界、溢出/取消、NPU错误后重新进入正常；
  不上传、不写录音文件、不播放、不误入云端路由，既有录音文件未被覆盖。
- 实际WAV解析+实际fbank与六份Linux原特征逐字节一致；截断、RIFF长度、声道、采样率拒绝。
  最终r2包已复测。首次独立harness缺少cstdint头导致编译失败，修正测试头文件；
  一次复测命令误传WAV父目录导致harness断言失败，改正测试路径后通过。
  失败日志均保留，未因此修改生产解析/前端代码。
- 音频配置/ASR路由/TTS契约/FIR重采样回归通过；这是mock，不是新云端或扬声器验收。
- tensor manifest8、adapter20、client11及实际专用页分配器1MiB/2MiB各16轮回归保持。
- CRC全扫描2073流分组、2048表项及1025保护页尾用例通过，npuref11项保持。
- 六WAV日志auditor新增5项：完整golden通过、未知调用/错误feature CRC/缺前端/失败拒绝。
  复用严格profile auditor的调用、命中、chain、cache范围和时延约束；原日志不篡改。
- ARM64实际voice对象13个函数静态栈最大1168字节，实际audio对象19个函数最大688字节。
  主机和目标对象编译不算NPU硬件执行。

本地原始证据（out忽略发布，不含API key）：

```text
out/v161-kws-delivery.log
out/v161-audio-final.log
out/v161-wave-r2-verified.log
out/v161-wave-auditor.log
out/v161-wave-final-assets.json
out/v161-tensor.log
out/v161-crc.log
out/v161-npuref.log
out/v161-profile-audit.log
out/v161-target-voice.log
out/v161-target-audio.log
out/kws-v161-target/summary.json
out/audio-v161-target/summary.json
out/v161-staged-sources.json
out/build-v161-buffered-kws.log
out/sdk-before-v161.json
out/v161-sdk-restoration.log
out/package-v161-input.log
out/verify-v161-input.log
out/seal-v161-input.log
```

最终原始证据SHA256：

| 文件 | SHA256 |
| --- | --- |
| v161-kws-delivery.log | fa8d849815d9d345e1438f12ec4e52af01e94b1c9f5f8799d9a822983c48e41b |
| v161-audio-final.log | b9fea180e59dde96cb432d610e23974f2d6bd6acf0a86e70e0ac8a46a8ded74e |
| v161-wave-r2-verified.log | 7b80e8ee983976484817a4eb531006ef5bc11854699aba6c8c8ff606ce242341 |
| v161-staged-sources.json | 011cf645f260f6f4d70be743e4a389ff8fa7c134ef58032f386d46c595c8d935 |
| build-v161-buffered-kws.log | 671681227aad7283aeadd04ed5657d790345ad402c0e9796417dc794b6a72815 |

模型仍沿用v158-r2，模型来源与具体SHA见旧prepared/v158证据。
新增六WAV来自已验证归一化PCM（包含原静音尾），清单关联PCM、特征、WAV的SHA及feature CRC。
补充包SHA256为`774c6cc022e10624e0f16e802f573000bd4e3bd64c7fe9ca189685f1a44d267d`。
没有将私有native输出文件放进板端资产或镜像。

## 上板与后续

见[全部测试步骤](../../a733/LOCAL_NPU_KWS_V161_INPUT.md)。先验证六WAV每次known=calls/unknown=0，
再测真麦克风；任意声音候选不构成声学准确率验收。后续再扩展连续采集接口和队列、
热稳/误唤醒/阈值标定、完整ASR。不能以本版替代全文ASR或完整本地语音助手。

## 完整构建、镜像与封存

独立目录`quickly-openvela/cmake_out/cubie-a7z_nsh_v161_buffered_kws_candidate`完整2972目标
构建成功。内核2688832字节、runtime36978688字节，比v160增加4096字节；
页/段/栈/Image占位检查通过，19个关键暂存文件匹配仓库，149个SDK路径逐字节恢复。
已有第三方签名比较、UNUSED/zblue和build-id等警告保留，不宣称零警告构建。

仅替换v160底板partition3内的`/boot/openvela/a733/Image`，ext4/GPT/内嵌内核检查通过。
partition3之前348127232字节和469762048之后的全部字节与v160一致；v160整卡哈希未变。
原GPT partition4末端未2048扇区对齐提示保留，未重分区。
Windows独立复算镜像、内核、ELF哈希一致。

- 新镜像`openvela-a733-cubie-a7z-sd-v161-buffered-kws-candidate.img`，2147483648字节，SHA256：
  `bb00b3432bd593a08de4a8da331f161767914b95142b75ad80b84d8aa941a7f3`
- 内核SHA256：`428ed74940c72141093be74b2623b4c2589f343fc48da98d24324dbec35b5c87`
- ELF SHA256：`d42898435cc6b53e8f8be13524b8a1b70a536b7bc80d34062e28d45abcad2e77`
- 源码包`out/a733-v161-source-20261020.tar.gz`，409个源码/工具文件，SHA256：
  `0794678f363db9ac40082a14d72af23b2d7928e12949ab490e66e3bfcf767df8`

`out/v161-candidate-20261020`封存Image/ELF/System.map/config、暂存清单、逐文件源码SHA和manifest；
补存本轮日志、WAV清单/包及全部现有docs，保留开发历史。源码包不包含完整SDK、模型或docs，
需要配套SDK和单独资产；当前工作树有未提交开发，不能仅凭HEAD复现。
manifest明确hardware_executed/boot_passed/live_microphone/asr/end_to_end_voice_assistant均为false。
本版待用户上板，不将上述软件与镜像检查冒充硬件或声学验收。
