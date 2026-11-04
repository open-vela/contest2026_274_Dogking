# v152 前置代码构建记录（不是本地语音全链路）

2026-10-19。固件源码提交 `835a7e26ec4b8a0f513f5ae1c7f6eff77d2e8e30`。
后续只补审查脚本/下载记录/文档，未改已编译应用源。

- `tools/build-a733-local-voice-v152.sh`：全量 **2958/2958通过**，08:20。
- 输出 `quickly-openvela/cmake_out/cubie-a7z_nsh_v152_local_voice_foundation`。
- GCC ARM64 13.4.0、CMake3.23.0、Ninja1.12.1；配置与v151相同，8核、
  flat、MMU断言、官方Agent、BLE/GATT、LVGL与原NPU功能均保留。
- 布局检查通过：file=2,610,984，runtime=36,900,864；Image/页/段/栈检查通过。
- SDK暂存 **140路径＋home目录身份** 前后哈希一致；`HOME_ABSENT`保持。
- 最终BIN静态确认包含 `aipet audio play22k` 和 `local WAV 22050 -> 48000`；
  **没有编入本地KWS/Zipformer ASR/Matcha/HiFi-GAN执行后端**。

| 产物 | bytes | SHA-256 |
| --- | ---: | --- |
| nuttx.bin | 2610984 | 16a6b9f898c7e0bdf33cf023897f5a10c9e4ebadb726ce5ba7e77af433605550 |
| nuttx ELF | 3636504 | c4b7165917d0503f3a7c302ba8f87988d1a26c19c3c522797d31091aaae52332 |
| build.config | 见文件 | d6cf9a1177029504822433fc790f6e0704ee9ce339dd8373e72ad91e33190189 |
| Linux Bullseye voice-golden | 本地out目录 | f283a60e59e41fccff5adfc53865fd38e21d6ab501e8f63b185dec00d81789f0 |

Windows独立Get-FileHash与WSL构建哈希一致。Linux工具交叉链接本地授权库，
静态ABI仅需GLIBC_2.17，**未在A733运行**，也不是固件的一部分。

## 已完成的主机验证

- ASan/UBSan：39路输入/输出绑定及query/flush/missing/short/existing-dir/
  timeout失败清理；WAV/S16/10秒上限/未知chunk/容量/格式拒绝；应用play22k无上传/合成调用。
- 22.05k→48k：1kHz RMS7071.39，RMSE0.42 S16单位；DC与1样本至10秒长度通过。
- 原应用配置、UART/off选择、音频取消、云语音mock、完整TTS响应验证保持通过。
- 原路由/加密配置PTY、I2S采集/播放/增益、蓝牙生命周期、home协议/FAT回归均通过。
- Python审查3项回归通过；真实参考模型/词表内容锁、16个PCM16/16k mono WAV检查通过。
  `openvela_voice_ready=false` 保持，是故意未放行的缺失后端，不是测试失败。

## 保留的环境诊断与未验证项

独立手工布局复查首次未设置ARM工具链PATH，出现 `aarch64-none-elf-nm: command not found`
和 `missing _image_start`。构建脚本内原检查已通过；手工检查显式设置A733_NM/
A733_READELF后也通过（layout.log）。未修改二进制以掩盖这次调用环境错误。
原SDK的gitfile/hostABI探测/Rust LLVM警告沿用，见完整build.log。

没有生成整卡镜像、没有烧录/SSH实机操作、没有Linux语音golden、没有真实云调用、
没有声学/唤醒/识别/合成/NPU并发或压力验收，没有远端推送。
旧v150/v151与全部历史不变。本阶段不能作为“离线全链路已完成”参赛证据。

接续需要含Zipformer/TTS的新版Model Zoo，以及用户安排的官方Linux A733对照时机；
技术门槛见 [迁移说明](../../a733/LOCAL_NPU_VOICE_V152.md)。
