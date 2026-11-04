# v150：MAX98357A 有限 WAV 播放候选

后续实机更新（2026-10-16）：两次默认 ×5 录音、五次 48kHz mono WAV 回放，
50% 音量全部返回成功；用户确认拍手/说话清楚。音量已支持 0..100，默认25%，
重启不保留，影响下一次 WAV 播放而非播放中实时调节。
见 [实测记录及未验收范围](../evidence/I2S_SPEAKER_V150_RUNTIME_ACCEPTANCE.md)。
下面保留原候选交付时的状态，不代表当前仍未首次回放通过。

全量构建、镜像布局/GPT/ext4/内嵌内核SHA及130路径SDK恢复核对已通过，
候选镜像和合成测试音已生成，实机声音尚未验收。
镜像/源码/测试音SHA见 [交付归档](../evidence/i2s-v150/build-summary.md)。

2026-10-16。用户澄清本阶段要适配I2S扬声器，不是重新调试麦克风。
保留v147基础录音、v148镜像/标签、v149默认麦克风×5源码（57d7eee）。
本轮将已通过的MAX98357A S16测试音路径推进到真实PCM WAV文件播放。

## 接线与职责

继续使用已通过的合并模块/接线：PB5 pin12→BCLK/SCK，PB6 pin35→LRCLK/WS，
PB7 pin40→功放DIN，PB8 pin38→麦克风SD，GND共地，供电按原已验证模块方案。
DIN与麦克风SD不要短接。扬声器只接功放OUTP/OUTN（或SPK插口），任何一端都不接GND。
裸芯片与合并板的供电边界见既有ICS43434文档，不改供电或假定模块内部连接。

永久实现位于vendor板级诊断层a733_i2s0_playback.inc，保留UART4 TTS，
不把私有节点写入产品应用；本轮还不是标准audio lower-half、Media或官方云TTS接入。
后续应在证明发送/排空后，提供标准I2S异步队列/音频驱动，
再接官方voice_tts与既有PcmSpeechOutput接口，而不是让应用直接读写MMIO。

## 新入口与约束

```sh
echo volume=25 > /dev/a733-audio
echo play=/data/models/speaker-test-v150-16k.wav > /dev/a733-audio
cat /dev/a733-audio
```

- play只读/data/下的普通WAV文件，最大10秒；整段预读RAM并关闭文件，再启动TX。
- 仅PCM format1、S16LE、mono/stereo、8/16/24/48kHz；拒绝MP3、float、
  24/32-bit、44.1kHz等不支持格式，不改WAV头冒充48kHz。
- 硬件保持已验证48kHz、2×32-bit slot、S16 half-word FIFO写入。低采样率整数重复
  到48kHz（零阶保持，不移调）；这不是高质量带限重采样，后续可替换标准转换器。
- mono复制到两槽，stereo保留左右，不依赖功放选槽脚；前置64帧、末尾有界排空，
  不在TX运行时访问SD；同一锁保护录放，不支持全双工。
- volume=0..100为PCM数字音量，默认25%，只在本次开机保留；不影响麦克风×5、
  tone测试音或UART TTS。录音增益与播放音量不可混为一谈。
- 单次播放是同步且最多10秒加200ms/单次等待界限；另一个shell的off不能抢占同一锁，
  此阶段不是即时中断/低延迟连续流播放接口。
  播放时请等待命令自然结束；Ctrl+C/kill中途取消清理还未验证，不作为当前验收操作。
- 状态报告playback: status、plays、volume、源rate/channels、48k输出frames/sent、
  elapsed、原始INTSTA；原始TXCNT只报告，不猜测其帧/字比例来伪造通过。
- 中途TX欠载/溢出、时长异常、FIFO/排空超时会停止/释放RAM。最终有意排空后
  TXU可能置位，保留原始状态，不把尾端欠载扩大为整个文件失败。

Linux参考：本地bsp-product-aiot-stable/drivers/sound/platform/snd_sunxi_i2s.h
INTSTA TXU=6/TXO=5及FIFO布局；不将Linux驱动直接拷入RTOS。
启动仍为被动，不自动播放、录音、发起云请求或修改UART TTS默认链路。

## 主机验证

tools/test-a733-playback.py直接编译实际加载/发送函数，ASan/UBSan测试：
4采样率×2声道×3音量、负半波逐字节检查、mono复制、重复转换、先RAM后TX、
短读、RIFF奇数metadata/padding、5秒节拍、FIFO预填充/排空、
非法格式/长度/路径、打开/读取/关闭/seek/分配失败、供数/排空超时和清理。
曾由测试抓到有符号PCM与无符号音量相乘的错误，已明确转换S32修正并回归。
麦克风默认×5、原始接口、旧矩阵和错误文件保护继续回归。
这些仅为source/host证明，不是新WAV播放已实机发声。

## 构建交付

```sh
bash tools/build-a733-i2s-v150.sh
bash tools/package-a733-i2s-v150.sh
python3 tools/make-a733-speaker-fixture.py ../speaker-test-v150-16k.wav
```

独立构建目录cubie-a7z_nsh_v150_i2s_speaker；新镜像
openvela-a733-cubie-a7z-sd-i2s-v150-speaker-candidate.img。
底图只读核对v148 SHA，拒绝覆盖；新增播放include纳入SDK暂存/恢复清单。
候选日志/配置/镜像哈希在docs/evidence/i2s-v150/，没有实机结果前不称音质已通过。
合成测试音是工具生成，不含用户声音；原始语音不入Git。

## 上板测试

先备份SD上的模型、配置/凭据与录音，再烧录新候选镜像。
用现有FTP把speaker-test-v150-16k.wav上传到/data/models/。
初次播放远离耳朵，从低音量开始，不直接用100%测试未知大幅音乐。

```sh
cat /dev/a733-audio
echo init > /dev/a733-audio
echo volume=25 > /dev/a733-audio
time "echo play=/data/models/speaker-test-v150-16k.wav > /dev/a733-audio"
cat /dev/a733-audio

echo icsvoice=5000 > /dev/a733-audio
cat /dev/a733-audio
echo volume=50 > /dev/a733-audio
time "echo play=/data/ics43434-voice.wav > /dev/a733-audio"
cat /dev/a733-audio

echo volume=101 > /dev/a733-audio
echo play=/data/no-such-file.wav > /dev/a733-audio
echo volume=25 > /dev/a733-audio
echo play=/data/models/speaker-test-v150-16k.wav > /dev/a733-audio
echo off > /dev/a733-audio
```

合成音预期2秒、status=0、rate=16000/channels=1、frames=sent=96000；
录音回放预期5秒、rate=48000、frames=sent=240000，并可听懂自己的声音。
麦克风默认省略参数预期gain=5（旧v148仍是4）。
非法音量/缺失文件应报错，不改变已设音量/不启动播放；下一次合法播放应恢复。
请提供完整cat报告、实际播放时间、是否发声/失真/破音，而不只看status=0。
用户试听通过后再推进标准音频框架/云TTS；不在本阶段做ASR或改变Agent路由。
