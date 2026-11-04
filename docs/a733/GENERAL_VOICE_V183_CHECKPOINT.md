# v183 开发检查点：Linux 通用 ASR 已取证，RTOS 全链路未完成

2026-10-26。基线 `35d83a0` / `a733-v182-live-voice-20261025` 保留。
本检查点不生成或替换固件，不修改 SDK、NPU 内存布局、分区、UFS 或引导文件。

## Linux 实测

用户提供 Radxa Linux A7Z，内核 `5.15.147-21-a733`，VIPLite 2.0.3.2，
CID `1000003b`。SSH 只用于独立 `/home/radxa/a733-v183-20261026` 测试目录。
没有重置 NPU、格式化、挂载或写入 UFS，没有修改 `/boot`。

新通用 ASR 是 36/36 encoder、512 维 decoder/joiner、6254 类，不是旧 263 类 KWS。
使用已锁定的三份 fp16 NBG；每一块的 35 个状态来自真正 NPU 输出。
三组输入：

| 输入 | 原生转写 | 与 CPU 参考的 token / 位置 |
|---|---|---|
| cmd_open_light_16k | 帮我打开灯 | 完全一致 |
| cmd_time_16k | 现在几点了 | 完全一致 |
| neg_silence_3s_16k | 空 | 完全一致 |

共 216 次原生模型调用，保存每次所有输入和输出。11 次 encoder（含相同输入重复）
396 个输出、13 个 decoder 输出、192 个 joiner 输出，共 601 个输出逐个与
CPU ONNX 比较。全部整数状态一致，192 次 joiner argmax 一致。
浮点最大相对 L2：encoder 0.0110572、decoder 0.00222867、joiner 0.00108895。
浮点并非逐字节一致，不能把这些参考输入结果外推成所有语音或 RTOS 验收。
本记录未宣称通用浮点误差阈值已经通过。

三份模型另外采集了真实准备后布局，encoder 使用非零跨块状态，decoder 使用
非零 token context；每个模型的实际子进程退出码为 0、全部输出与之前原生 golden
逐字节相同，tracer failed=0、snapshots=2。未修改 ioctl 命令或返回值。
encoder 大于旧 32MiB 证据文件限制，tracer 为此增加可配置且至多 64MiB 的
单文件上限；旧模型默认仍为 32MiB。**这不是 NPU DMA/工作区扩容。**

## 已完整回传的私有制品

均位于项目 `out/asr-native-v183-r1/`，原始及失败文件保留，不进 Git：

- `asr-real-r2-report.tar.gz`，12,926,512 B，SHA-256
  `a8f7c870574fa1e604e063c45edc299bce14418438716b390c0ea5d27e11817d`。
- `asr-layout-r1-report.tar.gz`，103,143,856 B，SHA-256
  `1d6a8cac78838a6f01c112bcce7ac6aa2f65718c77bed7e26669fa595260800a`。
- `layout-qualified-r3-report.tar.gz`，103,309,667 B，SHA-256
  `1d40b0c2d437b253e4f64b67a59bc7c86e800e2e7e3a79bf6224f74f5101acbf`。

CPU 全输出比较：`out/asr-native-v183-compare-r1/`。
布局尚未导出新 A7TV，也未做 openvela 硬件执行。不能据此启用默认 ASR provider。

## TTS 最小 CPU 执行器准备

已获取官方 ORT v1.23.2 固定提交
`a83fc4d58cb48eb68890dd689f94f28288cf2278`；源码归档 SHA-256
`7270fab0d96d964959b35648014230c00fa2a9cb98df087e5fd5c2b2c24c8085`。
源码只作参考；另在 `out/ort-nuttx-v183-r1/` 创建独立构建副本，SDK 不变。

Matcha 原 ONNX 转成 `basic/Fixed/arm` 的最小 ORT 格式，避免主机 NCHWc 打包。
生成 `out/matcha-ort-v183-r1/model-steps-3.basic.ort`，65,033,312 B，SHA-256
`cc2ec17c1bcca92239ed476d059900c8dac9c6137a444ce15ff5cc903845d243`。
五组中文 token 输入，noise_scale=0 仅用于确定性等价检查，最大 mel 绝对误差
0.0000195503。不是实际声音、Linux 板上 TTS 或 RTOS 执行器通过。
原 Baker 模型非商业限制保留，不把模型或厂商库加入 Git。

CPU 依赖已按官方源码 deps.txt 的锁定 SHA1 下载，同时记录 SHA256，位于
`out/ort-deps-v183-r2/`；原 r1 长路径失败证据保留。
SDK CMake 3.23 不满足 ORT 3.28 最低要求，另用私有目录的 CMake 3.31.6。
真实 NuttX ABI 工具链配置已生成；运行库尚未编译成功，不能安装 provider。
首次组合配置命令被 PowerShell/Bash 引号解析中断，未开始 CMake 或修改 SDK；
下一步改为独立脚本调用 configure-command.json，避免嵌套 shell 字符串。

## 已修正的测试工具问题

- 老 contract 包使用的 runner 要求 GLIBC 2.33/2.34，不能运行于本次 Bullseye。
  已用既有 Bullseye sysroot 重新编译，所需 GLIBC 仅 2.17。
- Windows chmod 不会自动转为 tar 的执行权限。新打包器显式设置并检查
  runner tar mode=0755；板端 r1 仅修正该新测试文件权限，未改系统程序。
- 第一次大文件上传连接中断，原部分文件保留；以新文件名重传并核对 SHA256。
- SSH 主机使用已接受的 known_hosts，密码交互输入，不写入源码、报告或 Git。

## 停电后的新启动故障

用户在测试完成、报告回传后误断电。提供的日志显示内核已启动，但：

```
Initramfs unpacking failed: broken padding
Failed to execute /init (error -2)
Kernel panic - not syncing: No working init found.
```

这首先定位到 initramfs 解包/内容或读取问题，不能仅凭日志确定 SD 文件损坏、
SD 读取不稳定还是供电原因，更不能认定芯片损坏。该故障不是新的 openvela
固件启动失败；本轮没有写入 Linux initrd、内核、U-Boot 或启动配置。
已暂停进一步板端操作，不在不稳定启动环境写入、重刷或自动修复文件系统。

## 未完成及下一步

1. 根据真实布局导出并审计通用 ASR 准备包；做内存预算、独立候选构建和板测，
   再实现/启用 openvela 通用 ASR provider。旧 24MiB prepared/2MiB tensor 配置不变。
2. 完成 Matcha 最小 CPU 引擎的真实 NuttX 编译、链接、取消和内存边界验证，
   再接入已验证 vocoder，验证任意中文文本生成真实声音。
3. 接真实实时采集、ASR、LLM 首句和 TTS 队列，做停止/异常/多轮/录放切换验收。
4. LLM 仍是上次板测预热后首 token 3.653 秒；本轮没有新的 LLM 加速交付。
   全链路首音频 3–4 秒仍未达标或实测。

主机安全回归：`python tools/test-general-asr-v183.py`。
所有 Linux 原生流程都是取证工具，不是可直接放入 openvela 的 Linux 动态库后端。
