# v151 AIPET I2S 应用层候选交付

2026-10-16。状态：source-ready / host-tests-passed / build-passed /
image-verified；**v151-boot-pending / application-and-cloud-voice-hardware-pending /
stress-pending**。没有SSH操作开发板、烧录SD、真实云调用或远端推送。

## 恢复与源码

- v150用户实测通过镜像与标签 `a733-i2s-v150-wav-5play-pass` 保留。
- 最初全量源码 `df4339f94ee2f3181ef11742b2b2e8c8a43e68ad`，2957/2957目标通过，
  原日志 `build-full-df4339f.log`、`full-source-commit.txt` 和 `official-restore-full.log` 保留。
- 最终编译源码 `3a72a1f8f58da5edebe63de2e7bce02640170433`（source-commit.txt）：
  完整TTS解码不再走旧的部分成功路径；补SIGINT延后清理及取消后不上传/不播放的主机回归。
  同一独立输出目录最终复编1991/1991通过，`build.log`为最终日志。
- 没有修改PLL/引脚/启动/SMP架构/中断布局或UART4 TTS协议；原诊断命令仍保留。
  新RAM/ioctl包装复用已通过的采集与发送核心，不等于新路径已经实机通过。

## 构建与产物

入口 `tools/build-a733-i2s-v151.sh`（最终 `--incremental`），输出
`quickly-openvela/cmake_out/cubie-a7z_nsh_v151_audio_app`。
工具链沿用 ARM64 GCC13.4.0、CMake3.23.0、Ninja1.12.1、WSL Python3.14.4；
主机使用GCC15.2.0及ASan/UBSan。`.config`与v150一致：8核、flat、MMU断言、
官方Agent、BLE/GATT、LVGL保持；没有为了应用接入关闭其他子系统。

| 产物 | 字节 | SHA-256 |
| --- | ---: | --- |
| nuttx.bin | 2606888 | 6a23be9811d8c728b59f2658265506dbe5ffac123dc928e3404ff50d6c074f2c |
| nuttx ELF | 3632184 | b28612b08a8d2b46be611bd2a422a67868ae073ad14bf0fe5e3e8be2829fe862 |
| build.config | 本目录实际配置 | d6cf9a1177029504822433fc790f6e0704ee9ce339dd8373e72ad91e33190189 |
| v151 SD镜像 | 2147483648 | 436ad2fce1e88901b2c2d34f6a5f5d17da4ae4ef9317ab1463c4c39cd2669942 |

仓库旁镜像：`openvela-a733-cubie-a7z-sd-i2s-v151-aipet-audio-candidate.img`，
同名 `.img.sha256`；Windows独立Get-FileHash与WSL打包/验证哈希一致。
大镜像与用户原始声音不提交Git。

`package-a733-i2s-v151.sh`核对v150底图SHA
`07d16f91575677063f79e63b2c5885ab7161a1c6c9f228e38d012f80575ae130`，
复制为新镜像，仅替换分区3内核；ext4修改前/后/最终抽取通过，内嵌内核逐字节匹配。
GPT报告No problems found；沿用底图分区4非2048-sector结束警告，不重排分区。
Image头/段/页/栈/内存布局通过，运行时预留36896768字节。
内核字符串检查包含driver v151、ASR input=16000、隐藏语音配置、静音不请求云TTS、完整TTS字节报告。

## 上游恢复与软件回归

最终138个暂存路径和home目录身份前后核对一致，`official-before.txt` 与
`official-after.txt`完全相同，`official-restore.log`为PASS/HOME_ABSENT。
新代码永久保存在项目overlay/补丁，不留在官方SDK源树。
既有SDK gitfile元数据/host ABI探测、Rust LLVM参数、早期grep和已有编译警告保留，
最终构建退出0；不在本任务修改无关SDK问题。

本目录测试日志对应真实主机程序，不是硬件录音/云响应回放：

- 应用配置保存/重读、坏配置/残留临时文件保护、UART/I2S/off选择、音量0不请求云TTS。
- 本地录音不上传，ASR后走既有路由，I2S PCM输出；错误/UTF-8/超长PCM阻止后续阶段。
- SIGINT注入：录音取消不上传、合成取消不播放、清理后可再次操作；仍需板端验证，不是硬实时取消。
- 63-tap 48k→16k FIR：1kHz输出RMS7069.7、12kHz输出RMS7.8；不把此主机测量当声学标定。
- 官方TTS终止码/NDJSON/base64/精确容量/奇数字节/错误响应门禁；缺终止响应不得部分成功。
- 官方ASR补丁精确应用：CA强校验、时间与I/O deadline、最终结果门禁、接收终止字节空间。
- 原语音输出契约、路由/解析、AES-GCM旧密文及255字节token、PTY隐藏凭据/取消/超长恢复。
- 真实采集/发送函数、RAM/ioctl、四采样率×单双声道×音量、FIFO/计数/时间/文件错误清理回归。
- 原矩阵/分析工具、home协议100会话、bttool生命周期、Zblue重开、FAT路径等回归。

## 上板边界和入口

按 [v151测试说明](../../a733/AIPET_AUDIO_V151.md) 依次做本地录放→云ASR→云TTS→完整回合。
默认UART、应用音量50%、麦克风×5；音量/增益/输出选择保存到audio.conf，重启保留。
语音服务凭据与DeepSeek/MiMo的LLM凭据独立，必须在设备隐藏向导配置有效授权。
不后台录音或自动上传；显式record导出WAV，asr/listen只在RAM处理声音。
标准audio/Media、双工/AEC/唤醒词、连续低延迟流、所有负载下取消和长期共存尚未完成。
单次PCM/整句TTS最多10秒，kill/task_delete不支持；新整链路不能替代v150通过标签。
