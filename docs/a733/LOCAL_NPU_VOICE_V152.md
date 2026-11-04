# v152：本地 NPU 语音迁移（前置阶段，不是全链路完成）

2026-10-19。依据用户指定的
[Radxa 离线语音助手](https://docs.radxa.com/cubie/a7z/app-dev/npu-dev/voice-assistant)
和它链接的 [参考仓库](https://github.com/Ronin-1124/cubie-a7a-voice-assistant)。
使用 embedded-npu-porting / embedded-soc-porting 的分级验证要求，保留当前基线。

## 项目目标

ICS43434 → 48k采集/16k转换 → 本地KWS → 本地Zipformer ASR → 桌宠路由/
官方Agent适配层/本地Qwen → Matcha CPU声学 → HiFi-GAN NPU → 22.05k转48k → MAX98357A。

“完全离线”必须显式禁止云ASR、云LLM、云TTS，且错误不能暗中回云端。
标准 openvela Agent 现有 LLM 路由并不自动获得离线 ASR/TTS；本轮没有偷换后端。
参考助手主要是识别后复述，不含本项目的完整 Agent、工具调用、LVGL 和家庭设备逻辑。

已核对本地官方 `voice_asr.h` / `voice_tts.h`：后续本地ASR应通过
`voice_asr_ops_t` / `voice_asr_register()` 接入；TTS可通过 `voice_tts_ops_t` /
`voice_tts_register()`，但其契约固定16k mono S16，必须真实转换22.05k输出，
或者在现有产品PCM适配层携带明确采样率，不能伪装为16k。本轮没有注册假后端，
也没有改掉默认云配置。KWS在进入Agent前管理唤醒/采集状态，不把partial识别当指令。

## 审查结果与硬阻塞

1. 参考是 Linux Python/NumPy/ONNX Runtime、ALSA、VIPLite `.so`、子进程组合；
   NuttX flat 固件不能直接运行 glibc ELF、Python subprocess 或 Linux驱动ioctl。
2. KWS float编码器39输入/39输出，固定输入 `[1,29,80]`，输出 `[1,4,320]`，
   包含卷积/注意力缓存及整数 processed_lens。必须保留真实类型、padding、布局并逐帧回灌。
   不能经FP32转换回灌整数状态。上游还修正过conv Slice以及int16 joiner溢出，
   KWS采用float三件套，不用词表263的KWS当成中文ASR。
3. 当前A7PM v3上限4输入/8输出/16区域/24MiB执行池，RUN只替换单个输入文件，
   输出仅CRC和短预览；不是可供语音解码的完整多张量驻留执行器。
   不通过盲目扩大数组/物理池绕过真实golden和内存布局验收。
4. 已下载的四个语音模型是NBG `0x00020000`、CID `0x1000003b`；现有
   MODEL_PROBE 仅接受 `0x00010020`。改成“接受头部”也不等于具备NBG linker。
5. 本地Model Zoo及2026-10-19从公开链接重新下载的包均为v0.9.0，
   缺少 `examples/zipformer` 和 `examples/tts`。文档描述v1.0.0但链接未提供它。
   需新版完整包/相关源码、ASR三个NBG及词表。不要索要用户网站密码，给目录即可。
6. 已另行下载Matcha ONNX/lexicon/词表，CPU运行时未移植。它仍使用CPU，不能声称所有TTS都在NPU。
   Baker训练数据标注非商用，比赛演示与量产权利分别核实。
7. HiFi-GAN 22.05k输出与底层8/16/24/48k整数倍播放不兼容。
   本轮在产品层增加显式转换接口，未改底层时钟。

文档称同一时间一个网络、需卸载；下载的仓库 `docs/STATUS.md` 与
`assistant_fast.py` 又描述/实现多网络常驻分时运行。两者不一致，不能将文档
概括当成硬件定律；第一版按串行单执行所有者处理，是否驻留须以实际内存和行为验证。

## 本轮实现

- 内容锁定模型清单和16个正/负语音WAV，`audit-a733-local-voice.py` 无推理、无网络、
  无硬件访问。返回2表示依赖/运行门槛仍不完整，不应改成“成功”让流程继续。
- Linux专用 `tools/npu-voice/voice-golden.c`：支持64路输入输出、查询真实形状/
  格式/量化/物理缓冲长度，精确加载 `input0.bin`...，保存全部 `outputN.bin`，
  验证CID、设置网络5秒超时，失败清理资源；输出目录必须不存在。
  零张量只做IO冒烟，不能替代语音模型语义、回灌或动态输入证据。
- `aipet audio play22k /data/matcha.wav`：严格有界PCM16/mono/22050 WAV解析，
  最长10秒，320/147分数采样率转换到48k后交给现有PCM API；不合成语音。
  使用已有应用音量，转换前后都在RAM，完成后擦除，不改云音频配置。
  不修改48k麦克风增益x5与UART路径，不启动常开录音。

## 复现（开发电脑/WSL）

从项目仓库目录：

```sh
python3 tools/audit-a733-local-voice.py \
  --reference ../references/cubie-a7a-voice-assistant-main \
  --zoo ../model-zoo-extracted/awnpu_model_zoo-v0.9.0-20260116-83a67d4b
# 当前预期退出2，openvela_voice_ready=false；不是模型损坏。
python3 tools/test-local-voice-audit.py
bash tools/test-npu-voice-golden.sh
bash tools/test-aipet-audio.sh
make -C tools/npu-voice
# 旧Bullseye用匹配sysroot另编译，不把较新glibc产物拿去误测：
make -C tools/npu-voice \
  SYSROOT="$PWD/../a733-viplite-golden/bullseye-sysroot" \
  OUT="$PWD/out/npu-voice-bullseye"
bash tools/build-a733-local-voice-v152.sh
```

构建脚本使用独立 `cubie-a7z_nsh_v152_local_voice_foundation`，SDK按原样恢复，
不覆盖v151。即便固件编译通过，也没有编入本地语音推理引擎。
本阶段不发布“全语音可测镜像”，不让用户为缺失后端反复烧录。

## 需要临时官方Linux环境的下一步（尚未执行）

先保留openvela SD，使用另一张卡启动官方Linux，避免覆盖唯一配置/恢复点。
必须有用户认可的切换时机。不能从WSL代替实际A733运行VIPLite。

取得新版Model Zoo后按官方文章构建KWS/ASR/TTS程序，先跑自带WAV验证整条参考链，
再分别在静音、噪声、不同唤醒词/命令下采集完整张量、golden结果和耗时。
不将Linux示例的推理RTF当成openvela端到端延迟或实时保证。

在安装了授权VIPLite运行库的Linux A733上，先做单子网检查：

```sh
export LD_LIBRARY_PATH=/path/to/authorized/A733/lib
export A733_VOICE_GOLDEN=/absolute/path/to/voice-golden-linux-aarch64
bash tools/npu-voice/run-reference.sh encoder_float_a733.nb --inspect
bash tools/npu-voice/run-reference.sh encoder_float_a733.nb --zero new-zero-output
bash tools/npu-voice/run-reference.sh encoder_float_a733.nb real-tensors-input-dir new-real-output
sha256sum new-real-output/output*.bin
```

包装器检查真实Linux/aarch64与设备节点，并在网络5秒之外加进程60秒总deadline/5秒强制终止。
它不检测其他客户端是否空闲，运行前需人工停止其他NPU应用。
raw输入是运行库所需tensor布局及完整padding，不是16k WAV，也不是未转置的mel。
实际缓存状态必须由参考推理导出，`--zero`不生成这些状态。
只运行一个进程，停掉其他NPU应用。若超时/驱动挂死，保存dmesg/trace并冷重启，
不要在未知状态继续跑后续阶段。该工具不是设备reset恢复证明。

## 后续必须按门槛推进

1. 官方Linux真实语音golden与许可；多输入/输出/内存峰值/状态回灌精确记录。
2. 语音prepared资源或可移植合法linker；兼容新模型的有界、持久多张量NPU API。
3. 每子网双输入动态输出证明、长句状态回灌、timeout/reset复用；回归YOLO。
4. 使用与golden一致的Kaldi fbank/endpoint/KWS图/Zipformer解码，先文件后麦克风。
5. Matcha CPU声学/文本规范化/词典、HiFi-GAN NPU与22.05k转换，先固定短句。
6. 接官方Agent/规则/本地Qwen与UI。显式 offline/auto/cloud策略，离线时计数证明
   云调用0；播放时停采，清空残留，冷却后重回唤醒，避免TTS自唤醒。

状态：工具与转换代码已实现；主机/交叉构建结果见证据目录。
本地NPU KWS、ASR、Matcha、声码器及Agent全链路均 **未完成/未上板**。

最终全量2958目标、布局和140路径SDK恢复已通过，详见
[构建记录与哈希](../evidence/local-voice-v152/build-summary.md)。没有发布新的整卡镜像。
