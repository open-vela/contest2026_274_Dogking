# A733 语音模型 Linux 对照工具

2026-10-19后续：已新增独立的精确地址prepared导出、实际Linux回放、动态状态与完整KWS
测试工具；不使用通用NBG加载器。当前状态、构建/RTOS测试入口及边界见
[v156说明](../../docs/a733/LOCAL_NPU_PREPARED_V156.md)；以下保留原生对照阶段的历史说明。

仅用于官方 Linux A733/VIPLite 黄金输出采集。**不是 openvela 推理后端，不是语音助手**。
从本仓原有 `a733_viplite_golden.c` 的厂商API用法扩展，独立源文件，不覆盖旧工具。

```sh
make -C tools/npu-voice
# 目标Linux旧glibc时必须匹配sysroot（项目本地已有Bullseye sysroot）：
make -C tools/npu-voice \
  SYSROOT="$PWD/../a733-viplite-golden/bullseye-sysroot" \
  OUT="$PWD/out/npu-voice-bullseye"
bash tools/test-npu-voice-golden.sh
```

构建不复制厂商`.so`，不把它们打进Git/固件。目标板使用合法获得的匹配A733库：

```sh
export LD_LIBRARY_PATH=/your/authorized/A733/lib
export A733_VOICE_GOLDEN=/absolute/path/to/voice-golden-linux-aarch64
bash tools/npu-voice/run-reference.sh model.nb --inspect
bash tools/npu-voice/run-reference.sh model.nb --zero output-zero-new
bash tools/npu-voice/run-reference.sh model.nb input-tensors output-real-new
```

- 查询最多64路输入/输出的原生尺寸/类型/量化/缓冲字节，buffer总量限制128MiB。
- `input-tensors/input0.bin`等必须**每一路完整且精确匹配运行库缓冲长度**，包含必要padding。
  不隐式填缺失state，不接受WAV当mel，不猜测ONNX/VIP维度顺序，不自动转换INT/FP。
- `--zero`只为IO冒烟，不代表KWS/ASR正确；输出若不变化需要诊断，不能计入推理通过。
- 所有原生输出都写入全新目录，不截成32字节预览。失败可能留下部分诊断输出，
  必须看进程退出码/`reference-status`，不能只看文件存在。目录不复用，重试另取名。
- 网络超时5000ms，Linux包装器外层60s终止/5s强杀。若超时应保存日志并冷重启；
  用户态进程终止不保证内核/NPU恢复。禁止同时运行其他NPU应用。
- 原生API/头文件链接通过，39路与失败清理仅做**主机mock**验证。
  早期该工具仅有mock验证；2026-10-19已取得第一阶段实际A733运行记录（见下文）。
  默认WSL编译的glibc依赖不适合旧Bullseye，
  另编译的sysroot版已静态确认仅需GLIBC_2.17。

golden不只需静音：至少两组真实不同输入及其输入/全部输出hash、逐帧state反馈、
同输入重复结果、模型SHA256、库/驱动版本、各阶段耗时、温度与内存、失败恢复。
拿到这些后才能设计可验证的openvela多张量执行器，不能只修改A7PM上限就宣布支持语音。

2026-10-19 Linux 板测已通过4模型接口查询与两次 joiner 零输入重复性检查；
这不等于整个语音链路通过。[第一阶段脚本](LINUX_BOARD_CHECK.md)、
[第二阶段输入敏感性脚本及首测证据](LINUX_JOINER_CHECK.md)。

第二阶段随后实机通过：两路输入分别影响结果，原输入重复一致；
下一步的 [第三阶段真实语音 KWS 张量采集](LINUX_KWS_CHECK.md) 包含完整
38路状态回灌、明确的标量策略、首块全部输出重复检查和逐帧贪心音素诊断。
第三阶段仍待板测，不是官方关键词beam、中文ASR或openvela端本地语音通过。

第三阶段随后回传221次调用：218条推理的全张量与状态连接复核通过，但语音/静音
贪心均为空，未判识别通过。已准备 [一次执行的可用资产综合测试](LINUX_VOICE_ALL.md)：
24个KWS参数/正负样本场景、四模型真实暖态/重载、非法输入拒绝、CPU真实Matcha mel
到NPU声码器与WAV，保留ASR/RTOS未覆盖清单。
增强的 `voice-golden.c --repeat 1..20` 用单独的 `voice-repeat-linux-aarch64` 构建目标；
综合包继续使用原已通过的缓存单次二进制及其固定哈希，不覆盖或假称新基准程序已板测。

[项目迁移状态与门槛](../../docs/a733/LOCAL_NPU_VOICE_V152.md)。

## v154：有界 KWS 搜索回归

综合 Linux 回传已核对；两段声码器输出经用户试听确认内容正确。
短唤醒词漏检的搜索修正、可移植 C 核心、冻结编码器输出上的一次性分支补测见
[LINUX_KWS_SEARCH.md](LINUX_KWS_SEARCH.md)。保留历史记录；不是openvela完整离线语音通过。

v154随后回传3正例/3负例均通过，见项目证据目录的 `kws-search-v154-passed.md`。
v155新增64路公共传输接口，默认关闭。首次四模型命令/内存布局采集见
[LINUX_VOICE_LAYOUT.md](LINUX_VOICE_LAYOUT.md)，不是重复唤醒矩阵，也不生成猜测的prepared命令。
