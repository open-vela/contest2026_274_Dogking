# v163 前端数值与削顶定位：证据

2026-10-20。依 embedded-soc-porting 保留 v162 音频通过镜像、官方树恢复和驱动边界；
依 embedded-npu-porting 分开前处理、native golden、真实识别准确率。不 SSH、不烧录、
不自动采集语音、不联网、不改原 Linux 目录、不推送远端。无新增模型导入或转换。

## 代码范围

新增 `voice_frontend_audit.inc`，由 audio_app 调度；WAV/fbank 对照与原 KWS 共用生产
`kws_extract`，不复制另一套“测试特供”前端。原 FIR/KNF 数学和编译参数不改。
KNF 内部状态和降采样临时区现在在 NPU prepare 前释放；实际 feature 缓冲推理后清零。
没有据此宣称 NPU 加速或峰值内存实测降幅。

wavinfo 输出 PCM 电平与 rail-risk，fbankcheck 严格判字节并输出逐元素差异，
fbankexport 明确新建本地文件，禁止覆盖。没有 AGC/自动增益改动，没有阈值调整，
没有以小误差/候选文字相同替换严格 oracle。

本轮驱动两文件均逐字节保持 v162：

- `a733_i2s0_audio.c` SHA-256：
  `6508f532989f3d76b8aac3ae80b10dc8c08e60f6b2d3bdfac274662caab7a84a`。
- `a733_i2s0_playback.inc` SHA-256：
  `507a42d19087d92c97c1af5e653f4e65d53ffb43f10cf5f742d980cbb06e5f58`。

## 已完成主机/目标对象检查

- 新增实际 parser/KNF/导出/对照测试（ASan/UBSan）：六个参考 WAV 导出逐字节精确；
  finite 差异返回失败、NaN/长度/域错误拒绝，RMS/峰值/零/rail-run/CRC 独立常量断言。
  路径、最终 symlink、已有目标、FIFO/目录、短写/EINTR/零写/write/fsync/close/取消失败
  都保持目标保护/清理；恢复后可继续。测试断言录音与 NPU 调用数均 0。
- Python compare CLI：精确 exit0、差异 exit2、非法或已有报告 exit1；
  首帧/bin、RMSE、signed-zero 字节差异、NaN/Inf/幅度域、尺寸/覆盖保护测试通过。
- 实际 audio_app 的 48k WAV 诊断 dispatch、export/check、重复导出、损坏 config 独立入口，
  与原 record/asr/listen/TTS/UART/off/锁/取消/FIR 回归通过，无额外捕获/联网/播放。
- 实际 WAV→前端六组主机字节精确；原 fbank 三编译模式、严格+known dynamic FILE oracle
  六组、未知/NaN/取消负例通过。这些不是 ARM 实机数值或 native NPU 执行证明。
- v162 采集/播放/8 配置矩阵及 tensor/两池真实分配器/CRC/原 WAV auditor 回归均保持通过。
- 实际 ARM64 GCC 编译 audio_app 最大静态帧 688 字节、26 函数；4KiB 门限通过，
  该对象未单独链接成镜像，不将对象编译成功当板测。

原始日志位于 out/v163-frontend-audit-r2.log、v163-compare-test.log、v163-audio-r2.log、
v163-wave-r2.log、v163-fbank.log、v163-kws.log、v163-capture.log、v163-playback.log、
v163-matrix.log、v163-tensor.log、v163-crc.log、v163-wav-auditor.log；
目标对象记录为 out/audio-v163-target-r2/summary.json 与 v163-target-audio-r2.log。
最终补足 10 秒/1000 帧导出和越界输入回归，见 v163-frontend-audit-final.log；
生产源未因扩充测试而改变，旧测试记录保留。

## 数值边界

比较工具也用 v162 主机探针验证：unfused case0 与原参考精确，fused case0 有 6376/16800
元素字节不同，maxabs=0.00019359588623046875，RMSE=3.5659057328660193e-06；
对应比较返回 exit2，未升级为验收。ARM 工具链查询显示默认 -ffp-contract=fast，
但未证明它或 libm 是板端差异的根因；等实际板端完整 f32 后再做定位。
记录为 out/v163-compare-exact.json、v163-compare-fma-sensitivity.json、v163-target-optimizer-defaults.log。
旧 v162 实际 ARM64 对象反汇编也可见融合指令：feature-window 9、feature-functions 1、
mel-computations 12、fftsg 128，feature-fbank 0；有界摘录在
out/v163-arm64-existing-fused-audit.log。这只证明存在融合运算，不证明是全部差异根因。

## 构建过程与待测边界

初次 v163 在 CMake 配置阶段主动终止，原因是自审补上 FIFO 非阻塞打开保护；
未打包未交付。原日志 build-v163-frontend-audit.log 保留，退出后 149 路径官方 SDK
逐字节恢复（v163-sdk-restoration-config-stop.log）。最终采用独立 r2 构建目录，
当前生产源需以 v163-staged-sources-final.json 的 23 关键文件核验。

尚未执行 v163 板上 wavinfo/fbankcheck/export，未判断板端 maxabs/RMSE，
未验收真实录音削顶根因、唤醒准确率、连续流 ASR 或全本地语音助手。
完整步骤：[v163 流程](../../a733/LOCAL_VOICE_FRONTEND_V163.md)。

## 最终构建、镜像与封存

最终独立目录为 `quickly-openvela/cmake_out/cubie-a7z_nsh_v163_frontend_audit_r2_candidate`。
2972 目标完整构建通过；23 关键暂存文件匹配，结束后 149 个 SDK 路径逐字节恢复。
新构建目录的实际 ARM64 命令重编应用对象同样通过：最大静态帧 688 字节/26 函数，
见 out/audio-v163-final-build-target/summary.json、v163-target-final-build.log。
保留既有第三方 signedness、UNUSED、zblue 局部地址、build-id、配置器 LLVM/Rust 等警告，
不是零警告构建，未为隐藏警告修改官方 SDK。

- 内核 2692936 字节，runtime 36986880 字节；页/段/栈/Image 足迹检查通过。
  SHA-256：`ea6228e2e4e12b09575da35ebf35c8f52e3a8a54ea8b147155a2ccaf074cc335`。
- ELF 3740320 字节，SHA-256：
  `757d2a663ef9bf7074de165621ce3bb68113a2e006cd292d863be58f27d82a4e`。
- 独立镜像 `openvela-a733-cubie-a7z-sd-v163-frontend-audit-candidate.img`，2147483648 字节，
  SHA-256：`b02e966eb62d1701bd27b03be52e896b8b7bb1b01a4386a5eb2c4cb01e8ab92d`。
- 仅替换 v162 底板 partition3 中的内核；ext4 与内嵌内核哈希及 GPT 校验通过。
  前 348127232 字节、469762048 字节之后的全部内容与底板逐字节相同。
  原 GPT partition4 非 2048 扇区对齐提示保留，不重分区。v162 原镜像 SHA 仍为
  `8f67428298d6c764e73082636d8bcd7aa97db4cf708bf3bb7c250341a134ba80`。
- 415 文件源码包 `out/a733-v163-source-20261020.tar.gz`，SHA-256：
  `0c22bede3eaa0072e0aaea0cacc70a2f7e3ea51173f0c38b71d63769e25b5da1`。
  包含本轮及历史脏工作树实际 overlay/app/patches/tools，不含完整 SDK/模型/docs；不能只凭 HEAD 复现。
- `out/v163-candidate-20261020` 保留 Image/ELF/config/map、23 文件暂存记录、逐文件 source SHA、
  manifest，并另补全部当前历史 docs、本轮日志/目标对象 summary、README。
  manifest 是原始六个封存产物清单，不声称后补 docs/logs 也在原 artifacts 哈希里。

Windows 独立 SHA-256 已核对镜像/内核/ELF/源码包，415 当前源文件及 tar 内全部 415 成员
均匹配逐文件清单，429 个当前历史 docs 副本及原六个封存产物也逐字节/哈希核对通过。
manifest 所有 v163 hardware/boot/live-mic/asr/end-to-end 标志仍为 false，
不把旧 v162 听音通过状态自动继承给新镜像。
