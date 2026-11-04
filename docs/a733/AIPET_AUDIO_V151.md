# v151：ICS43434 / MAX98357A 接入 AIPET 应用

2026-10-16。保留 v150 用户实测“两次录音、五次回放”镜像/标签。
本轮将已通过的采集/发送核心包在有界 PCM ABI 之后，产品不读写 MMIO、
不拼接板级诊断命令；复用官方 `voice_asr_recognize`、`voice_tts_speak`
和原 `pet` 消息总线/Agent 路由。不是标准 audio lower-half/Media 完成。

候选已完成全量构建及最终修正复编、布局、SDK恢复与镜像校验，
新链路仍待实机验收。[镜像/源码哈希和构建证据](../evidence/i2s-v151/build-summary.md)。

## 职责和状态

```
ICS43434 → 板级采集（48k mono S16，默认×5）→ RAM
         → 63-tap低通/3倍降采样（16k mono S16）→ 官方云 ASR
         → 既有规则/官方 Agent/本地回退路由 → 文本 + LVGL表情
         → UART4 TTS 或 官方云 TTS（16k PCM）→ RAM → MAX98357A
```

默认仍 UART，沿用 `aipet tts on/off` 标记；切 I2S 必须显式配置。
本地 LLM/ASR 接口保留，云 ASR 在断网时并不能自动变成本地识别。
没有后台唤醒词、常开录音、VAD、双工/AEC、即时取消或实时播放中调音量。
应用接口已实现并做主机测试；新固件构建/镜像/上板验收以交付证据为准，
不能用 v150 底层通过代替 v151 应用/云语音通过。

## 应用命令（NSH/SSH均可）

烧录整卡镜像前先备份现有 `/data`；加密配置与 `device.key` 必须成套保存，
丢失板级密钥后不能仅凭加密JSON恢复账号。备份中的密钥/私钥不要上传仓库。

先只做本地验证，不需要 ASR/TTS 账号，不上传任何录音：

```sh
aipet audio status
aipet audio gain 5
aipet audio volume 50
aipet audio record 5000
ls -l /data/aipet-record.wav
aipet audio play /data/aipet-record.wav
aipet audio volume 30
aipet audio play /data/aipet-record.wav
aipet audio volume 70
aipet audio play /data/aipet-record.wav
```

录音5秒应得到480044字节48kHz mono PCM16 WAV；应用会暂缓SIGINT，等同步操作结束
后在下一阶段前返回取消，避免持锁任务被Ctrl+C直接销毁；主机注入已测，板端仍待验收。
Ctrl+C不是即时停止，kill/task_delete不支持；建议等有界命令自然完成。
应用采集先RAM，成功后才提交上述显式录音文件，
失败保留旧文件。所有录音上限10秒、下限100ms。

`volume 0..100`、`gain 1..16`、`output uart|i2s|off` 原子保存于
`/data/ai_agent/config/audio.conf`；重启仍保留。默认应用音量50%、增益×5。
volume是I2S PCM线性幅度，不是声学响度百分比，不调UART模块音量。
直接诊断节点的 `volume=` 仍是旧的开机内设置，应用播放时使用自己的持久值。
应用 I2S 音量0时文本播报不发起TTS请求；`off`关闭自动播报，不关闭麦克风命令。
损坏配置报错，不静默覆盖；意外掉电留下 `.new` 时先保留文件排查，再人工恢复。
FAT不保证POSIX权限/掉电原子性，配置文件权限不是完整物理安全边界。

## 官方语音服务的独立配置

DeepSeek/MiMo是LLM，不自带这个官方Volcengine ASR/TTS的权限。
先确保 Wi-Fi、DNS、NTP 和 `/data/ai_agent/ca.pem` 的受信任CA链正确，再：

```sh
aipet status
# 若未启动且已完成LLM初始化：
aipet start
aipet audio setup
```

向导输入ASR app id、隐藏access token（最多255字节）、TTS V3隐藏API key
（最多127字节）、可选speaker id、可选ASR cluster；空项保留原设置。
必须是对应服务实际开通的凭据/音色/cluster，不能拿LLM key代替。
配置保留LLM参数，token/TTS key用既有AES-GCM板级密钥加密保存；
板级密钥同在SD，不能抵抗获得整卡的攻击者，不是安全芯片保险箱。
向导不发送请求、不打印密钥，也不把密钥放到shell命令参数。

## 分步上板：先 ASR，再 TTS，再完整对话

以下命令会上传声音或文本并可能产生服务费用，均需手动执行：

```sh
# 录5秒并上传官方ASR，只输出识别文本，不调用LLM
aipet audio asr 5000

# 启用I2S回复，先做短句云TTS测试
aipet audio output i2s
aipet audio volume 50
aipet audio speak "语音播放测试成功"

# 文本问答 → 原路由/官方Agent → I2S播报
aipet ask "请只回答：应用语音测试成功"

# 一次完整的手动语音回合：录音 → 云ASR → 原路由 → 表情/所选播报
aipet audio listen 5000

# 随时切回已通过的UART模块
aipet audio output uart
aipet tts on
```

`listen/asr`在RAM采集、转换、调用官方接口，不自动导出或保留WAV；
原Agent会按已有策略保存识别后的文字会话。网络调用期间不持硬件FIFO锁。
播报失败打印错误，但保留已得到的文字/UI，不偷偷改用UART或声称已播成功。
云TTS暂限整句10秒PCM，超长/错误/缺终止响应拒绝，不播放被截断前缀。
48k硬件播放16k输入采用整数重复，不是高质量带限上采样器。
官方WS TTS当前请求24k，不能冒充16k，因此这轮用其官方16k批量接口。

## 新 ABI 与安全边界

- `sys/a733_audio.h`：flat构建同步ioctl，捕获/播放PCM及既有WAV；调用者持有缓冲到返回。
- `system/a733_pcm.h`：产品公共POSIX适配器；设备路径仅在vendor客户端出现。
- 采集保留原双槽FIFO、时间/计数/错误门禁与左槽×5；成功才复制RAM结果，云链路不经SD。
- 发送复用v150的预填充/供数/排空核心，输入先拷贝、最大10秒；竞争硬件返回EBUSY。
- 官方ASR补丁：受信CA强校验、合理时钟检查、5秒socket读写超时及I/O循环60秒deadline、
  接收缓冲额外NUL字节、最终响应缺失失败。阻塞DNS/TCP connect仍受系统超时，
  不宣称60秒硬实时取消；不要并行使用另一套官方voice CLI切后端/操作同一语音服务。
- 官方TTS补丁：有界扩大响应容量，完整NDJSON/终止码/base64/PCM长度检查，拒绝半段成功。
- 主机ASan/UBSan覆盖应用路由/负面/锁、真实采集/发送函数和凭据向导；
  实机云鉴权、音量范围、重启持久化、录放切换、与Wi-Fi/BLE/LVGL共存需用户验收。

## 复现

```sh
bash tools/test-aipet-audio.sh
bash tools/test-ai-voice-patches.sh
python3 tools/test-a733-ics43434.py
python3 tools/test-a733-playback.py
bash tools/build-a733-i2s-v151.sh
bash tools/package-a733-i2s-v151.sh
```

构建在官方树临时暂存overlay/补丁，退出恢复原字节并核对；独立v151输出，
不覆盖v150恢复镜像。新整链路只在最终板测后再标 hardware-passed。
