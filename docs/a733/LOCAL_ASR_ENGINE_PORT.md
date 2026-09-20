# 本地流式 ASR 引擎移植记录

## 已推进

- 真实 Sherpa-ncnn C API 引擎适配器：`apps/system/aipetasr/sherpa_engine.{h,c}`（board overlay 下）。
- 七个模型资产非空/可读检查、线程范围 1..8、CPU 模式、80 维特征、16kHz 与 greedy_search 配置。
- PCM int16 → [-1,1] float、AcceptWaveform、IsReady/Decode 增量排空、partial/final 结果与销毁。
- 完成时按上游示例补 0.3 秒静音后 InputFinished；静音不计入真实录音样本统计。
- 解码步数上限为进度保护，不是墙钟超时。
- 结果缓存不足返回 ENOSPC，不静默截断 UTF-8。
- `asr_file_main.c` 为真实模型文件识别入口源码，明确接受 raw s16le/16kHz/mono，不假设所有 WAV 都是 44 字节头。

## 源码获取与冻结证据

Git clone 在 Windows/WSL 均连接重置；第一次归档下载超时，只作为失败文件保留，不能用于构建。
第二次官方 codeload 归档下载成功：`archives/local-asr/sherpa-source-download.tar.gz`。
SHA256：`b2acf660a34b6e65a9ed91459cb235a8da9a1da86d9aa5cc823d081f6eb96157`。
来源：<https://codeload.github.com/k2-fsa/sherpa-ncnn/tar.gz/refs/heads/master>。
归档 CMake 标示版本 2.1.15；这是按内容哈希保存的快照，尚未取得对应 Git commit，不宣称按提交锁定。
Windows tar 提取 Android/Kotlin 示例符号链接失败；已提取 native 文件可用于只读审查，但完整提取和构建须在 Linux 独立目录进行，不能忽略提取失败而称完整源码构建成功。

官方 C API 头文件下载至 `archives/local-asr/sherpa-c-api.h`：
SHA256 `acafb634bc1653f34c5061daaa4aa990b2ea3b611d49a3e5b9a0e7f220e850d3`。
所有源码归档和模型留在生成资产目录，尚未纳入源码 bundle；转交项目时须同时复制相关归档。

## 上游依赖审查

此次上游快照还编译 offline/TTS/VAD 等模块，不能把完整默认构建当成轻量流式方案。
上游依赖锁定信息：

| 依赖 | 版本/提交 | 上游预期归档 SHA256 |
| --- | --- | --- |
| ncnn | c4193aadbbb56582aa87b1850dd3d98fb8fd936d | da5563a86045d66ecf34820f82edec67906f988c61ecd3787f7e5df8bdfb43c0 |
| kaldi-native-fbank | 1.22.3 | 9176cc66fc7ce1edf85cf355b06e320c57db6297df74277f575183468893cf61 |
| kaldifst | 1.7.17 | c4b701a23a400bda8032586b02c7e0d5e813a765832df60c23e6df9e62b010f4 |
| openfst | sherpa-onnx-2024-06-19 | 5c98e82cc509c5618502dde4860b8ea04d843850ed57e6d6b590b644b268853d |

这些是源码声明的预期值，**依赖文件尚未下载/校验/移植**。流式识别子集是否能去掉 FST 等额外依赖仍需做引用和链接验证。
不要在官方环境中让 FetchContent 隐式下载并修改依赖；隔离目录准备、校验和构建，最终以本仓补丁/脚本接入。

## 测试结果与未完成项

真实官方头文件编译配合 mock 神经计算的 `tools/test-local-asr-sherpa.c` 通过；验证采样归一化、20ms 帧、尾静音、final、缺文件与对象释放，ASan/UBSan 通过。
第三方头文件 visibility 属性在 GCC typedef 上有警告，测试以系统头路径处理；项目源码仍启用 Wall/Wextra/Werror，没有改动官方头文件。
**没有加载真实模型，没有完整构建推理依赖，没有 ARM64/openvela 识别结果，没有新镜像。**
文件入口尚未注册为可运行命令。下一步：完整 Linux 提取 → 隔离依赖构建 → 中文流式模型主机对照 → openvela 交叉编译/ABI 修补 → PCM 板测 → 官方 Media/USB 音频接入。

上游 API/示例：
- <https://github.com/k2-fsa/sherpa-ncnn/blob/master/sherpa-ncnn/c-api/c-api.h>
- <https://github.com/k2-fsa/sherpa-ncnn/blob/master/c-api-examples/decode-file-c-api.c>
