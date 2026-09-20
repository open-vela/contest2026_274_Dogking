# AI 桌宠 openvela 优先架构与迁移准则

> 状态更新：v97 已通过本地 Qwen2 中文单/多轮用户测试。下文早期“生成尚未完成”的记录是历史阶段，不再代表最新状态。桌宠移植进度、行为对照和未完成依赖见 [Linux 桌宠移植阶段 1](a733/AIPET_LINUX_PORT_STAGE1.md)。

## 1. 不可变目标

AI 桌宠是运行在 openvela 上的产品应用，不是把 Linux Python 程序改写成一组 NSH
命令，也不是应用直接控制 NuttX 私有驱动。openvela 的内核本身基于 NuttX，因此镜像
出现 `NuttShell`、使用 POSIX 文件/线程/Socket 接口，不能据此判断“只适配了
NuttX”。判断标准是产品功能是否优先通过 openvela 已有的公共框架、服务和应用库
实现，并将芯片私有代码限制在 BSP/HAL 边界内。

本项目此后遵守以下顺序：

1. openvela 已提供公共框架或组件：直接使用，不另写平行实现；
2. openvela 提供标准设备模型、但缺少 A733 下半层：只补 A733/板级驱动；
3. openvela 完全没有所需能力：在比赛仓库实现可移植组件，并给出缺口证据；
4. `/dev/a733-*` 仅用于 bring-up、诊断与兼容，不得成为桌宠产品业务 API。

## 2. 强制分层

```text
AI 桌宠产品层
  对话状态机 / 人格与记忆 / 情绪 / 动作策略 / 视觉事件
                         │
openvela 公共能力层
  Media API / uORB / WAPI+netlib / libcurl / KVDB / UIkit / V4L2
                         │
设备标准接口与 vendor HAL
  audio / video / wlan0 / NPU runtime / GPIO-PWM-I2C-SPI
                         │
A733/Cubie A7Z BSP（允许自研的边界）
  FCU760K USB+固件 / VIP2 MMU+IRQ / A733 USB、时钟、复位、SDMMC
                         │
NuttX 实时内核（openvela 的内核基础）
```

产品层不得包含寄存器地址、USB vendor command、IRQ 号、物理地址、cache/MMU 操作，
也不得解析 `/dev/a733-wifi`、`/dev/a733-npu`、`/dev/a733-uvc` 的诊断文本。

## 3. 已核对的官方组件与采用决定

以下结论来自本项目使用的 `quickly-openvela` 源码，不是根据 Linux 名称猜测。

| 产品能力 | openvela 官方来源 | 项目采用方式 | 当前状态 |
| --- | --- | --- | --- |
| Wi-Fi 扫描与配置 | `apps/wireless/wapi`、`apps/netutils/netlib` | `wifi` 应用调用 WAPI/WEXT；底层只补 FCU760K 驱动 | 已完成并通过 v71 长稳测试 |
| DHCP/DNS/IPv4 | `apps/netutils/dhcpc`、netlib、系统网络栈 | 直接复用 | 已完成 |
| HTTP/HTTPS/API | `apps/netutils/libcurl4nx`、curl | 云端 LLM/ASR/TTS 备用链路统一走 libcurl | 基础 curl 已完成，业务客户端待接入 |
| 组件间事件 | `apps/system/uorb` 与系统 topics | 对话、ASR、LLM、TTS、视觉、动作间使用 topic；不靠 shell 串联 | 待产品化接入 |
| 配置与非敏感状态 | `frameworks/system/utils/kvdb` | Wi-Fi 策略、音量、人格、设备设置使用 KVDB | 待启用；密码/密钥不得明文入库 |
| 音频会话 | `frameworks/multimedia/media` 的 Media API、policy/player/recorder/trigger | 录音、播放、焦点和唤醒统一走 Media API | A733 音频下半层未完成 |
| 简化音频工具 | `apps/system/nxrecorder`、`nxplayer`、`apps/audioutils` | 仅用于底层验收，不作为最终对话编排器 | 待音频设备 |
| 摄像头 | openvela V4L2/video API、`apps/system/nxcamera` | UVC 成功后注册 `/dev/video0`，产品只使用 V4L2/Media 接口 | xHCI/UVC 尚未完成 |
| UI/表情 | `frameworks/graphics/uikit`（以及所选显示后端） | 表情、状态、二维码等由 UIkit 管理 | 显示驱动未完成 |
| 蓝牙 | `frameworks/connectivity/bluetooth` | FCU760K HCI transport 完成后接官方蓝牙 service/framework | HCI transport 待完成 |
| 本地 LLM | 官方树无可直接运行 Qwen2 GGUF 的完整 runtime | `aipetllm` 移植 GGML/ARM64 CPU 后端；保持独立 inference API | GGUF 张量验证完成，生成尚未完成 |
| A733 VIP2 NPU | 官方树无 A733 VIPLite/VIP2 驱动 | vendor BSP 实现硬件 runtime，上层只调用稳定 inference API | YOLOv8 真机推理基线完成 |
| ASR/TTS 模型 | 尚未发现可直接满足本项目中文离线模型的官方成品 | 优先接 Media API；推理后端按 CPU/VIP2 能力适配 | 待模型选型和执行链 |

## 4. Linux 原型到 openvela 的迁移映射

`A733-A7Z-ALL-Files/goal` 是需求和行为参考，不能原样搬入镜像。其 Python/Linux
依赖按下表迁移：

| Linux 原型 | openvela 实现 |
| --- | --- |
| `main.py`、`conversation.py` | C/C++ 产品状态机；模块间通过 uORB 事件解耦 |
| `pyaudio`、`vad.py` | Media Recorder/Trigger；必要时复用 SpeexDSP，仅补 A733 codec/I2S 下半层 |
| `requests` 云服务 | libcurl4nx + mbedTLS + 系统 DNS/NTP |
| `local_llm.py` 调 llama.cpp HTTP server | 进程内 `aipetllm` inference API，不依赖本机 Linux server |
| `tts_client.py` | Media API 播放；本地或云 TTS 都输出到统一音频会话 |
| `spi_screen.py` | 标准 SPI/LCD 下半层 + UIkit，不在产品层直接拼 SPI 命令 |
| `hardware/*.py` | 标准 GPIO/PWM/I2C 接口后的 actuator HAL，动作有白名单和限幅 |
| `network_monitor.py` | 网络状态 topic/标准 socket；不得轮询私有 Wi-Fi 诊断节点 |
| JSON 设置和环境变量 | KVDB + 安全凭据提供器；仓库不保存用户密码/API key |

## 5. 当前代码的真实性边界

- `apps/system/a733wifi` 已是 openvela WAPI 的板级用户界面，不再以私有字符设备完成
  正常扫描和连接。
- `apps/system/aipetllm` 通过 openvela 应用系统构建，当前使用标准 C/C++/POSIX 文件
  接口与移植的 GGML 量化代码；它尚未构成完整桌宠，也尚未生成 Qwen 文本。
- `vendor/allwinnertech/chips/a733` 内的 FCU760K、VIP2、USB、SDMMC 等实现属于官方
  缺失的芯片支持层。它们是 openvela 在新芯片上运行所必需的 NuttX 下半层，不是
  产品应用替代 openvela 服务的证据。
- NSH、`/dev/a733-*` 与 checkpoint 日志是开发和验收设施。最终演示可以保留，但桌宠
  的正常运行不得依赖人工执行这些命令。

## 6. 后续实施顺序和完成门槛

1. 完成本地 Qwen2 推理 API：模型加载、tokenizer、KV cache、采样、流式 token；
2. 新建桌宠 service，使用 uORB 定义音频、文本、情绪、视觉和动作消息；
3. 完成音频设备后接 Media Recorder/Player/Trigger，再接 VAD、ASR、TTS；
4. 完成 UVC `/dev/video0` 后，通过标准视频接口送入预处理和 VIP2 YOLOv8；
5. 接 UIkit 表情与状态界面，并用标准 GPIO/PWM/I2C actuator HAL 执行动作；
6. libcurl 只作为云端备用，断网时本地对话和基础动作仍可运行；
7. 完成开机 service 编排，NSH 只用于维护，不作为产品启动器。

每个功能必须同时满足：使用上述公共边界、可在开机服务中运行、真机日志可验证、
错误有超时和恢复路径、不会把密码或模型许可证受限内容提交到仓库。

## 7. 自动边界检查

`tools/check-openvela-first.sh` 在每次 `tools/build-a733.sh` 构建前运行。它会拒绝产品
应用直接包含除 `nuttx/config.h` 之外的 NuttX 私有头文件，也会拒绝产品应用直接使用
A733 私有诊断设备路径。第三方移植代码和 vendor BSP 不在此规则内，但必须保持在各自
目录中并保留来源与许可证。

这项检查不能证明某个功能已经完成，却能防止后续开发重新滑回“业务代码直接操纵
芯片私有节点”的结构。
