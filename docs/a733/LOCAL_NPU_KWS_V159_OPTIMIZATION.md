# v159：保留全校验的 KWS 性能优化候选

2026-10-20。v158 已实机通过六组固定特征连续 KWS，先保留该镜像和源码，再做本轮优化。
本版构建/镜像/哈希状态见 [交付证据](../evidence/local-voice-v153/kws-optimized-v159.md)。
本轮不自动 SSH、烧录、录音、上传、联网、播音或向 Agent 发送控制命令。

## 已证实的旧开销与改动

v158 每次 RUN 都 clean 整个24MiB prepared池和全部16个STLB slot，
即使 joiner 仅映射约200KiB工作区。应用重复使用逐位CRC，并线性搜索1728条参考记录。
这些是源码可确认的工作量，不能将其占比直接当成实机耗时占比。

| 项目 | v158 | v159 |
| --- | --- | --- |
| prepared cache clean | 每次25165824字节 | 当前模型的完整 page-rounded prefix，含所有区域/padding/END尾页 |
| STLB clean | 全部262144字节 | 所有实际使用 slot 的完整前缀，slot仍为16384字节 |
| resident 工作区初始重建 | 全区清零再逐LOAD复制 | 每个LOAD复制并校验，scratch和padding显式清零，尾页重新生成 |
| 应用CRC | 逐字节8次位运算 | 经公共BSP接口复用官方NuttX crc32part（原始seed、不取反） |
| 参考输入查找 | 全表线性查找 | 有界哈希索引，完整key再比对，重复key拒绝 |
| CPU | 原亲和性下自动调度 | 默认仅当前调用线程临时使用CPU6/A76；可选CPU7或auto，退出恢复 |
| 测量 | 整轮/模型聚合时间 | 驱动9阶段、字节计数、应用校验/搜索、chunk P50/P95/max/超时步数 |

池clean字节数：encoder 8077312、decoder 606208、joiner 204800，分别约为
旧范围的32.1%、2.41%、0.814%。这不是“已提速3/41/122倍”的声明。
缓存维护指令、MMU格式、NPU reset/init/时钟/IRQ、固定32MiB DMA arena都不改变。

重建复核特别保留原语义：不复制快照中的scratch或padding作为初始化，
即使它们被污染也显式清零；所有LOAD区域继续逐次校验CRC。没有复用NPU改写的工作数据。
全部39路状态、每次输入/输出CRC、guard、END尾页和changed检查均保留。
没有改beam、阈值、关键词、模型、浮点格式或调用顺序，不用减少正确性检查换取速度。

`fullclean` 对照仅恢复24MiB/全部STLB的清理范围；CRC与重建方式仍为新版本。
它不是完全回退到旧固件，需要完整回退时使用保留的v158镜像。

## 兼容性与边界

- ABI v4、A7TV ABI1及原RUN结构长度保持不变。增加独立0xf0 PROFILE ioctl与RUN标志0x4，
  不在旧结构尾部直接追加字段。默认产品实验开关仍关闭。
- PROFILE仅返回本fd最后一次RUN；公共客户端验证version/status/committed后使用。
- 官方CRC的fast与slow实现均与独立逐位参考比对2073组：seed、长度、错位和分段组合。
  当前板配置使用已有官方table版本，不新增CRC硬件指令或修改全系统CRC配置。
- CPU亲和性只影响本次aipet调用，不独占CPU，不改其他任务的策略/优先级/亲和性。
  若调用者原mask不含指定核则明确失败，不越过原限制；使用`auto`保持原调度。
- 哈希表不代替CRC或SHA；每次匹配完整model+inputCRC key，输出仍来自实际后端。
- `npukws`仍为固定特征检查点，未知特征输入提交硬件前返回ENODATA。
  不是任意语音ASR，也没有悄悄开启麦克风。

## 上板准备

烧录整卡会覆盖data，**先备份整个data分区的模型、配置和凭据**。
v159不需要重新转换/下载模型，使用原v158-r2测试包：
`out/a733-openvela-kws-v158-r2.tar.gz`，SHA256
`231485021a389881d50e1921a07340668a3ef73a3d3f923df8a0f12159239467`。
恢复到 `/data/npu/voice`；如需旧npuref检查点还须恢复v156包里的input目录。

先运行：

```text
npu info
npu selftest
free
```

不并发其他NPU任务，不写私有NPU诊断节点，不Ctrl+C/task_delete在途任务。
任何FAULT、超时或负status即停止，不连续重试/reset；原隔离机制没有关闭。

## 六组主测试：默认优化、指定A76、减少串口干扰

每次返回后都运行`npu info`和`free`，保留完整UTF-8日志。

```text
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

省略可选参数仍是cpu6；不加quiet保留逐块进度。quiet不隐藏命中、统计、错误或总结果。

| features | calls | hits | chain | 期望 |
| --- | ---: | ---: | --- | --- |
| 0 | 161 | 1 | c0a6f8f2 | 你好小瑞 |
| 1 | 278 | 1 | 871a92c3 | 小瑞 |
| 2 | 298 | 1 | 66236234 | 小瑞小瑞 |
| 3 | 391 | 0 | a1596f25 | 无 |
| 4 | 96 | 0 | 1d4e2db1 | 无 |
| 5 | 547 | 0 | aa07f970 | 无 |

必须status=0、affinity-restored=1、completed等于chunks（前四组12、后两组19），
NPU free恢复1654784，堆用量不持续增长。对比本次开机自己的基线，不强求历史固定数值。

## 可选A/B：另存一份日志，不混入上述六组日志

同一输入保持calls=161、hits=1、chain=c0a6f8f2，对照只改变CPU/clean范围。
每条后也检查NPU/堆回收；有错误不继续下一条。

```text
aipet audio npukws /data/npu/voice /data/npu/voice/kws/features0.f32 auto fullclean quiet
aipet audio npukws /data/npu/voice /data/npu/voice/kws/features0.f32 auto quiet
aipet audio npukws /data/npu/voice /data/npu/voice/kws/features0.f32 cpu6 fullclean quiet
aipet audio npukws /data/npu/voice /data/npu/voice/kws/features0.f32 cpu6 quiet
aipet audio npukws /data/npu/voice /data/npu/voice/kws/features0.f32 cpu7 quiet
```

先分别比较相同CPU下fullclean/默认的`clean`耗时与字节量，再比较CPU6/auto。
不要将调度、温度、输出模式均不同的两次结果直接当成单因素提速比例。

## 如何解读分项与核验

`profile[0/1/2]`分别为encoder/decoder/joiner，九个us字段：
validate / rebuild+CRC / map / clean / submit+wait / invalidate+check /
commit / restore / total。`hw`包括提交、init、等待等，不是裸矩阵运算时间。
clean-bytes只计prepared池，tables为STLB，未包含init和MTLB的小额固定维护。
app-check-us为应用额外CRC/查找/有限值核验；search-us为C搜索。

chunk时间包含实际模型调用、校验、PROFILE读取、状态回灌和搜索；排除每块进度输出，
但命中打印仍在该块内。sum/chunks用于平均值，P50/P95使用nearest-rank。
总elapsed还包括冷加载、关闭及打印统计，不能把elapsed-cold与chunk-sum完全等同。

在电脑运行（输入仅上述六组主测试）：

```sh
python3 -X utf8 tools/npu-voice/audit-openvela-npukws-profile.py \
  out/a733-openvela-kws-v158-r2/application-expected.json /path/to/v159-serial.txt
```

工具逐场景核对结果、实际清理/重建字节及模型调用数、亲和性恢复、chunk统计。
`stride_budget_ratio = chunk_sum / (chunks*160ms)`；比值≤1且没有超预算块，只表示
这些固定文件在本次环境下满足该步进预算，还不包括真实采集/fbank/队列、长稳和热降频。
不要据此直接宣布实时语音助手完成。

## 开发复现

```sh
bash tools/test-a733-crc.sh
bash tools/test-a733-tensor.sh
bash tools/test-aipet-npuref.sh
python3 tools/test-aipet-npukws.py out/a733-openvela-kws-v158-r1
python3 tools/test-openvela-npukws-profile.py
bash tools/test-aipet-audio.sh
```

主机文件后端不运行NPU；CPU亲和性在主机测试中为可失败的桩，并逐条核验恢复。
build-a733-wapi.sh沿用全新v159目录及暂存/退出恢复流程；原v158目录不清理、不覆盖。

在仓库根目录的WSL shell中复现候选构建（SDK仍为原配套版本）：

```sh
A733_TENSOR_EXPERIMENTAL=1 \
A733_BUILD_DIR="$(realpath ../../quickly-openvela)/cmake_out/cubie-a7z_nsh_v159_kws_optimized_candidate" \
JOBS=8 bash tools/build-a733-wapi.sh
```

这个命令会重建指定的生成目录；不要把A733_BUILD_DIR改成源码或通过版本目录。
本次交付先完整构建，再以`--incremental`纳入最后防御修订；最终镜像只取第二次输出。
源码包保存脏工作区的实际文件与逐文件SHA，不把Git HEAD当作完整复现凭据。
