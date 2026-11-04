# v184 通用语音后端推进记录

2026-10-26；可恢复基线为 `a733-v183-linux-asr-checkpoint-20261026`。
本版目前是候选代码和离线验证，不是完整语音闭环验收。

## 数据是否够用

通用 ASR 已有两段真实语音、静音、35 个跨块状态、216 次原生调用和全部
601 个输出的 Linux/CPU 对照，可继续实现 openvela 后端，不必重做旧 KWS。
TTS 的中文前端、Matcha ONNX/ORT 等价检查及 NPU vocoder 数据已具备；
CPU 声学执行器尚需真正编译、链接和上板，不能以预生成 WAV 代替。

## 新的准备包（私有，不提交模型/厂商产物）

`tools/npu-voice/export-general-asr-v184.py` 只接受 v183 合格捕获归档和真实
输入/输出哈希。全部模型审计后才生成包，失败不发布半套产物。

|模型|输入/输出|文件字节|执行工作区字节|
|---|---:|---:|---:|
|encoder|36/36|47099200|47374336|
|decoder|1/1|7265024|7286784|
|joiner|2/1|6467712|6488064|

生成目录：`out/general-asr-export-v184-r2/packages`。
三包通过实际 RTOS plan/CRC 校验代码的主机 ASAN/UBSAN 检查，尚未在 RTOS 执行。

encoder 命令 byte 523504 出现 `4000ffff 00c7fcf8`，目标是下一条命令，
不是外部跳转。对照本地厂商 VIP2 `vip_drv_hardware.c:cmd_link` 的最大 fetch
编码，只在显式选项下接受同分配区、顺序、8-byte 对齐的 LINK；默认旧导出器
仍拒绝 LINK，任意跳转/回跳/改长度/越界仍拒绝。RTOS FE 已使用 `0001ffff`。

## 独立内存配置

`CONFIG_A733_NPU_GENERAL_ASR` 默认关闭且依赖 TENSOR_IO：

- 低 32 位有 guard 的真实 DMA arena 从 32 MiB 增至 80 MiB；
- tensor I/O 池 8 MiB，共享执行工作区 64 MiB；
- CPU-only resident 上限 96 MiB，包上限 64 MiB；
- MMU 格式、IRQ、时钟、模型原地址和硬件失败 quarantine 不变；
- 必须通过最终链接的 `0x48000000` 边界断言，不以 4 GiB 总内存替代该检查。

旧默认容量与模型别名保留。新诊断别名为 `asr-encoder/asr-decoder/asr-joiner`，
它们返回原生张量，不声称已完成转写。

## CPU TTS 构建

官方 ORT v1.23.2 固定 revision、Matcha basic-minimal 配置和全部依赖沿用 v183。
专用 CMake 补丁仅在裁剪 NuttX profile 下跳过无调用者的 host libiconv 搜索，
没有伪造或链接 Linux 库。初次真实交叉编译发现现有 libc++ 禁用了 stream 和
wide character；后续候选全 SDK 构建需同时启用 mini localization/CXX_WCHAR，
不能仅取消头文件保护而继续链接旧 libc++。

`tools/build-a733-general-v184.sh` 是独立全链接/ABI 候选构建入口，输出新目录；
所有临时 SDK 源文件仍由既有包装器还原并比对。尚不生成可烧写镜像。

## 当前验收边界

保持 v182/v183、旧镜像及历史过程文档不动。未写开发板、SD、UFS 或启动分区。
真实 ASR adapter/provider 和边录边识别代码已接入候选应用：
`general-asr <目录>` 显式安装 RAM-only 配置，`general-stream` 使用 I2S 采样、
63-tap FIR 降采样、consumer 线程和通用 NPU 后端；没有自动录音或动作发布。
provider 不跨命令保存 fd，所有 NPU client 都在有效任务组内创建、执行和关闭。
旧 `local-asr` 也可使用该候选 provider，但不是边录边识别入口。

`out/asr-backend-replay-v184-r2` 用实际生产 C++ frontend/backend、10ms 输入块和
216 次 Linux 原生捕获进行主机 mock 回放。全部 35 状态逐块接线得到两段正确中文
以及静音空文本；取消、NaN、未完成/失败结果不发布检查通过。这不是新 NPU 执行。
ARM64 对象/栈检查通过：ASR 最大静态栈 816 字节，provider 256 字节。

大内存/libc++ ABI 基础全包已链接：runtime=87805952 字节，低于 128 MiB 启动
bank 边界；SDK 248 路径逐文件还原。此基础构建尚不含后加入的 adapter，后续须重链。
CPU TTS 运行库已进入实际编译；Abseil 将 ELF 格式误认为具备 glibc 的 `link.h`，
新增 NuttX 专用条件排除 vDSO ELF 调试能力，而不是伪造 Linux 环境。
CPU 声学执行、NPU vocoder 联动、首句合成和整机回环尚待完成及上板。
3–4 秒首音频目标尚未测得，不能宣称达标。

## 第二检查点：CPU 声学模型实际执行与取消

新增 `voice_acoustic` 使用固定 v1.23.2 官方 ORT C API，直接持有通过 SHA256
溯源的 65033312-byte `.ort`，另以 CRC 防误传损坏。CRC 不提供认证。
单计算线程避免跨 NuttX task-group 保留 worker。原生输出严格限定 F32 `[1,80,T]`、
`T<=861`，成功前不发布 mel；每次 Run 由独立、有限监视器控制 deadline/停止标志。
监视器只持有自己的 RunOptions 和静态 getter，没有调用者栈/cookie/Session 指针。

`out/acoustic-capi-host-v184-r2` 实际执行了 C++ 前端＋CPU C API：五段中文得到
70/115/87/143/143 mel 帧；与独立原始 ONNX oracle 逐元素一致（本批次 max-abs=0）。
noise_scale=0 只用于等价测试，不能冒充生产音色设置。推理前取消、中途 ORT 终止、
超时和同 Session 恢复均通过主机测试。真实 ARM64 对象通过，最大静态栈 432 字节。

`out/general-build-v184-r2` 完整构建及包装器状态 0，254 路径逐字节还原。
ASR/provider 已进入最终 ELF，file=2825816，runtime=87814144 字节。
这不是开机/麦克风/NPU/扬声器的本轮硬件验收。

`out/ort-build-v184-newabi-r6-result.json` 的实际静态 runtime 依赖闭包编译成功，
`out/ort-static-v184-r1` 有 78 个归档、541 个 AArch64 ELF 对象，共 61127558 字节。
前一轮 `all` 失败是额外 int4 CLI 尝试链接 Linux `-ldl`；新入口明确只构建
`onnxruntime_session` 的真实依赖闭包，并未伪造 libdl。

`out/ort-link-probe-v184-r1` 是使用实际 SDK 链接命令的全 API 诊断，原固件未覆盖；
它尚未链接成功。需补真实 `EnvTime::Default`、文件系统实现、无异常计数，及
DISABLE_CONTRIB_OPS 下缺失的 activation helper。下一阶段必须先通过这个链接 gate，
再把 CPU 声学＋NPU vocoder 安装为完整 provider。不能把这些 `.a` 当成已经可用的固件。

官方 C API 头文件和 MIT LICENSE 可提交；全部模型、厂商产物、CPU 主机库和目标
静态库都留在私有 ignored `out`，不随 Git 提交。Baker 模型的非商业限制继续有效。
