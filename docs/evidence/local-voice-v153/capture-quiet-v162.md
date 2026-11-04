# v162 录音功放 DIN 静音修复：证据

2026-10-20。使用embedded-soc-porting约束板级引脚/生命周期与可恢复构建；
embedded-npu-porting约束数值/硬件验收边界，不放宽oracle。不SSH、不采集真实声音、
不烧录、不改原Linux开发目录、不推送远端。旧v160/v161镜像和全部历史保留。

## 修改

仅音频板级驱动/播放include修改：PB7 GPIO latch先清零，再切function1，
录音前及stop后静音；PCM/WAV预填FIFO后恢复function3再启动TX，tone两种格式同样恢复。
初始化也先停serializer再改引脚。PB8与I2S时钟/采集格式/模型/NPU驱动不变。
诊断版本v162并增加PB7/data真实寄存器读回；默认gain仍x5。
GPIO输出顺序与同BSP既有header-peripherals实现一致，仅更新PB7字段/数据位。
这不是独立功放SD_MODE控制，不保证模拟电源噪声或硬件接线故障一定消除。
目标栈审查还发现诊断读取在报告缓冲加varargs后静态帧达4672字节。
改为现有g_i2s0.lock完整保护的静态报告缓冲，保持格式化到复制/返回都在锁内，
不新增堆分配或无锁共享；ARM64 driver最大帧降为1088字节（23函数）。
voice/audio应用对象最大帧分别1168/688字节（13/19函数），4KiB检查通过。
首次大栈失败日志保留，不放宽栈阈值；初次构建只作中间产物，最终另复编后才打包。

## 已通过的离线检查

- 实际生产采集函数+实际pinmux/quiet/stop，ASan/UBSan：RX开始前PB7必为GPIO1/data0，
  TX/SDO必须关闭，数据锁存必须先于output-mode设置；保持其他引脚与DAT位。
  实际ioctl、增益穷举、原采集错误/计数/文件提交等回归保持。
- 实际PCM/WAV播放核心及实际pinmux/quiet/stop：四采样率×单双声道×三音量、
  FIFO预填/排空、20次重复短播放、S16/S32真实tone/超时，TX启动时必须mux3，退出mux1/data0。
  错误/短读/分配失败/重入保持。物理静音及声学质量未在主机验证。
- 原矩阵8配置、16坏参数、MMIO隔离和64位统计/诊断分段读取通过。
- 实际应用音频配置/捕获mock、路由/TTS契约、取消/清理/FIR/重采样回归通过。
- 原严格与known动态六场景应用FILE oracle仍各1771调用、全部匹配；未知/NaN/取消负例保持。
- 三种fbank主机编译模式通过，六个实际WAV+实际fbank与主机参考仍逐字节一致。
  这不能代替板端WAV精确通过；v161已实际显示跨平台差异。
- tensor manifest/adapter/client/真实页分配器两池型及全扫描CRC回归通过，WAV auditor5项保持。

首次增强播放测试的PB_BASE=0模拟地址触发-Werror类型检查，已修改测试地址范围比较；
保留失败日志，不因它更改生产逻辑。

## 数值敏感性探针

`tools/npu-voice/audit-fbank-platform.py`使用同一归一化PCM和生产前端，CPU-only，
不执行NPU。-Os/-ffp-contract=off六组精确匹配；-mfma/-ffp-contract=fast前五组
有差异而静音不变，maxabs范围3.08e-5..1.94e-4，RMSE 2.65e-6..4.08e-6。
不能推导用户板上差异大小或判定准确率通过；本轮不修改模型/特征/阈值/编译参数。

原始输出：out/v162-capture-final.log、v162-playback-tone.log、v162-matrix.log、
v162-audio.log、v162-fbank.log、v162-kws.log、v162-wave.log、v162-wave-audit.log、
v162-tensor.log、v162-crc.log、v162-fbank-platform.log、fbank-platform-v162/summary.json。
静态报告缓冲最终回归为v162-capture-final-r2.log、v162-matrix-r2.log；
目标证据为v162-target-driver-r2.log、audio-driver-v162-target-r2/summary.json、
v162-target-voice.log及v162-target-audio.log。22关键暂存源必须与最终复编一致。

## 最终构建

独立目录`quickly-openvela/cmake_out/cubie-a7z_nsh_v162_capture_quiet_candidate`首次2972目标
构建完成后，在同一未交付目录补静态报告缓冲，最终2002依赖目标复编成功。
两次149路径SDK恢复检查均逐字节一致。最终22关键暂存源/头匹配当前仓库，
以`v162-staged-sources-final.json`为准，不能用初次暂存记录替代。
初次打包等待脚本到达等待上限时最终复编尚未结束，因此退出且没有打包；
确认最终复编/恢复成功后重新执行打包，不将中间内核交付。

最终内核2688840字节，runtime36982784字节，页/段/栈/Image占位检查通过。
内核SHA256：`508fe2ae361b245812f99b1dfdc1eed45afccedfa5d77f5ec9317b6e02802344`。
ELF3735528字节，SHA256：`ca744049e0916d58859626e47a49ebb435b3f6956f1d2c9ce08e08e3d5c8dd65`。
Windows独立核对两者一致。第三方既有签名比较、UNUSED/zblue/build-id等警告仍保留，
不是零警告构建；没有为压警告更改SDK。模型、API及输入输出CRC策略未改。

## 镜像与封存

新镜像`openvela-a733-cubie-a7z-sd-v162-capture-quiet-candidate.img`，2147483648字节，
SHA256：`8f67428298d6c764e73082636d8bcd7aa97db4cf708bf3bb7c250341a134ba80`。
Windows独立核对整卡哈希一致；仅替换v161底板partition3内核，ext4/GPT/内嵌内核检查通过。
前348127232字节和469762048之后的全部字节与v161逐字节相同，未重新分区。
原GPT partition4末端未2048扇区对齐提示保留。v160、v161旧镜像整卡SHA均未变。

410文件实际源码包`out/a733-v162-source-20261020.tar.gz`，SHA256：
`766ded079942debdef91360d9390839a6958ed52063c6a9edfb96351fd2885fe`。
它保留当前未提交的overlay/app/patches/tools，不含完整SDK、模型或docs，不能仅用HEAD复现。
`out/v162-candidate-20261020`保留Image/ELF/config/System.map、22文件暂存记录、逐文件源码SHA、
manifest，另补全部历史docs和本轮日志/目标summary。原始产物manifest不包含后补说明/日志。
manifest的hardware_executed、boot_passed、live_microphone、asr、end_to_end_voice_assistant均false。
本版是软件/构建/镜像校验通过的候选，仍待实机听音，不声明声学通过。
测试见[完整步骤](../../a733/I2S_CAPTURE_QUIET_V162.md)。
