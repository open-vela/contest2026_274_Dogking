# v185 本地通用语音闭环候选

后续板测补记（2026-10-27）：用户首次开机停在 `N4` 后的混杂多核横幅，
没有进入 NSH。本文件以下保留原开发/打包记录，不作为启动通过证据；
排查转入 [v188 启动加固与定位候选](BOOT_V188_CANDIDATE.md)。

2026-10-27。继承 v183 的真实 Linux ASR 证据、v184 通用后端与 CPU 声学执行器。
旧镜像、旧模型、开发记录、Git 标签全部保留；本轮不触碰板上 SD/UFS。

这是新候选，不是整机验收。**尚未证明板上完整语音成功，也未达到/测得 3–4 秒首音频。**
构建、镜像和实机结果分开记录，不能把主机数据当作 A733 时延。

## 真实实现

链路为 ICS43434 → 48k/16k FIR → 通用 Zipformer NPU（36/36 encoder、35 个状态、
6254 类 joiner）→ MiniCPM5 CPU LLM → Matcha CPU 声学 → VIP2 NPU vocoder → MAX98357 I2S。
不是旧关键词模型，也不是整套 TTS 跑 NPU。没有预置答复/预生成 WAV/云端降级。

`voice_tts_engine` 将真实 CPU mel 分为 `[80,200]`，不足部分用 -11.4 填充，
NPU 返回全部 51200 个 F32 样本后检查有限值，再转 22.05k 单声道 S16。
尾块按真实帧数裁掉 padding，流式重采样到 48k 播放。生产候选使用 noise=1、length=1；
noise=0 仅用于与独立原始 ONNX 逐元素比较的确定性测试。

文字先完整验证，不认识的非空白文字报错，不跳过语义；支持前端已有的中文、数字、
算式及词典读音，不声称是完整通用 TN/G2P。每句最多 127 IDs、整个合成调用最多 10 秒，
超限报错。流式播放的前缀无法撤回，后续错误会停止并返回失败，不伪称回滚。

CPU 模型/Session 可驻留 RAM，计算为单线程；NPU vocoder 在**当前调用 task group**
内打开、准备、执行、关闭，没有 fd 跨 NSH 命令保留。ORT Run 的监视器有 deadline，
不持有调用者栈或外部取消 cookie。Ctrl+C 使用现有阻塞信号/协作退出策略；
定向线程信号响应、ORT 跨命令复用仍必须板测，不能仅凭主机取消测试宣称通过。

首次安装只加载 CPU 资产，不录音，不执行 NPU。配置为 RAM-only，重启需重新安装。
这避免未验收的模型自动占用设备/录音；以后再做持久化和开机预热。

## 运行库与内存关卡

固定 ORT 1.23.2 / `a83fc4d58cb48eb68890dd689f94f28288cf2278`，只导入真实
NuttX/AArch64 静态库。CMake 检查每个归档 SHA256、C++17、mini localization、
wchar、无 RTTI/异常及 flat ABI。不链接 Linux .so/libdl，也没有伪造 mmap、时间或文件系统。

补齐的是实际 SDK libc++ filesystem 实现、真正 `gettimeofday/clock_gettime` 时间入口，
以及核心 Conv 所依赖的真实 activation helper。无异常 profile 的 uncaught count 为零，
该兼容源禁止在开启异常时编译。静态对象析构/线程和跨 task-group 生命周期必须继续审查；
当前候选 `CONFIG_LIBC_MAX_EXITFUNS=0`，真实 SDK 的 `atexit_register` 在该配置下不注册
任务退出析构，避免首个 NSH 命令退出时销毁 ORT 的静态环境。不能将本 profile 直接搬到
开启任务退出析构的配置；完整 Session 跨命令及 TLS 行为仍属于连续多轮板测关卡。

静态库改动后必须重建全部 ORT 核心归档及其声明依赖；只请求 session archive 曾留下旧库。
不能扩成无差别编译所有第三方测试工具。所有失败尝试在 ignored out 中保留。

`out/ort-link-probe-v185-r3`：完整 C API 强引用探测链接、布局检查均为 0，
file=6979280、runtime=91979776 字节。该 ELF **没有运行**，不是最终整合镜像。
80 MiB NPU arena/8 MiB I/O/64 MiB workspace 沿用 v184，有原始边界/重叠检查。
CPU 完整运行时调用栈为显式 256 KiB（主命令和逐句合成线程）；不能只用适配层的小栈帧
结果推断第三方运行库整个调用链安全。

## 已做的软件检查

- 五段中文：实际 C++ CPU 声学与独立原始 ONNX 相同；参见 v184 证据。
- `out/tts-engine-host-v185-r4`：CPU 实际推理，vocoder **明确是测试替身**；逐元素核对
  真实 mel 的 padding/布局，检查取消、NaN 尾部、NPU/sink 失败、清理、再次调用。
  不保存音频，不是 NPU/扬声器验收；这些主机微秒数没有目标机意义。
- `out/general-asr-host-v185-r1`：使用 Linux 的 216 次原生调用捕获重放，全部 35 状态反馈，
  两句中文及静音结果正确；重放不是本轮 NPU 执行。
- 既有 live-capture/I2S TX/RX 模拟回归通过，含取消、FIFO/计数错误、静音/端点与恢复。
- 最后审计修复等待失败时的完整所有权：不只保留队列，还保留 ASR、前端/echo、取消
  context 和下游 phrase 状态。返回 `-EOWNERDEAD`，应用层音频进入需重启的故障状态；
  不把所有权不明当成可正常恢复。录音、播放和 phrase 的等待失败注入测试通过；
  该分支的 RAM 保留是刻意隔离，不是“无泄漏恢复”，不自动重置硬件。
- `out/tts-engine-host-v185-r5` 另执行默认 noise=1 的真实 CPU 声学，验证有限 mel；
  不把这个检查当成自然音色/板上 vocoder 或扬声器验收。
- `out/voice-arm-v185-r1`：实际 NuttX/AArch64 参数下完整适配源编译通过。
  最终源又增加了绝对时间标记，最终固件构建仍是独立关卡。

## 可复现构建

在仓库根目录的 WSL/Linux 环境，按已有 v183/v184 工具准备锁定源码/依赖/模型后：

```sh
python3 tools/build-ort-compat-v185.py ../../quickly-openvela/cmake_out/cubie-a7z_nsh_v184_general_voice out/NEW-compat
python3 tools/npu-voice/run-ort-build-v184.py out/ort-nuttx-v184-r1 build out/NEW-ort-build --jobs 6
python3 tools/npu-voice/package-ort-static-v184.py out/ort-nuttx-v184-r1 out/NEW-ort-build-result.json out/NEW-static --nuttx-compat out/NEW-compat
python3 tools/probe-ort-link-v184.py ../../quickly-openvela/cmake_out/cubie-a7z_nsh_v184_general_voice out/NEW-static out/NEW-link-probe
bash tools/build-a733-voice-v185.sh out/NEW-full-build out/NEW-static
python3 tools/npu-voice/prepare-general-voice-v185.py out/NEW-private-assets
```

NEW 名字必须替换且不存在。构建脚本有独立 v185 目录，包装器保存/逐字节恢复 SDK；
不自动刷卡/格式化。需要读完整 v183/v184 说明获得原始资料和转换步骤，不能只复制这几行。

## 模型部署

最终 `out/general-voice-assets-v185-r2`：49 文件、137765328 字节，带哈希清单、
ORT 许可证及第三方声明（r1 的 47 文件初始包保留）；
模型为私有、非商业许可，未入 Git，不随公开源码发布。里面两目录放到：

```text
/data/npu/general-asr/   或 /ufs/npu/general-asr/
/data/npu/general-tts/   或 /ufs/npu/general-tts/
```

可以在电脑上将两个目录复制到 SD 的 data 分区 `npu/` 下，**不覆盖其他目录**。
UFS 使用已经通过的挂载/拷贝方式，本轮不修 UFS boot 残留，不格式化。
MiniCPM5 不包含在语音资产内，仍用既有 `/ufs/models/MiniCPM5-1B-Q4_K_M.gguf`
或 `/data/models/MiniCPM5-1B-Q4_K_M.gguf` 自动路径，加载后驻留 RAM。
若 UFS 没挂载，可沿用已通过的 `ufs status` / `ufs start` 流程，确认 ready=1；
失败就保留输出，不格式化、不清 bootloader。也可以直接用 SD 的 `models/` 路径。

## 板测顺序（必须使用最终 v185 候选固件，旧固件没有新命令）

先串口单窗口，先固定 5 秒，不同时启动其他 NPU/录放任务：

```text
npu info
npu selftest
free
aipet audio output i2s
aipet audio volume 50
aipet audio gain 5
aipet audio npuref asr-joiner /data/npu/general-asr/asr-joiner.a7tv /data/npu/general-asr/fixture/joiner
aipet audio npuref asr-decoder /data/npu/general-asr/asr-decoder.a7tv /data/npu/general-asr/fixture/decoder
aipet audio npuref asr-encoder /data/npu/general-asr/asr-encoder.a7tv /data/npu/general-asr/fixture/encoder
aipet audio general-asr /data/npu/general-asr
aipet audio general-stream 5000 0
aipet audio general-voice /data/npu/general-asr /data/npu/general-tts
aipet audio local-status
aipet audio local-speak-stream "你好，语音测试成功。"
aipet local context 3
aipet local batch 4
aipet local ask "你好"
aipet audio local-dialog-stream "请只用一句简短中文打招呼"
aipet audio general-listen 5000 0
```

任何一步失败先停止后续步骤，保留 `cat /dev/a733-audio`、`npu info`、`free`、`dmesg`。
UFS 部署时把上面模型目录前缀全部改为 `/ufs/npu/`。ASR 先说“现在几点了”“帮我打开灯”，
这里只验证转写/自然语言回复，**不执行 STM32 动作，不自动 arm**。

固定时长闭环成功后才尝试 `aipet audio general-listen 8000 400`。
每次都要等打印 **MIC ON** 再说话：此前可能仍在从存储打开/准备模型，尚未录音。
400 是诊断能量阈值，不是已验收 VAD；静音应报无语音而不是生成控制。
再测试多轮、Ctrl+C、错误后再次运行、输入/输出切换。完整 provider 安装后，
普通 Agent 的 I2S 输出也选本地 TTS；本地失败不重试云端。
`aipet audio general-asr off` 卸载候选；别用它作为“本地失败后继续云端”的恢复手段。

`ready=1` 仅表示两个软件回调已经安装，不是“硬件通过”。
日志 `mic-stopped-mono-us` → `first-driver-pull-mono-us` 可定位采集停止到驱动首取样的耗时，
包含 ASR backlog/收尾，不包含停止前的能量检测静音等待。
**驱动首取样不是耳朵听到首个有效语音**；3–4 秒目标还需真实结束说话及扬声器声学测量。

## 本轮未越界的事项

最终整合固件 `out/general-voice-build-v185-r3` 包装器为 0，256 路径 SDK 逐字节恢复；
独立最终 ELF 检查确认包含 CPU acoustic、NPU vocoder、通用 ASR 和所有权异常保护，
不是仅链接了早期探测入口。kernel=7007952 字节、runtime=92004352 字节，SHA256：
`1c6ce8f73bfb5def0dbe059ee9ad4b781f8df5b3613d171e4dba931b696b5d8e`。
镜像打包/开机/实机结果另计，不以这个链接结果冒充验收。

最终私有测试镜像已经打包并通过 kernel/GPT 校验、49 文件逐一回读：
`../openvela-a733-cubie-a7z-sd-v185-general-voice-candidate.img`，2147483648 字节。
SHA256：`fbcd67f55429c83550edd7c983b63f5862e7eccc8840de898b60aa6c62afdfd0`。
内置 `/data/npu/general-asr`、`/data/npu/general-tts` 及许可证，不含 MiniCPM5/密钥。
打包来源提交 `02a047be5c6d4a24c171e6824d166175647e2e02`；固件功能源码候选标签
`a733-v185-full-voice-candidate-20261027`。报告在 `out/general-voice-image-v185-r3`。
打包当时尚未开机测试，后续首次开机失败见文首补记。刷卡前备份 SD 数据；
本工具没有刷写任何物理磁盘，UFS 未改。
带 `kernel-only-incomplete` 的两份中间镜像是失败打包记录，**不要刷它们**。
2026-10-27 后续按用户清理要求删除这两份无用文件，保留摘要与完整 v185 镜像；
见 [v189 清理及语音修复记录](TTS_REBUFFER_V189.md)。
该镜像含私有/非商业模型，只供本地测试，不作为不受限固件公开分发。

未修板上 Linux/启动分区，未清 UFS bootloader，未迁移用户数据，未操作 STM32/摄像头/BLE，
未上传模型/密钥/厂商二进制。已通过 v182 镜像及各历史版本保持可恢复。
后续优先统一板测，依据阶段数据再优化 LLM prefill、ASR 热路径、TTS 首句和缓存。
