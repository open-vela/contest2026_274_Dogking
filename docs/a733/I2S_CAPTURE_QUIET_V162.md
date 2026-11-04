# v162：录音期间功放 DIN 保持低电平

## 修复范围

用户确认kwsmic后两次的电流声发生在录音阶段。v161只开I2S RX，但功放与麦克风
共用BCLK/LRCLK；仅关闭TX/SDO没有保证功放DIN引脚电平。此前无独立SD_MODE控制。
这是一处明确的软件缺口，但是否为全部噪声来源仍须实机确认。

新驱动v162在录音/空闲阶段，将PB7（功放DIN，物理pin40）的GPIO数据锁存先清零，
再切GPIO输出function1。不会把麦克风PB8/SD接地，也不改PB5/BCLK、PB6/LRCK、
PB8/DIN0时序、PLL、RX通道映射、增益x5或48k/24bit/32slot采集参数。
显式WAV/PCM播放预填FIFO后恢复PB7 function3，再启动TX；tone/tone32同样恢复。
正常结束、超时等已启动流的退出路径停止I2S后重新拉低PB7。
另外将诊断报告缓冲移到现有互斥锁保护的静态区，降低cat调用者栈压力。

这是**数据线静音**，不是MAX98357硬件shutdown；没有虚构控制模块未引出的SD_MODE。
模块标注SD是麦克风数据输出，不要改接地。软件output=uart/off和volume=0不等于硬件关闭功放。
依据[厂家手册](https://www.analog.com/media/en/technical-documentation/data-sheets/MAX98357A-MAX98357B.pdf)：
功放SD_MODE用于shutdown，移除BCLK会进入standby。录音必须保留共用时钟，故先固定DIN=0。

## 镜像与保留

仓库上一级`openvela-a733-cubie-a7z-sd-v162-capture-quiet-candidate.img`为新候选。
镜像2147483648字节，SHA256：
`8f67428298d6c764e73082636d8bcd7aa97db4cf708bf3bb7c250341a134ba80`。
v160通过版本、v161复现镜像、模型包与全部历史记录保留，不覆盖。
**整卡烧录覆盖整个/data，先备份模型、音频、网络/SSH/Agent配置及凭据。**
模型与WAV包沿用v158-r2和v161-r2，无需重新转换或下载。
新候选未上板，不能预先声明电流声已经实际消除。

## 上板顺序：先无 NPU 录音，再播放，再 KWS

恢复原模型/配置后，先只验证采集本身，避免混入NPU输入校验问题：

```text
aipet audio status
aipet audio gain 5
cat /dev/a733-audio
time "aipet audio record 2000"
cat /dev/a733-audio
time "aipet audio record 2000"
cat /dev/a733-audio
time "aipet audio record 2000"
cat /dev/a733-audio
time "aipet audio record 2000"
cat /dev/a733-audio
```

record会替换`/data/aipet-record.wav`，要留旧文件请先备份。说几句话用于播放复核。
录音期间及结束后，扬声器都不应主动输出声音。cat中应显示driver v162；
首次录音前若initialized=0，只检查版本，不按未初始化的引脚值判失败。
完成录音后，空闲PB7 mux字段（cfg0的最高4位）应为1，data的bit7应为0，ctl=0。
PB5/6/8仍是I2S function3。旧M0 v141-layout启动标识无需更改，不用它判定音频驱动版本。

然后确认播放没有被静音修复破坏，首次用低音量：

```text
aipet audio volume 25
aipet audio play /data/aipet-record.wav
cat /dev/a733-audio
time "aipet audio record 2000"
aipet audio play /data/aipet-record.wav
cat /dev/a733-audio
```

应能听到自己的声音；播放完恢复PB7 mux1/data bit7=0。上面显式play固定走I2S，
不需要切换Agent的output配置，也不需要云端或联网。如果录音电流声仍在，停止反复测试，
回传cat完整输出及PB7/DIN、BCLK、LRCLK逻辑分析仪抓取，不根据源码推断实测电平。

最后才测本地KWS录音链路，连续四次，第一遍安静，之后分别说唤醒词：

```text
npu info
free
time "aipet audio kwsmic /data/npu/voice 5000"
cat /dev/a733-audio
npu info
free
time "aipet audio kwsmic /data/npu/voice 5000"
cat /dev/a733-audio
npu info
free
time "aipet audio kwsmic /data/npu/voice 5000"
cat /dev/a733-audio
npu info
free
time "aipet audio kwsmic /data/npu/voice 5000"
cat /dev/a733-audio
npu info
free
```

不主动播放、不上传、不触发Agent/设备动作。仍是先录后推理，未知声音候选不是ASR验收。
NPU专用池应恢复1654784字节，堆不应持续增长。

## WAV 数值问题单独跟踪，不假装本次已修复

v161用户WAV0–4的特征CRC不同，WAV5静音一致；预生成features0入口完全一致。
本轮主机数值探针对真实前端使用-Os：禁止融合时六组精确一致，允许FMA时前五组变化，
最大绝对差约1.94e-4、RMSE约2.65e-6到4.08e-6，静音不变。
这只是浮点敏感性证据，FMA主机CRC并不等于用户ARM板CRC，不能据此确定全部板端根因。
还需目标特征导出、工具链libm/FMA对照和误差分析。本版不改第三方算法/编译参数，
不放宽旧npukws精确oracle，不把unknown调用改标known，也不更改识别阈值或模型。

因此本次先以“采集无电流声＋播放正常”验收；六WAV整体golden仍未通过。
参见[v161原始板测分析](../evidence/local-voice-v153/openvela-v161-runtime-partial.md)。

## 复现与软件证据

```sh
python3 tools/test-a733-ics43434.py
python3 tools/test-a733-playback.py
python3 tools/test-a733-i2s-matrix.py
bash tools/test-aipet-audio.sh
python3 tools/test-aipet-npukws.py out/a733-openvela-kws-v158-r1
A733_TENSOR_EXPERIMENTAL=1 \
A733_BUILD_DIR="$(realpath ../../quickly-openvela)/cmake_out/cubie-a7z_nsh_v162_capture_quiet_candidate" \
JOBS=8 bash tools/build-a733-wapi.sh
```

构建目录不可指向旧通过版本。详细封存见[v162证据](../evidence/local-voice-v153/capture-quiet-v162.md)。
