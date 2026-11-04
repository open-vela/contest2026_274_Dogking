# v153 构建与资料验证（不是 NPU 全语音验收）

2026-10-19。已编译应用源码提交 `ed136b6705afd1bc71a73e172218b0814bfd8c73`。
后续新增源模型准备/审查工具和文档，不改变本次编译的应用源码。

- 第一次完整构建失败：厂商调试文本依赖 iostream，当前 libc++ 未启用 localization。
  `attempt1/` 保留原日志/源码提交与161路径SDK恢复记录。
- 修正后 `bash tools/build-a733-local-voice-v153.sh --incremental` 退出0。
  Ninja 本轮2012目标，最终生成ELF/BIN/System.map，布局检查通过。
- 没有打开系统locale/iostream、exceptions、RTTI；NuttX选stdio日志，
  Fbank算法仍与主机共享，未改NPU MMU/内存池/内核。
- SDK **163路径＋home目录身份** 前后哈希一致，`HOME_ABSENT`保持。
- BIN字符串确认编入 `local-frontend: rate=16000`、`CPU Kaldi fbank only`；
  没有本地 KWS/ASR/Matcha/HiFi-GAN 推理后端，也没有云端替代调用。

输出：`quickly-openvela/cmake_out/cubie-a7z_nsh_v153_local_voice_frontend/`。

| 产物 | 字节数 | SHA-256 |
| --- | ---: | --- |
| nuttx.bin | 2643752 | 99e6ec93023ae92f3c917eb49a632d76ee78d9c68d3b94afaf457aa359fe94da |
| nuttx ELF | 3680144 | f3fc19270e870f1961309d1cac303e394f4ccf19d122958704a1f3864f58c62f |
| .config | 见配置文件 | d6cf9a1177029504822433fc790f6e0704ee9ce339dd8373e72ad91e33190189 |

ARM64 Image静态运行占用36933632字节，比v152增加32768字节；这不含每次
命令的动态PCM/特征堆峰值，不能拿来当总内存实测。配置哈希与v152相同。
GCC13.4.0；原SDK gitfile、hostABI、Rust配置、丢弃build-id警告及新增第三方
signed/unsigned比较警告均保留于日志，不声明零警告。
独立手工布局复核通过；其后字符串检查首次因WSL未安装rg失败，
改用Windows已有rg后确认两处前端标记（artifact-checks.log），未改二进制。
原始build.log含工具自身输出的行尾空白，归档时不格式化；源码/文档补丁
排除原始日志后通过git whitespace检查，保留失败输出而不是为检查删改证据。

## 验证范围

- 三种主机模式：ASan/UBSan、无exceptions/RTTI、无iostream+无exceptions/RTTI+ASan/UBSan。
  真实Kaldi DSP、分块/整段精确一致、参考类幅度契约、静音、不同频率、1样本至10秒、
  完成/容量/越界/时长检查通过。不是独立NPU golden。
- 音频应用record/play/play22k/云ASR与TTS mock/路由、未初始化features无上传、
  Ctrl+C后恢复、导出文件不被features覆盖、完整TTS验证通过。
- 既有I2S、增益、WAV播放、蓝牙CLI/生命周期、home客户端、FAT等回归通过。
- 资料审查4项、下载工具4项测试通过；所有模型文件内容哈希独立复核。
- 新版Zoo原始包哈希见v153文档。另备三份固定上游ONNX共115187227字节，
  encoder36入36出/103×80/INT64缓存、decoder INT64 token、joiner6254输出
  与校准文件静态接口已记录。最初按直接输入名寻找decoder y.npy未找到，
  原报告保留为`asr-source-static-audit-direct-names.json`；查阅厂商config后确认
  decoder采用TEXT列表10个NPY，最终报告已纠正，没有伪造/补写校准数据。

## 未完成与恢复

未转换ASR NBG、未在Linux/A733运行golden、未改造openvela完整多张量执行ABI，
未完成端点/唤醒/ASR/Matcha CPU运行时及离线应用全链路。
没有生成整卡镜像、烧录、SSH板测、真实云请求、声学或长期压力验收、远端推送。
原v150/v151镜像与全部历史保留，不建议为尚无推理后端的版本反复烧录。
后续需要用户安排另一张SD启动官方Linux的对照时机；具体门槛见
[v153说明](../../a733/LOCAL_NPU_VOICE_V153.md)。
