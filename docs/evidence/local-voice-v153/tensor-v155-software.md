# v155 多张量接口与四模型布局包：软件验证记录

日期：2026-10-19。保留原固件和全部历史记录；本记录不是实机验收。

## 恢复点

新增运行时之前保存到私有 `out/recovery-before-tensor-v155/`：

- `nuttx-v153.bin`：2643752 bytes，SHA-256
  `99e6ec93023ae92f3c917eb49a632d76ee78d9c68d3b94afaf457aa359fe94da`。
- `runtime-source.tar.gz`：SHA-256
  `24b720ea23070a686261ed0b9479057dae2f5c8b6400d24a6462354d19db1032`。

完成后重新核对SDK原 `cubie-a7z_nsh_v153_local_voice_frontend/nuttx.bin`，
仍为上述哈希。未替换、烧录或运行新固件。

## 本次软件验证

- `bash tools/test-a733-tensor.sh`：24组通过，ASan/UBSan；清单/状态8组、
  实际驱动适配器mock 10组、公共客户端mock 6组。
- `bash tools/test-vip-tensor-trace.sh`：有界自进程快照、不可读地址、
  独占创建/不覆盖和大小限制通过；未调用真实NPU。
- `python3 tools/test-voice-layout.py`：4个单测通过，含逐字节绑定、
  歧义/截断/损坏拒绝和四模型调度失败立即停止；未调用真实NPU。
- `bash tools/check-openvela-first.sh`：应用/BSP边界检查通过。
- 旧Python工具回归：`test-linux-voice-script.py` 5、`test-linux-joiner.py` 5、
  `test-linux-kws.py` 8、`test-voice-batch.py` 7，共25个通过。
- `bash tools/test-npu-voice-golden.sh`：39路和错误清理mock回归通过。
- Native Git `-c core.whitespace=cr-at-eol diff --check` 返回0；
  `.exports` 导出清单未被忽略。未提交或推送Git。

真实SDK生成配置及AArch64 GCC编译参数下，以下五个对象全部编译成功，日志无诊断：
旧运行时（扩展关闭）、新运行时（扩展开启）、清单解析器、公共客户端、KWS C搜索。
只编译目标对象，未链接镜像；没有暂存/改写SDK源文件。

复现命令：

```sh
python3 tools/compile-a733-tensor.py \
  ../../quickly-openvela/cmake_out/cubie-a7z_nsh_v153_local_voice_frontend \
  out/tensor-v155-compile-r2
```

输出目录必须是新的。现存完整命令/源码/对象哈希记录：
`out/tensor-v155-compile-r2/compile-summary.json`，SHA-256
`07e3f58dea887fbedda19eddd5488ff248a74afb7cd7f69f830899d45f02e2a8`。
基础 `.config` SHA-256：
`d6cf9a1177029504822433fc790f6e0704ee9ce339dd8373e72ad91e33190189`。

## 最终Linux采集包

私有文件：`out/a733-linux-voice-layout-v155-r2.tar.gz`。

- 大小：7914221 bytes。
- SHA-256：`d7511801d08c7fcd63fe6c9f17714df0310bd7c807d38a28b219eef3f33460c5`。
- 包内manifest、四组输入和固定参考输出哈希离线校验通过。
- 最终压缩包重新解压到新目录后再次离线校验通过。
- 新runner和追踪库交叉编译均启用 `-Wall -Wextra -Werror`；
  `readelf --version-info`显示GLIBC依赖仅2.17。
- 输入来自已通过 `voice-report-fXyxidmo`，不修改原报告。
- 保留原runner；新runner单独启用logical地址日志，追踪库只观察参考进程。

将包复制到仍运行官方Linux的A7Z后：

```sh
KIT=a733-linux-voice-layout-v155-r2
tar -xzf "$KIT.tar.gz"
cd "$KIT"
sudo bash test-linux-voice-layout.sh --run --npu-idle-confirmed
```

先退出其他NPU程序，保证至少2GiB空闲和供电散热。仅四个固定模型各一次，
不重跑KWS矩阵，不安装、不联网、不录音、不复位；结束回传 `REPORT:` 对应压缩包。
完整边界和失败行为见 [操作说明](../../../tools/npu-voice/LINUX_VOICE_LAYOUT.md)。

## 尚未通过的门槛

还没有本轮实际布局回传，也没有真实A7TV语音资产、动态输入/多块状态的
openvela NPU板测、ASR NBG或麦克风→ASR→Agent→TTS闭环。
四模型Linux布局采集通过也只允许进入任务链审查/导出阶段，不代表RTOS执行成功。
首版每次加载权重，尚不宣称实时性能。厂商库、模型及快照不上传Git。
