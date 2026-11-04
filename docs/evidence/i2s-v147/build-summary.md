# v147 ICS43434 帧计数候选构建/交付

后续更新（2026-10-15）：用户已完成首次基础录音并确认有声音，WAV与串口统计一致，
24MHz波形支持配置时钟/槽数；小音量、无增益、长稳/框架/ASR待开发。
见 [运行验收记录](../ICS43434_V147_RUNTIME_ACCEPTANCE.md)。本文件以下内容为
原构建交付时记录，保留当时hardware-pending状态和全部哈希，不改写历史。

日期2026-10-15。状态：**source-ready / host-tests-passed / build-passed /
image-verified / v147-hardware-pending / acoustic-pending**。
v146用户实机日志与20kHz原始逻辑捕获已归档，不能作为v147上板或有效语音证据。
本轮助手没有SSH操作开发板、没有烧写SD、没有执行录音。

## 源码与恢复

- 编译源码提交：`2d3acdc49152af5bfc19eae882f3b7e102c4d5f2`（source-commit.txt）。
- 后续 `bf287ca` 及最终归档只添加证据/工具/文档，不改变已编译驱动。
- 恢复分支 `backup-before-ics43434-v147` → `8ee2578`，v146镜像保持原样。
- 完整bundle及SHA见 [候选说明](../../a733/ICS43434_V147_COUNTER.md)。
- BLE通过基线与所有历史I2S失败记录保留，没有推送或改动官方远端。

## 故障定位与修正边界

v146在文件创建前以软件FIFO字数489600与原始RXCNT244801比较，返回-EIO。
因此没有WAV不是file_open/存储卡失败，也不能据此认定麦克风坏。
实测与5.1秒×48kHz的帧数吻合；手册sample用语未明确帧/字，v147将帧单位视为
有实机数值依据的候选，**仍待高采样率时序、过程计数和声音验证**。
分开报告FIFO字和HW计数，warmup及每秒严格核对，保留完整时长/FIFO/原子提交门禁。
不自动接受任意计数倍率，不取消检查强行生成“通过录音”。

## 构建与静态校验

`bash tools/build-a733-i2s-v147.sh`：独立目录
`quickly-openvela/cmake_out/cubie-a7z_nsh_v147_ics43434_counter`。
完整2955/2955目标，08:18。GCC13.4.0/CMake3.23.0/Ninja1.12.1/WSL Python3.14.4。
v145/v146的8核SMP、MMU断言、BLE/GATT/官方Agent/LVGL配置保持，配置SHA相同。
ELF页对齐、加载地址、Image头、BSS/栈预留与BL31边界校验通过；运行时预留36880384字节。
BIN中已静态确认v147、frame-candidate、FIFO-words/HW与ICS命令。

| 产物 | 字节 | SHA-256 |
| --- | ---: | --- |
| nuttx.bin | 2590352 | `70ac9b0db7a5f5065b10a2f5474f58480fff8dc6e8861875fdee46ffac57e1b1` |
| nuttx ELF | 3590384 | `ed00a0c3ac77e5f1c4b1f83d4187c9fd28b942ceba1fe15290dc966d97faf79c` |
| build.config | 本目录实际配置 | `d6cf9a1177029504822433fc790f6e0704ee9ce339dd8373e72ad91e33190189` |
| SD候选镜像 | 2147483648 | `6cf150ee3ba8fb58014463ef9925496b80d7b47e92b333901f8ec96b50233b94` |

镜像在仓库旁：`openvela-a733-cubie-a7z-sd-i2s-v147-ics43434-counter-candidate.img`，
旁有.img.sha256。镜像可烧写但未上板，不是“麦克风已通过”的镜像。
整盘烧录前备份SD上的凭据、模型、录音；本轮未自动写任何物理介质。

## 官方SDK恢复及封装

129个暂存路径的SHA/不存在状态及a733home目录身份前后一致，cmp通过；HOME_ABSENT。
清单见 official-before.txt、official-after.txt、official-restore.log。永久变更仅在比赛仓库。
SDK既有无法解析的.git文件不改、不伪称精确官方commit已知。
既有Rust参数/早期路径grep/build-id警告保留，完整构建确已成功，没有顺带改无关组件。

封装 `bash tools/package-a733-i2s-v147.sh`：只读校验v146底图SHA、创建新副本，
仅替换分区3内核。ext4前/后/最终抽取均干净，内嵌2590352字节内核与本次BIN SHA一致。
GPT为No problems found，沿用底图分区4非2048-sector结束提示，未改分区布局。

## 测试与原始证据

- 实际C采集、write入口通过ASan/UBSan；用户489600/244801 fixture、六个过程点、
  错误倍率/停滞/巨大计数拒绝，mono/stereo/raw、WAV、短写/关闭/rename保护、无泄漏。
  原有11种错误加2种新计数错误，共13种注入失败均不覆盖旧文件。
- 旧矩阵、合成WAV分轨、home-client、bttool-console/lifecycle、zblue-reopen、FAT路径回归通过。
- 逻辑审计与合成测试：20kHz判为欠采样；24/50MHz仅通过采样率门禁，不伪称时序通过。
  数字chunk排序与输入不改通过；原文件SHA见logic-analysis.json和候选说明。
- 对归档.sr/.pvs设置精确路径-text属性，避免Windows Git换行转换破坏原始证据；
  `.pvs`的未过滤工作树blob与最终暂存blob一致，用户原文件不变。
- 用户原串口片段、.sr/.pvs均保留。没有PCM文件可试听，不据min/max或非零数宣称音质通过。

下一步：v147上板核对新报告、录音文件和六组计数；分析仪保持D0=LRCLK/D1=BCLK/D2=SD，
改24MHz以上（支持则50MHz），抓稳定录音中的10..50ms原始波形。
若正常保存，再分轨试听和比较安静/说话/拍手。Agent/云ASR仍在收音通过之后接入。
