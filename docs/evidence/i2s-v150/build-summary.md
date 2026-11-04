# v150 MAX98357A WAV播放：候选镜像交付

后续用户实机已通过两次录音/五次48kHz mono WAV回放，50%音量，确认声音清楚；
见 [运行验收](../I2S_SPEAKER_V150_RUNTIME_ACCEPTANCE.md)。
以下为制作候选时的历史状态，镜像/源码/哈希不变；不扩大为其他采样率或长稳通过。

2026-10-16。用户明确本阶段适配I2S扬声器。状态：
**source-ready / host-tests-passed / build-passed / image-verified /
v150-boot-pending / wav-speaker-hardware-pending / stress-pending**。
没有SSH操作开发板、烧写SD或发起云请求；没有推送远端。

## 源码与恢复

- 编译源码89c8d5e966d5c08834470ae0756d032284c6f0c6（source-commit.txt）。
- 默认麦克风×5来源57d7eee，已合入本次内核。
- 源码恢复标签a733-before-i2s-speaker-v150→57d7eee，v148候选镜像/标签不覆盖，
  v147基础录音与历史失败证据保留。
- 只新增板级诊断WAV播放，没有改PLL/引脚/启动/SMP/中断架构或UART4 TTS路由。
- 按本地Linux头文件核对TXU/TXO与FIFO布局；实际S16半字发送复用已通过tone路径。
  标准audio lower-half/Media/官方云TTS尚未接入，不能称Agent→I2S播报已完成。

## 构建与产物

tools/build-a733-i2s-v150.sh：独立cubie-a7z_nsh_v150_i2s_speaker，全量2955/2955，
08:17；ARM64 GCC13.4.0/CMake3.23.0/Ninja1.12.1/WSL Python3.14.4。
主机实际C函数使用GCC15.2.0、ASan/UBSan。
MMU页/段、BSS/栈、Image头、0x40200000加载地址与BL31边界检查通过，
运行时预留36884480字节。内核字符串确认v150、play命令和默认麦克风x5。
配置与v148相同，8核、MMU断言、官方Agent、BLE/GATT和LVGL等保持。

| 产物 | 字节 | SHA-256 |
| --- | ---: | --- |
| nuttx.bin | 2594504 | c540fb0c5bb941bc35fd871e11eaf1bf08f7684e689e90259ff4563ca0c0ddf6 |
| nuttx ELF | 3595048 | 9be2f472367bf870de56272860091db061109ad7dff6dbdb4c29539b7c267c36 |
| build.config | 本目录实际配置 | d6cf9a1177029504822433fc790f6e0704ee9ce339dd8373e72ad91e33190189 |
| v150 SD镜像 | 2147483648 | 07d16f91575677063f79e63b2c5885ab7161a1c6c9f228e38d012f80575ae130 |
| 合成测试WAV | 64044 | 6eed6a79c0ac37c8b3962544d86b40d9b806ec222d0a744c6897d6980e8b8c9b |

仓库旁新镜像openvela-a733-cubie-a7z-sd-i2s-v150-speaker-candidate.img及.img.sha256；
同级speaker-test-v150-16k.wav为工具生成的440Hz/2秒/16kHz/mono S16测试音，
峰值约-12dBFS、100ms淡入淡出，不含用户声音。
所有原始用户语音留在仓库外，镜像大文件不提交Git。

tools/package-a733-i2s-v150.sh核对v148底图SHA=
1cd69296dd0d5b8a2d5d18cea1833b4030d018c6cf4da8b3f4c219467bf0a9bf，
只复制生成新镜像并替换分区3内核。ext4修改前/后/最终抽取均通过，
内嵌2594504字节内核与本次BIN SHA一致；GPT报告No problems found。
沿用底图分区4非2048-sector结束提醒，没有重排分区。

## 回归、恢复与边界

- 实际C WAV解析和发送：4采样率×mono/stereo×0/25/100%音量逐样本检查，
  S16负半波、mono双槽复制、整数重复、5秒节拍、预填充与排空、短读与odd chunk，
  非法格式/大小/路径、FS/内存错误、供数/排空超时与释放均通过。
- 开发期无符号音量导致负样本错误的缺陷被测试抓获，已转换有符号S32后回归。
- 麦克风默认×5与完整报告、原始接口、旧矩阵、13类录音失败保护继续通过；
  home协议/客户端、BLE生命周期/重开、FAT路径、分析工具合成测试通过。
- 新include纳入构建暂存：130路径+home身份前后cmp一致，恢复PASS/HOME_ABSENT。
  不声称整个官方SDK的无效Git元数据已修好。
- 保留既有Git描述/Rust参数/早期路径grep、ggml UNUSED、Zblue poller与build-id告警；
  不为清理告警改动无关组件。所有构建/测试日志完整保留。

本版是有界同步诊断播放（max10秒），不是DMA、异步队列、即时stop、全双工或
高质量带限重采样。测试时等待自然结束，Ctrl+C/kill的中途取消清理尚未验证。
正常失败会停止TX并释放RAM；不能据此声称强制杀任务同样安全。
TXCNT单位不作未经验证的硬判定，原始INTSTA/耗时/发送帧数用于后续板测定位。
播放成功状态不是模块存在/音质证明；冷启动/重复播放/热负载/共存与实机声音待测。

先备份SD模型/配置/凭据/录音，再烧录。测试音通过FTP放/data/models，
先25%音量播2秒测试音，再用默认×5录5秒、50%音量回放。
[完整接线/命令/预期](../../a733/I2S_SPEAKER_V150.md)。
