# v148 左槽语音增益：构建和镜像交付

2026-10-16，用户恢复开发。状态：
**source-ready / host-tests-passed / build-passed / image-verified /
v148-boot-pending / v148-voice-hardware-pending / stress-pending**。

v147首次基础收音通过、左槽有效/右槽静默，是既有基线，不扩大为v148新模式通过。
本轮未SSH操作/录音、未烧写物理SD、未推送远端。用户原始语音/逻辑文件留在仓库外。

## 源码、历史与恢复

- 本次构建提交：bb772249343e59f108b076c5d0e9de954c1ccc16（source-commit.txt）。
- 录音功能代码提交：bff039e7efe212ce2f5695c95502565ca8efc3db；
  本轮不改驱动，仅修复构建包装器先检查干净源码、再更新已跟踪的证据文件。
- v147通过恢复点：a733-i2s-v147-basic-recording-pass → bf3d772，旧镜像未覆盖。
- 10月15日中断原始日志完整保存在77f8343提交的本目录；
  [暂停说明](PAUSED.md)保留，当前日志为10月16日重新全量构建的结果。
- 新增icsvoice左槽mono S16、默认×4/整数1..16可调、S16饱和与削顶计数。
  增益在停RX且原传输门禁通过后处理，不改引脚/时钟/极性/槽映射/原始路径/原始幅度。
- 版本化构建与封装脚本均在tools/，不把永久实现藏在官方SDK或生成目录。

## 构建与验证

```sh
bash tools/build-a733-i2s-v148.sh
bash tools/package-a733-i2s-v148.sh
```

独立目录 quickly-openvela/cmake_out/cubie-a7z_nsh_v148_ics43434_voice。
完整2955/2955目标，构建器报告08:10；ARM64 GCC13.4.0、CMake3.23.0、
Ninja1.12.1、WSL Python3.14.4。主机测试使用Ubuntu GCC15.2.0及ASan/UBSan。

页对齐、text/rodata/RW、BSS/每核栈预留、BL31边界、无Zblue孤立段、
Image头与0x40200000加载契约检查通过；运行时预留36884480字节。
内核字符串已确认v148、processing、icsvoice入口。
配置SHA与v147一致，保留8核、MMU断言、BLE/GATT、官方Agent和LVGL等配置。
配置中的Wi-Fi口令为空，未将板端密钥/凭据归档。

| 产物 | 字节 | SHA-256 |
| --- | ---: | --- |
| nuttx.bin | 2594464 | b6755950fb0d85868496b9e73db3f051d7241f1ac6f5278887277cf20e1198ef |
| nuttx ELF | 3594640 | c99edccb681f43326c296d76a24b93bb8c85ff61b37535f5759516b7bba7aec6 |
| build.config | 本目录实际配置 | d6cf9a1177029504822433fc790f6e0704ee9ce339dd8373e72ad91e33190189 |
| v148 SD镜像 | 2147483648 | 1cd69296dd0d5b8a2d5d18cea1833b4030d018c6cf4da8b3f4c219467bf0a9bf |

镜像在比赛仓库旁：
openvela-a733-cubie-a7z-sd-i2s-v148-ics43434-voice-candidate.img，
有同名.img.sha256校验文件。原始2GiB镜像不入Git。
封装前核对v147底图SHA=6cf150ee3ba8fb58014463ef9925496b80d7b47e92b333901f8ec96b50233b94，
复制到新文件，只替换分区3内核；不覆盖v147或修改分区布局。
ext4前/后/最终抽取检查均通过，内嵌2594464字节Image与本次BIN完全一致。
GPT报告No problems found，原有分区4非2048-sector结束提醒仍保留，不是新改动。

## 回归和官方SDK恢复

- 实际生产C函数ASan/UBSan：全部65536个S16值×16种增益（1048576组合）、
  正负饱和/精确削顶计数、mono头/路径/默认与自定义增益、严格拒绝非法参数、
  5秒240000个输出样本/六个帧计数点；原始命令恢复未放大幅度。
- 新模式13类传输/存储故障不覆盖旧文件，原始stereo/mono/raw、时长/计数门禁、
  旧矩阵、短写与文件提交保护继续通过。
- 分轨/逻辑合成、home协议/客户端、bttool控制台/生命周期、Zblue重开与FAT路径回归通过。
  所有这些主机结果不能证明新的电气、声音质量或无线压力通过。
- 129个暂存路径与home目录身份前后核对一致；official-before/after经过cmp，
  official-restore.log报告PASS和HOME_ABSENT。只声称这些暂存路径恢复，
  不声称已修好SDK既有无效.git元数据。
- SDK既有Git描述、Rust LLVM参数、早期defconfig路径grep与build-id告警保留在日志。
  编译另有ggml UNUSED重定义与Zblue poller局部地址告警，均保留原始输出，
  不为清除告警重构与本次录音后处理无关的组件。
  全量编译与链接确已完成，没有为清除告警顺带改无关组件。

Windows Get-FileHash独立复核镜像SHA与WSL封装/回读结果一致。
非日志文件diff空白检查通过；原始编译器日志中的尾随空白不做清理。

## 上板和下一步

先备份SD上的模型、配置/凭据和录音；整盘烧写会覆盖原卡内容。
优先录5秒默认×4，核对status=0/committed=1、applied=1/gain=4、
clipped计数、480044字节WAV并试听；必要时试×8，原始双槽接口继续回归。
[完整命令与预期](../../a733/ICS43434_V148_VOICE_GAIN.md)包含另存、负例和扬声器恢复测试。
新模式冷启动/重复录音/音量与噪声/共存仍待用户上板，不承诺“完美全功能通过”。
本轮不是AGC、降噪、连续流、DMA或标准audio lower-half，也未接入官方ASR/Agent。
