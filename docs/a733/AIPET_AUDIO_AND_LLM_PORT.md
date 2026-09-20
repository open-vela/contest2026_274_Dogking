# 桌宠音频与 LLM 接口移植进度

## 顺序约束

保留规则路由与云端/本地共存，云端路线在无网络时降级本地，云端请求失败也降级本地。
先完成 Linux 版功能：USB 麦克风、原云端 ASR、UART TW-TTS、动作与屏幕。
I2S 麦克风/扬声器属于后续新增，当前不启用。

## 用户确认的硬件信息

- USB 麦克风型号未知，是用于树莓派的设备；用户确认 Linux 下即插即用。不能仅凭此判断 UAC 版本、采样率、端点或是否需要厂商特殊处理，后续按真实 USB 描述符适配，无需先知道商品型号。
- UART TTS 已连接 **UART4**。该信息是硬件控制器编号，不等于 openvela 的 `/dev/ttyS4`；仍须核对板级引脚复用、独立串口注册及设备节点，不能复用控制台。
- I2S 资料已登记在下方，仅作为后续参考，本轮不开发、不配置、不启用 I2S。

## I2S 后续资料位置（只读参考）

原目录：`D:\My-Program\AI-Agent-Program\cc-program\Radxa_A7Z_Sencond_Pro\Old-Program\a7z_i2s_audio`。

- `README.md`：Linux bring-up 路线、接线建议和验证顺序，已只读查看。
- README 提到的 `a7z-i2s-max98357a-inmp441.dtso`：Linux device tree overlay 模板；本轮未核查内容，不能直接作为 openvela 驱动。
- README 提到的 `check_i2s_support.sh`：Linux I2S/ALSA 支持检查脚本；本轮未核查内容。

README 所述模块：**MAX98357A** I2S 功放、**INMP441** I2S 麦克风。
资料中的候选引脚：BCLK=Pin12/PB5，LRCK=Pin35/PB6，MCLK=Pin36/PB4，功放 DIN=Pin40/PB7，麦克风 DOUT=Pin38/PB8。
这些接线仅登记原资料内容，未在本轮重新核验官方引脚表或实际硬件；启用前必须再次核对。
INMP441 资料建议 3.3V、L/R 接 GND；功放供电与模式脚按实际模块手册确认。
后续先播放、再录音，最后整合语音对话；必须在原 Linux 版移植全链路跑通后才开始。
Linux 的 ALSA、simple-audio-card 和设备树 overlay 方案不能直接代替 openvela 的控制器/音频驱动与 Media API 接入。

## 本轮代码与验证

1. `apps/system/aipet/uart_tts.{hxx,cxx}`（均在 board overlay 下）：实现原版 TW-TTS 文本帧和音量/速度/音调参数帧；9600、8N1；处理部分写、EINTR/EAGAIN 和 6 秒写超时。必须显式指定独立串口，拒绝 console 和 ttyS0，不自动配置 GPIO。记录帧数、失败数、errno。计数表示提交成功，不表示模块已实际发声。
2. `apps/system/aipetllm/aipetllm_api.{h,cxx}`：同步直接调用 API，可传系统提示词、选择核与生成长度，输出经 UTF-8 解码回调；共享已有缓存与计算线程池。回调输出为暂存数据，调用成功前不能提交动作。正常/错误/标准异常路径释放解码器。
3. tokenizer 支持自定义系统提示词，旧命令仍使用原默认提示词。序列输入数组上限扩至 512；旧 CLI 限制仍保留，生成上限仍为 64。
4. `pet_debug.hxx`：`AIPET_DEBUG=0` 默认编译去除业务后台日志；调试编译指定 `-DAIPET_DEBUG=1`。只记录阶段与状态，不输出密钥、对话或音频。原 LLM checkpoint 日志尚未统一关闭，不宣称整个系统日志已隐藏。

主机测试：UART 协议/参数/边界/控制台保护通过；LLM API 使用 mock 验证回调、参数、错误和异常清理通过；tokenizer 源码独立编译通过。测试在 `tools/test-aipet-tts.cxx` 与 `tools/test-aipet-llm-api.cxx`。
未完成 ARM64 固件构建/板端回归，不能将这些主机结果当成烧写验收。

## 必须继续完成的项（没有用占位成功掩盖）

- UART 中文需 UTF-8→GB2312 转换。当前接口只接受已编码 GB2312；不能把 UTF-8 原样发送。先审查官方 iconv 是否支持该转换方向，再决定官方适配或受控转换表。
- 确认 TTS TX/RX/GND 实际针脚、BSP 串口注册及 pinmux 冲突。Linux ttyAS4 不能直接映射为 openvela ttyS4。
- USB 麦克风尚无驱动：当前官方 `nuttx/drivers/usbhost/Kconfig` 没有 USB Audio 类选项。需获取真实设备描述符，完成 UAC 控制、采样格式、等时 IN、音频下半部及注册；现有 UVC 控制器链路不能自动替代音频驱动。
- 上层录音走官方 `frameworks/multimedia/media/include/media_recorder.h`，不能用 Linux PyAudio/ALSA 命令冒充适配。录音框架本身不提供缺失的 USB 麦克风底层。
- 注册实际 aipet 应用、配置存储与云端 HTTPS/SSE、ASR、VAD、屏幕、动作安全参数校验及串口 TTS 调度仍待实现。
- 还没有原版行为全链路板测，没有生成新最终镜像。
- NSH Ctrl+C 强制退出与锁清理风险仍存在；本 API 的 C++ 异常清理不能覆盖 NSH 强制任务删除。

原 Linux 开发目录保持只读。不要复制含 API Key 的完整配置，密钥由板端用户配置。
