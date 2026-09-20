# 桌宠 Linux 行为移植：第一阶段

## 基线和真实性

本次用户 v97 日志验证 `llm chat` 中文多轮：第一轮介绍名字 Tom，第二轮回答“你叫Tom。”；history 保留 50/64，reset 清零，status 显示驻留缓存与六计算线程。独立标记 v97 基本验证通过；运行中 stop、Ctrl+C、长期稳定性仍未通过。本轮不更换此烧写基线。

需求源为上层 `goal/`。以实际 Python 源码为行为真值，`docs/vision4-requirements.md` 用于解释需求；二者冲突需记录，不默默选一个或声称完美移植。

`goal` 本身的电机、LED、SPI 显示均为 Mock/TODO；assets 当前无文件，不能宣称已经有可播放 GIF。Linux 本地客户端使用 HTTP SSE，实际 ConversationManager 调用 `split=False`，收完完整回复才解析/执行。原客户端每次只有 system+user，不携带对话历史；v97 的多轮能力是扩展，不应强制替换原模式。

## 本轮交付：可移植业务核心（尚未进入镜像）

源码目录 `board/a733-cubie-a7z/openvela-overlay/apps/system/aipet/`：

- `pet_core.hxx`：回复、规则、路由、状态和 Ports 适配器契约。
- `pet_core.cxx`：动作/情绪双层解析、规则优先、云端不可用降级、云端失败重试本地、回复交付和返回 idle。
- 不调用 shell、私有诊断设备、寄存器或 Linux 子进程。LLM、网络、动作、屏幕、TTS 通过适配器接入，不伪造未完成能力。
- 没有注册新的 NSH 命令或开机服务；这是待集成的 C++ 业务库，不是完整桌宠固件。

已保留的解析语义：原动作正则、12 项方法白名单、动作出现顺序和重复次数；未知但格式正确的动作去掉且不执行；情绪取正文中第一次命中的标签，全部合法情绪标签去掉；Unicode 空白归一化；没有标签时默认普通。

刻意新增的安全边界：输入/回复 4096 字节、动作 32 个、单动作 160 字节、UTF-8 有效性检查；超限整个回复拒绝，不交付部分动作批次。动作参数暂保留原字符串，不能直接交给真实执行器：下一阶段必须做整数、个数、范围和资源检查，绝不 eval。

原解析器保留畸形/未知标签，因此其“任何方括号都不播报”的需求并非实际实现保证。已保留原解析行为用于对照，真实 TTS 适配器必须额外隔离畸形/截断标签，策略需单独测试。停止/截断生成不得执行不完整标签。

规则表由 Rules 注入，保留 JSON 配置顺序；没有硬编码用户配置或复制 API key。直接回复随机选择和 time/date/weekday 模板由 Ports 负责。当前关键词大小写处理针对原配置的中文和 ASCII；非 ASCII 大小写扩展尚未验证。端口方法用 false 返回业务失败；云端适配器需将可降级的 API 异常转换为 false。Ports::present 将动作非阻塞排队、更新表情并播报干净文本，return_idle 应等待播放结束后安全回到普通表情。

核心由一个对话任务独占运行，不是线程安全的公共单例。跨线程输入和取消应通过事件进入对话 owner，不并发调用 run。

## 测试证据

`tools/test-aipet-core.cxx`：解析、边界拒绝、规则优先级、无需联网的本地规则、云端失败降级、离线降级、后端失败无输出、状态恢复和完成计数。

`tools/test-aipet-parity.py`：直接加载用户 action_parser.py 与 emotion_parser.py，对 69 个案例逐项比较正文、情绪、动作列表。仅加载这两个文件，不加载 config/settings.py 或任何凭据。包含中文、Emoji、Unicode 空白、未知动作、畸形动作、多个情绪和重复动作，全部通过。

日志 `archives/aipet-port-stage1/test-core.log`、`parity.log`、`boundary.log`。边界检查通过。上述为主机测试，不等于 ARM64 链接、服务或硬件验收。

Linux/WSL 重复测试（仓库根目录）：

```sh
g++ -std=c++17 -O2 -Wall -Wextra -Werror board/a733-cubie-a7z/openvela-overlay/apps/system/aipet/pet_core.cxx tools/test-aipet-core.cxx -o /tmp/test-aipet-core
/tmp/test-aipet-core
python3 tools/test-aipet-parity.py /tmp/test-aipet-core ../goal
bash tools/check-openvela-first.sh
```

## 接下来的模块和验收顺序

| 阶段 | 原 Linux 文件 | openvela 接入与门槛 |
| --- | --- | --- |
| 2 本地文本闭环 | local_llm.py、conversation.py | 导出 LLM 业务 API：自定义 system prompt、完整回复/UTF-8 字节回调、加载/生成/停止所有权；扩大上下文以容纳原人格和动作说明，默认保持原单轮逻辑。禁止捕获诊断 stdout 或拼 shell 命令。 |
| 3 服务与配置 | main.py、settings.py、routing.py、network_monitor.py | 单 owner 对话服务、uORB 文本/状态/动作事件，KVDB 非敏感配置；网络适配器保留 30 秒缓存/强制检测语义，配置按原规则顺序加载。不得明文保存 key 到 Git。 |
| 4 云端备用 | cloud_llm.py、cloud_asr.py | libcurl + mbedTLS、超时、SSE 及整回复模式、失败降级。TLS/NTP 有效，凭据设备侧配置，用户独立授权联网验证。 |
| 5 输入与播报 | vad.py、tts_client.py | Media Recorder/Trigger/Player 与 A733 音频下半层；UART TTS 可独立按原帧协议、GB2312 和音量/语速/音调参数移植；不把 UTF-8 直接当 GB2312。Edge-TTS 的 Python/ALSA 路径不能原样搬。 |
| 6 表情与动作 | spi_screen.py、hardware/*.py | UIkit + 实际屏幕/触摸下半层；标准 actuator HAL、限幅、动作队列、motor.stop 抢占/清理。原 GPIO17/18/12/21 是原型配置，不映射为 A733 管脚号。 |
| 7 视觉 | 摄像头/NPU 既有工作 | UVC 标准视频接口 → 预处理 → VIP2 YOLOv8 → 产品视觉事件，不解析私有 checkpoint 文本。 |
| 8 产品化 | 主循环/cleanup | 问候、输入等待、规则/生成、动作并行+表情+播报、播放完成后 idle、告别/清理、可选自启动、断网/取消/内存/热保护压力验收。 |

优先完成阶段 2 的真正本地文本闭环后才做新烧写镜像，避免为一个尚无输入输出适配器的空服务重复烧录。当前 64 token 上下文不能容纳原项目完整动作 system prompt，必须先解决，而不是偷偷省略人格/动作定义。

## 已查到的官方组件位置

- `quickly-openvela/frameworks/multimedia/media/include/media_api.h`、`media_recorder.h`、`media_player.h`。
- `quickly-openvela/frameworks/system/utils/include/kvdb.h`。
- `quickly-openvela/apps/system/uorb/`、`frameworks/system/topics/`。
- `quickly-openvela/frameworks/graphics/uikit/`。
- `quickly-openvela/apps/netutils/libcurl4nx/` 和已构建 curl。

这些是选定接口，不代表已启用、链接或完成 A733 下半层。实际编写适配器前需读对应头文件、构建依赖及样例，不根据 Linux 同名 API 猜测。

源码只改比赛仓库，本轮未修改官方工作区。v97 及旧镜像继续保留；本阶段保存独立源码 commit 和 bundle，下一轮构建前再次保存。
