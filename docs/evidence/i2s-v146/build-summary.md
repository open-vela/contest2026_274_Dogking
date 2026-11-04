# v146 ICS43434 合并模块候选构建归档

日期：2026-10-15，Asia/Shanghai。
状态：**source-ready / host-tests-passed / build-passed / image-verified /
hardware-pending / acoustic-pending**。

没有通过 SSH 操作新模块，没有重刷物理 SD，没有执行 tone/capture，没有声学通过结论。
用户已确认是 MAX98357＋ICS43434 合并模块，但照片/最终接线尚待确认。

## 恢复点

蓝牙通过基线：`884c1825b095a65150245b9019bfcd2d056a259e`，恢复分支
`backup-before-ics43434-v146`。完整 bundle 与 v145 镜像保留，见
[暂停记录](../BLE_V145_PAUSE_FOR_ICS43434.md)。没有覆盖旧代码文档、录音或镜像。

本次构建源码：`03e788a0ace842cfd359cb8825ec4625de648b50`（见 source-commit.txt）。
后续 `9cb5911` 只添加主机分析工具/研究与测试文档，不改变本镜像所编译源码。
最终归档提交还更新构建包装器的换行检查提示和新增分析测试调用，同样不改变内核。

## 实际构建

- 独立目录：`quickly-openvela/cmake_out/cubie-a7z_nsh_v146_ics43434`。
- `bash tools/build-a733-i2s-v146.sh`：完整构建2955/2955目标，08:12。
- GCC13.4.0、CMake3.23.0、Ninja1.12.1；WSL Python3.14.4。
- CONFIG_A733_I2S0_AUDIO=y、8核SMP、ARM64_MMU_ASSERT保持开启。
- v145 BLE/GATT/AI Agent/LVGL配置保留，配置哈希与v145相同；不能据此称v146外设回归已通过。
- ELF的 text/rodata/RW/初始栈页对齐、加载地址、ARM64 Image header、BSS/栈预留
  和BL31 carveout检查全部通过；runtime footprint=36880384 bytes。
- GCC已实际编译 a733_i2s0_audio.c；BIN中已静态找到 v146、ICS43434、新命令和独立路径。

| 产物 | 字节 | SHA-256 |
| --- | ---: | --- |
| nuttx.bin | 2590296 | `6ece3d590816732f647798afa7420f19763e5500d90c0f89a2f9848d3623c887` |
| nuttx ELF | 3590264 | `6a51f9b653823efa77d21ea0563a7126259caad1a668c73c446c6cc6bb59293a` |
| build.config | 本目录保存实际配置 | `d6cf9a1177029504822433fc790f6e0704ee9ce339dd8373e72ad91e33190189` |
| SD候选镜像 | 2147483648 | `fece72b9666b3d3b99c6ec3831e26ae6962fa33404aa13e6db55217903b2eb1f` |

镜像位置（仓库旁）：
`A733-A7Z-ALL-Files/openvela-a733-cubie-a7z-sd-i2s-v146-ics43434-candidate.img`。
同目录有 `.img.sha256`。它是**可烧写候选**，不是已经上板验证的通过镜像。
整盘烧录覆盖SD已有数据，先备份模型、凭据和录音。

## 官方 SDK 恢复

所有永久源码修改均在比赛仓库 overlay/脚本内。构建前后129路径的内容/不存在状态与
a733home目录身份一致（HOME_ABSENT），`cmp`通过。原样保留SDK原先已有内容，
不清理不相关文件、不修改其Git元数据。清单见 official-before.txt、official-after.txt、
official-restore.log。

SDK原有若干 .git 文件仍不可解析；保留历史记录，不伪称读到了官方精确commit。
构建的Rust LLVM参数提示、build-id段被丢弃及build.sh早期路径grep提示属于当前SDK
既有行为，本轮实际以成功生成ELF/BIN、2955目标和布局检查为准，没有修改这些无关组件。
首次包装器只读清洁检查因Windows/WSL换行规范不同退出，发生在暂存SDK之前；
修正为单次命令的core.autocrlf设置后重跑，未改用户全局Git配置。

## 镜像校验

`bash tools/package-a733-i2s-v146.sh`：核对v145底图SHA后使用副本，仅替换分区3内核。
ext4在替换前/后以及最终镜像抽取时均干净；内嵌内核2590296字节，SHA与本次BIN一致。
GPT结果为 `No problems found`。沿用底图分区4未按2048-sector结束的提示，未改分区。
v145底图再次哈希核对通过，旧镜像没有被写入。

## 主机测试与限制

- a733-i2s-matrix-tests.log：旧8组参数、位隔离/恢复、PCM统计、64位平方和、报告分块。
- a733-ics43434-tests.log：实际C采集与write入口，双槽字节映射、两处通道数、
  左右顺序、mono选择、5秒样本数/模拟5100ms、WAV格式、raw、旧配置恢复、参数拒绝、
  短写与11种注入失败不替换旧文件、释放内存、报告格式；ASan/UBSan通过。
- analysis-tests.log：合成WAV分轨、恒值通道AC RMS=0、时长校验、输入不变、禁止覆盖。
- home-client / bttool-console / bttool-lifecycle / zblue-reopen / fat-filepath原有回归通过。

模拟MMIO不会证明芯片物理FIFO顺序、真实PLL时钟、杜邦线信号质量或麦克风健康。
5秒双声道WAV应为960044字节，但正确长度仍不等于真实声音。
最终通过要求：波形/试听能辨识语音、安静→说话→拍手有相应变化，且传输计数/时长无错。
首轮命令、引脚、电压边界及后续官方音频框架/ASR接入顺序见
[ICS43434说明](../../a733/ICS43434_V146.md)。
