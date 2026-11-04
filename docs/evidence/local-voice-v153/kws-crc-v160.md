# v160 CRC全扫描加速：软件与镜像证据

2026-10-20。使用embedded-npu-porting技能，保留v159通过镜像/源码/历史记录。
本轮不SSH、不执行开发板推理、不采集/上传音频、不刷卡、不修改原Linux开发目录。
前一版用户实机数据另见[v159归档](openvela-v159-runtime-passed.md)。

## 实现与安全边界

新增私有`a733_tensor_crc.inc`，基于反射多项式0xedb88320生成8张只读256项表。
8字节组采用字节拼接、8张表查找和异或，末尾逐字节。无对齐类型转换、尾部超读、
运行期表初始化、额外工作堆或硬件CRC指令；每编译单元8KiB只读表。
runtime/client两处采用，官方libc、旧A7PM/YOLO实现和全系统CRC配置不改变。

完整保留每轮LOAD复制及**复制后目标区域**CRC，原始输入基线、实际输入、全输出、
状态链、END和guard检查；scratch/padding继续显式清零。不让源扫描代替目标扫描，
不省略复制、不缓存“校验成功”位、不复用NPU可变数据，不调整阈值/beam/模型。
新增RUN标志0x8和公共client开关，CLI为crclegacy；仅恢复RUN内的官方CRC，
应用仍slice8。fullclean仍只控制cache范围。ABI和RUN结构长度保持，旧内核拒绝未知flag。

## 软件、目标对象证据

- 官方fast/slow分别与slice8及独立逐位实现对照2073组：不同seed/长度/非对齐/分段、
  最大1MiB；2048表项逐项验证递推；1025个保护页尾部用例确认不跨界读取。
  NULL+零长度保留seed，123456789 raw CRC为2dfd2d88。ASan/UBSan通过。
- manifest/39张量/状态8组、实际adapter20组、public client11组通过；两种CRC模式
  均检查快照损坏拒绝、不发布失败输出。原有guard/timeout/隔离/清理回归保持。
- 真实reserved-page allocator接实际client：1MiB/2MiB各16轮，保留页及回收正常。
- 实际应用与相同C搜索，使用原Linux张量文件做主机后端，六场景1771调用calls/hits/chain
  保持一致；quiet/fullclean/CPU/crclegacy和11类失败通过。文件后端不进入板端资产。
- npuref11项、音频配置/路由/重采样/完整TTS、prepared格式5组、搜索8组、profile
  auditor9组（含新CRC标签强制核验）、结果auditor5组通过。音频/云端为mock，
  不是新声学或网络验收。
- ARM64实际工具链五个C对象通过；KWS C++对象9个静态栈帧最大1152字节。
- 构建暂存16个关键文件匹配当前仓库，包含新CRC表，避免漏暂存导致旧算法进镜像。

主机单次微基准：1MiB随机输入、64轮seed链，官方96696406ns、slice8为19659324ns，
两者chain=a7a06e3c；仅是本电脑主机结果，不是A76/NPU/实机语音提速比例。
板测以同镜像cpu6 quiet/crclegacy对照及六场景P95/超预算数为准。

关键本地原始输出：

```text
out/v160-crc.log
out/v160-tensor.log
out/v160-npuref.log
out/v160-kws-host.log
out/v160-audio.log
out/v160-profile-audit.log
out/v160-result-audit.log
out/v160-prepared.log
out/v160-search.log
out/v160-crc-host-benchmark.log
out/v160-target-c.log
out/v160-target-kws.log
out/tensor-v160-target/compile-summary.json
out/kws-v160-target/summary.json
out/v160-staged-sources.json
out/build-v160-crc.log
out/sdk-before-v160.json
out/v160-sdk-restoration.log
```

模型沿用v158-r2，SHA256
`231485021a389881d50e1921a07340668a3ef73a3d3f923df8a0f12159239467`。
模型来源/具体哈希及转换链见[v158](resident-kws-v158.md)/[v156](prepared-v156.md)。
固定特征checkpoint，任意未知输入仍ENODATA；不是实时麦克风、中文ASR或完整助手。
上板步骤见[v160说明](../../a733/LOCAL_NPU_KWS_V160_CRC.md)。整卡烧录前备份全部/data。

## 构建、镜像与实际源码封存

独立目录`quickly-openvela/cmake_out/cubie-a7z_nsh_v160_crc_candidate`完整2972目标
构建成功，file=2684736、runtime=36974592字节。比v159增加16384字节，DMA arena不变。
布局/页/栈边界通过，149个SDK暂存路径逐字节恢复；16个关键暂存源/头文件与仓库一致。
既有第三方签名比较、UNUSED宏、zblue poll及build-id警告保留，不是零警告构建。

镜像在仓库上一级`openvela-a733-cubie-a7z-sd-v160-crc-candidate.img`，2147483648字节。

- 整卡SHA256：`c82e6a3835ae82f2992dc93f5122529c99187331c32df3020870e4ba67151084`
- 内核SHA256：`6875c6a223b9c370ca2ff8827f920bb4fb4e1e8ce5ad267a9bc20fb7039d0f8d`
- ELF SHA256：`2696f8cf94eb63a5c644695c5f2b1969628a446a928e185f8a1a69cf44f613d9`
- 源码包`out/a733-v160-source-20261020.tar.gz`（403个源码/工具文件）SHA256：
  `16478db6d850b017e84f0b3fb1bce1ddee6f7c4937e59230b862fd86ee3a32dd`

复制v159只读底板，仅替换partition3内的`/boot/openvela/a733/Image`。
ext4/GPT/内嵌内核检查通过；原GPT partition4末端未2048扇区对齐提示保留，未重分区。
前348127232字节和从469762048字节起的后缀逐字节一致；v159原镜像SHA保持
`4140c934bcd929b042382156f0aca4517043c19f028f0623288984de7aed4c93`。
Windows独立Get-FileHash确认新整卡哈希一致。

封存目录`out/v160-candidate-20261020/`含manifest、实际源码哈希、内核/ELF/config/map、
测试命令和全部历史文档副本。不含完整SDK、私有模型或Linux原生输出张量。
模型测试资产另沿用v158-r2。源码包含当前脏工作区，不以Git HEAD冒充完整复现依据。
本轮新候选尚无实机启动、速度、长稳或热稳证据，manifest相应验收字段保持false。
