# v148：左声道语音文件与可调数字增益

2026-10-15。在 v147 首次基础录音通过的基线上开发。
10月15日用户要求暂停的原始记录保留在 [暂停记录](../evidence/i2s-v148/PAUSED.md)。
10月16日恢复后，已完成全量2955目标构建、SDK恢复和2GiB候选镜像校验，
新语音模式仍待上板。镜像SHA与日志见 [交付记录](../evidence/i2s-v148/build-summary.md)。
恢复标签 a733-i2s-v147-basic-recording-pass，提交 bf3d772；
已验证 v147 镜像保持原名、原 SHA 不覆盖。
[v147实测记录](../evidence/ICS43434_V147_RUNTIME_ACCEPTANCE.md)包含音量与波形分析。

## 范围与边界

这是板级诊断节点的后处理入口，不冒充标准openvela audio lower-half，
不在本轮启动ASR/Agent、不改SMP/启动/PLL/引脚/极性/采样率/槽映射/采集节拍。
新功能在主机验证/构建后仍是候选，必须由用户上板试听验收。
不是AGC、降噪、混音或硬件麦克风增益，也不是连续流接口。

原命令 icscapture/icsleft/icsright/icsraw/capturecfg 及旧INMP441接口：
原路径、原幅度和计数门禁保留。原始声音文件不入Git、不上传远端。

## 新命令

```sh
echo icsvoice=5000 > /dev/a733-audio
echo icsvoice=5000,1 > /dev/a733-audio
echo icsvoice=5000,4 > /dev/a733-audio
echo icsvoice=5000,8 > /dev/a733-audio
```

格式 icsvoice=<ms>[,<gain>]：时长100..10000ms，整数倍增益1..16，省略则×4。
每次只对本次命令生效，没有全局/持久增益设置，不影响随后原始采集。
输出 /data/ics43434-voice.wav：48kHz、mono S16LE，固定选择用户已确认有效的左槽。
仍从硬件读取双槽，右槽仅丢弃、不与左槽平均；不同模块先用原始双槽接口确定有效槽。
5秒输出480044字节（44字节头＋240000个S16样本）。

处理顺序：原始双槽采集→软件选左槽/S24转S16→停RX→原有时长/FIFO/计数门禁→
RAM中有界S32乘法及S16饱和限幅→输出统计→临时文件完整写入/关闭→同盘rename。
失败不替换旧voice文件；不会改写stereo/left/right原始文件。
同一voice文件再次成功录制会替换，比较不同增益前请先另存。

诊断新增：

```text
processing: voice=1 applied=1 gain=4 clipped=0 input-min=... input-max=...; capture-stats/min/max describe output
```

applied=1仅表示后处理完成，文件是否成功必须同时看last/status=0与committed=1。
传输门禁失败则不应用增益；文件写入失败可出现applied=1、committed=0。
clipped为乘法后超过S16上下界而被钳位的样本数（不是正好等于边界的数）。
input-min/max是未放大S16；capture-stats、capture-min/max是最终输出。
gain=1应不改变幅度；原诊断捕获voice=0、applied=0、gain=1。

×4约+12dB，×8约+18dB。v147录音峰值2189，对该文件数学分析×4/×8不超量程，
但×16已有17个超量程样本。更近/更大声输入可能在×4/×8也削顶。
增益放大底噪，不提高固有SNR；本轮不掩盖低信号、静默槽或时钟错误。

## 构建与可恢复交付

```sh
bash tools/build-a733-i2s-v148.sh
bash tools/package-a733-i2s-v148.sh
```

独立构建目录 quickly-openvela/cmake_out/cubie-a7z_nsh_v148_ics43434_voice。
候选镜像 openvela-a733-cubie-a7z-sd-i2s-v148-ics43434-voice-candidate.img。
源底图必须为已验证v147，SHA必须匹配；拒绝覆盖输出文件。
构建封装日志及配置保存在 docs/evidence/i2s-v148/，实际结果另行归档。
官方SDK仅暂存overlay，构建结束必须核对129个路径及home目录身份恢复。
整盘烧录会覆盖SD，先备份模型、配置/凭据、录音；不自动烧写物理介质。

## 上板测试

1. 确认同一已通过接线，给模块可靠供电。串口115200 8N1，进入NSH。
2. 运行下列命令：录音期间正常距离说话，末尾拍手；默认先×4，不直接×16。

```sh
echo init > /dev/a733-audio
cat /dev/a733-audio
time "echo icsvoice=5000 > /dev/a733-audio"
cat /dev/a733-audio
ls -l /data/ics43434-voice.wav
cp /data/ics43434-voice.wav /data/models/v148-voice-g4.wav

time "echo icsvoice=5000,8 > /dev/a733-audio"
cat /dev/a733-audio
cp /data/ics43434-voice.wav /data/models/v148-voice-g8.wav

time "echo icsvoice=5000,1 > /dev/a733-audio"
cat /dev/a733-audio
cp /data/ics43434-voice.wav /data/models/v148-voice-g1.wav

time "echo icscapture=5000 > /dev/a733-audio"
cat /dev/a733-audio
ls -l /data/ics43434-stereo.wav

echo icsvoice=5000,17 > /dev/a733-audio
cat /dev/a733-audio
echo tone=1000,1000 > /dev/a733-audio
echo off > /dev/a733-audio
```

新语音模式预期：v148、slot=left、rx-channels=2/file-channels=1、samples=240000，
内部elapsed约5100ms、consumed=489600、HW约244801（门禁内）、
status=0/committed=1、处理增益符合命令、WAV恰好480044字节。
若clipped>0，减小增益；大声拍手可能使其非零，不以非零削顶计数冒充FIFO故障。
原始stereo预期960044字节、gain=1、voice=0；语音命令不会覆盖该路径。
非法17应报错且committed=0、不覆盖上一个成功文件；其余指标可能仍保留上一次值，
不能把旧指标当成失败命令的新录音。仅靠ls文件长度不足以证明文件未变，必要时下载比SHA。
文件可从现有FTP /data/models根目录下载；把完整日志、g1/g4/g8声音反馈一起返回。
三次是独立录音，不要求逐样本呈精确倍率关系。先确认声音和限幅，再规划官方ASR接入。

## 主机测试要求

tools/test-a733-ics43434.py提取实际C函数，通过ASan/UBSan运行：
16种增益×65536个S16值遍历（1048576组合）、正负饱和与精确clip计数、
mono WAV头/路径/时长、默认/自定义/非法参数、5秒六次计数、原始接口幅度恢复，
以及13种新模式传输/存储失败不覆盖旧文件。原有矩阵、原始双槽/raw和解析回归仍需通过。
这些主机结果不等于新镜像上板成功或音质验收。
