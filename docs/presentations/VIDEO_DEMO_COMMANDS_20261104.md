# 桌宠 Vela 分模块完整演示命令

日期：2026-11-04。对应 [五分钟拍摄稿](VIDEO_SCRIPT_5MIN_20261104.md)，面向v227–v229产品镜像；v229是待实机验收的修复候选。**逐条执行，不要把全文一次性粘贴进串口。** 未标注“Windows”的代码都输入开发板NSH，不输入 `nsh>`；bttool内部命令只在bttool提示符下执行。

正式拍摄前先完整试一遍。命令成功日志不等于实际听见声音、看见画面或电机完成运动；不在镜头里伪造输出。密码及密钥配置在开拍前完成。

## 00 开拍准备与资源释放

先准备稳定电源、摄像头、SPI屏、I2S音频、UART下位机及物体道具。断电接线；旧UART4 TTS断开。STM32侧TX/RX与A7Z交叉并共地，电机独立供电。运动镜头先架空履带、避开桌边和衣物。

```text
aipet help
aipet stop
aipet audio service status
camera live-stop
camera live-status
```

等语音与camera均 `active=0` 后，再做独立测试。`stop=1`只是请求，不代表结束。若cleanup/owned异常、隔离或要求重启，不再叠加新任务。下面命令（v227起）关闭LLM自主动作提议；人类明确的规则运动命令仍可执行。

```text
aipet model-actions off
aipet audio output i2s
aipet audio volume 50
aipet audio gain 5
aipet mode local
aipet mode
```

正式拍摄无需开启自启动，避免重启后自动占用麦克风或摄像头。不要在拍摄中清空UFS、改PMIC、改下位机FLASH配置或清除未知故障。

## 01 启动 SMP 与内存

从上电前就录制串口完整启动过程，进入NSH后执行：

```text
ps
free
aipetllm cpucheck
cat /dev/a733-hw
```

拍摄重点：8个CPU空闲任务、内存总量、当前CPU身份及基础硬件状态。`free`通常不会恰好显示4 GiB空闲，部分内存用于保留区和系统。`ps`不等于所有任务都在A76上运行。

## 02 Wi-Fi 与时间

已经联网则只展示后四条，不必为拍摄反复断网。未连接时，在已获授权的热点上操作；下面SSID来自现有测试环境，现场不同就改成实际SSID，不要输入尖括号占位符。

```text
wifi scan
wifi connect ZBCK-E-5G_Wi-Fi5
```

按提示输入Wi-Fi密码，密码输入过程不录屏。

```text
ifconfig
ping -c 4 baidu.com
wifi time
ntpcstatus
```

展示有效IPv4、路由和校时。ICMP成功不是云API验证；在线TTS另在第12段实测。`ntpcstatus`如尚未采样，等待NTP，不用错误时间绕过证书校验。

## 03 SSH FTP 与运维

### 03A SSH 已配置的常规展示

```text
sshd --status
service status
```

只有未监听时才启动：

```text
sshd &
sshd --status
```

若明确提示未配置认证，开拍前执行一次 `sshd --setup openvela`，隐藏输入新密码；不要用 `sshd start` 代替本项目的启动方式。

Windows PowerShell中：

```powershell
$a7zIp = Read-Host "输入开发板 ifconfig 显示的 IPv4"
ssh "openvela@$a7zIp"
```

SSH内执行 `ps`、`free`、`aipet audio service status`。完成后在SSH会话中输入 `exit` 回到Windows PowerShell，或另开Windows窗口做FTP，不把Windows命令输入板端NSH。首次遇到主机指纹需与自己的板核对，不盲目删除known_hosts或关闭主机验证。

### 03B FTP 文件传输

已有FTP账号就不要重新setup。首次配置在开拍前运行 `ftpd setup`；当前启动/状态命令为：

```text
ftpd start
ftpd status
ls -l /data/models
```

FTP默认根目录为 `/data/models`，不能直接下载 `/data/aipet-record.wav`。第06段录音后，可复制到演示专用文件（重复此行会覆盖这个演示副本，不影响原录音）：

```text
cp /data/aipet-record.wav /data/models/video-record-20261104.wav
ls -l /data/models/video-record-20261104.wav
```

Windows使用FTP客户端连接板端IP、端口21和已设置账号。命令行可执行：

```powershell
ftp $a7zIp
```

进入FTP后输入账号密码，再执行：

```text
binary
ls
get video-record-20261104.wav
bye
```

在新的演示下载目录操作，避免覆盖电脑已有同名文件。FTP不加密，只在可信局域网使用；不拍账号密码，不展示凭据目录。

## 04 BLE 官方 Host

先确认无语音或相机独占任务。`bttool`会异步打印回调，**不能把下列各步无等待地一次粘贴**。

```text
bttool
```

bttool内部：

```text
pair auto 0
enable
```

等状态回调到BLE_ON(2)或ON(4)，再输入：

```text
state
scan dump
scan start -t 0 -p 1M -m 0 -l 1
```

拍5–10秒扫描结果，然后：

```text
scan stop
```

等 `on_scan_stopped_cb` 后：

```text
scan dump
disable
```

等OFF(0)回调后：

```text
state
quit
```

周围没有可见广播不等于Host适配失败。数据卡片可展示历史1501条记录及47/47 ping，但不要把它标成本次新扫描或唯一设备数；ESP32-S3业务连接不在本段冒充完成。

## 05 UFS 与模型存储

```text
ufs status
ufs autostart status
```

未挂载时才执行：

```text
ufs start
ufs status
```

等待 `ready=1` 后：

```text
ls -l /ufs/models
ls -l /ufs/models/MiniCPM5-1B-Q4_K_M.gguf
ls -l /data/models
```

MiniCPM5参考完整大小688065920字节；不能把0字节旧Qwen文件当模型。可选拍摄文件读速，只读取已有完整文件、不写盘：

```text
ufs readbench /ufs/models/MiniCPM5-1B-Q4_K_M.gguf 32
ufs status
```

按本固件实现观察任务/测量完成后再读数；32 MiB文件读速可能受缓存影响，不叫整颗UFS极限带宽。当前演示不执行copy大模型、分区、格式化或擦引导命令，不宣称1000 MB/s。

## 06 I2S 实际录放音

```text
aipet audio output i2s
aipet audio volume 50
aipet audio gain 5
echo init > /dev/a733-audio
echo tone=1000,500 > /dev/a733-audio
aipet audio record 3000
```

在录音开始后说：“这是 openvela 上的 I2S 录放音测试。”然后：

```text
ls -l /data/aipet-record.wav
aipet audio wavinfo /data/aipet-record.wav
aipet audio play /data/aipet-record.wav
aipet audio volume 30
aipet audio play /data/aipet-record.wav
aipet audio volume 50
```

提示音和回放都必须实际听到。麦克风与功放正确连线为：PB5/Pin12→BCLK及SCK；PB6/Pin35→LRCLK及WS；PB7/Pin40→功放DIN；PB8/Pin38←麦克风SD；共地。PB5不是LRCLK。扬声器两端接差分SPK，不把负端接地。合并模块电源按模块规格，不把裸芯片电压直接套上去。

## 07 VIP2 NPU 状态和自测

确保语音服务和camera均active=0：

```text
npu info
npu selftest
```

拍摄设备身份、池大小和selftest通过。全部Tensor/跨块验证的历史对照日志另作素材；不要为了镜头随意启动旧关键词模型并将它称为通用ASR，也不要在语音/视觉占用NPU时叠加selftest。

## 08 本地通用 ASR

### 08A 资产与一次准备

```text
ls -l /data/npu/general-asr
ls -l /data/npu/general-tts
time "aipet audio general-voice /data/npu/general-asr /data/npu/general-tts"
```

先等configure result=0，再开始独立ASR。若资产实际完整放在UFS，则成套改为 `/ufs/npu/general-asr` 与 `/ufs/npu/general-tts`，不要混用不完整模型。第一次准备可能较长，提前录好，不在正式镜头里伪装成瞬间初始化。

### 08B 独立识别 不执行动作

```text
aipet audio general-stream 5000 0
```

看到MIC ON后说：“请介绍一下你的功能。”参数0表示这次固定时长测试不以能量端点提前结束。确认实际转写，而不是只看result=0。独立命令不会执行“前进”等动作。

若返回-19，先确认general-voice准备成功；若返回-2，检查路径和文件。不要连续重启任务掩盖错误。

## 09 本地 MiniCPM5 与冷热计时

先选一个真实存在且完整的模型路径。以下采用UFS：

```text
aipet local on
aipet local model /ufs/models/MiniCPM5-1B-Q4_K_M.gguf
aipet local context 0
aipet local batch 4
aipet local tokens 128
aipet local clear
time "aipet local preload"
aipet local status
aipet local ask "请用一句话鼓励正在参加比赛的我。"
```

中文请求保留为一个带引号的参数；128为生成预算，不强制输出128个token，短回复会自行结束。正式演示使用模型自身输出的first-visible/first计时，不为套 `time` 损坏中文引号。冷preload与热cache-hit是两项，首token不等于首音频。

若使用SD，把model路径改为完整的 `/data/models/MiniCPM5-1B-Q4_K_M.gguf`。需要展示记忆时另拍 `aipet local context 3` 的可选片段；低延迟主镜头保持0，不同时宣称无限记忆。

## 10 本地 TTS

需要08A已经准备成功：

```text
aipet audio output i2s
aipet audio volume 50
aipet audio local-speak-stream "你好，我是桌宠 Vela，很高兴认识你。"
aipet audio local-speak-stream "今天的语音测试已经完成，请继续下一步。"
```

听完整句和尾部，不只截取前两个字。英文属于当前本地保底拼读，不拿它演示自然双语音色。若无声，先回第06段测tone与PCM回放，不直接把错误归因于TTS模型。

## 11 常驻本地多轮与停止恢复

结束所有独立命令后：

```text
aipet mode local
aipet audio output i2s
aipet run
aipet audio service status
```

等 `READY MIC ON` 后再说话，逐轮等播报完成、再次MIC ON后说下一句：

1. “请用一句话介绍你的功能。”
2. “请用一句话鼓励正在参加比赛的我。”
3. “音量调到五十。”

半双工处理/播报期间MIC OFF正常，不演示语音打断。停止恢复：

```text
aipet stop
aipet audio service status
```

等待active=0后：

```text
aipet run
aipet audio service status
```

确认重新READY，完成一轮后再stop并等待active=0，才能继续独立测试或改模式。

## 12 联网 ASR LLM TTS

**明确会上传录音/文本到所选云服务，使用有额度的已授权账号；不说私人内容。** 此段必须网络、时间和三种服务配置完整。首次配置开拍前完成：`aipet init`选择LLM；`aipet audio setup xfyun`配置独立APPID/APIKey/APISecret。DeepSeek密钥不能代替讯飞语音密钥。已经配置就不要重设。

```text
aipet stop
aipet audio service status
```

等待active=0，再：

```text
wifi time
aipet start
aipet status
aipet audio providers asr xfyun
aipet audio providers tts xfyun
aipet audio providers
aipet mode cloud
aipet mode
aipet audio cloud-speak "你好，我是桌宠 Vela。"
aipet audio asr 3000
```

录音期间说“请介绍一下你自己”，这条独立ASR只转写，不驱动电机。三项均通过后：

```text
aipet run
aipet audio service status
```

READY后说一条LLM问题，等回答结束再说第二条。v228以后cloud必须 `automatic-local-fallback=0`，不能偷偷加载本地模型。auto才允许自动选择；拍强制联网镜头不用auto。

如再次出现连接失败，保存新 `phase/SO_ERROR/wire-rx/wire-tx/owned-WSS` 日志；先修好或改用明确标注的本地演示，不把本地音频配成“联网测试成功”。

拍完：

```text
aipet stop
aipet audio service status
```

## 13 MIPI 摄像头与 RAW

等语音active=0、camera空闲：

```text
camera board
camera control status
camera selftest
camera live 10 0
camera live-status
```

`live 10 0`为10秒预览，不运行YOLO。背景任务未结束就等它结束；必要时 `camera live-stop` 后等active=0。

需要拍RAW文件导出时，**目标目录必须不存在，不要先mkdir这个目录**：

```text
camera capture /data/video-camera-20261104-01
ls -l /data/video-camera-20261104-01
cat /data/video-camera-20261104-01/COMPLETE.txt
```

只有成功生成COMPLETE才认可新帧，预览文件为view.ppm/view.rgb，另有frame.raw和vision-rgb.bin。重新拍换-02、-03，不删除旧证据、不反复覆盖同一个目录。

如果提示没有受信引导handoff，停在这里，按已验证的配套camera启动脚本启动；不要临时猜测PMIC寄存器写值。

## 14 YOLO SPI 连续画面与单次播报

先确认语音和camera均空闲：

```text
display raw8
camera live-pipe 30 1
camera live-status
```

拍摄摄像头、真实物体与屏幕同框；30秒后读：

```text
camera live-status
```

记录本次实际输出的FPS、frames/inferences、error/cleanup/fallback，不拿历史7.733替换本次读数。若当前版本pipe不健康，先合作停止并检查，而不是现场强杀任务；可在确认已清理后改用保守 `camera live 15 1`，字幕说明实际路径。

需要提前结束：

```text
camera live-stop
camera live-status
```

等active=0后，选择所需local/cloud模式，再做“拍照→识别→播报”：

```text
aipet vision once
```

这条命令通过产品规则获取新画面、YOLO、SPI和所选TTS；不会凭LLM猜图。连续camera任务不能与常驻语音同时占用资源。物体类别必须在模型支持范围内，不保证所有道具可识别。

## 15 STM32 协议与小幅运动

### 15A 先查询 不运动

```text
aipet mcu start
aipet mcu ping
aipet mcu config-get
aipet mcu stepper-stat
aipet mcu link-stat
aipet mcu status
```

应有实际RX与online=1、fault=0。只有TX计数增长或返回-110不算通信成功；先检查供电、3.3V电平、115200/8N1与交叉接线。A7Z Pin16/PJ24/TX→STM32 PA10/RX；Pin18/PJ25/RX←PA9/TX；共地。

### 15B 会真实运动 逐条执行

确认履带架空、机械范围安全、现场可以停止：

```text
aipet mcu arm
aipet mcu move forward 2 3
```

观察完成后：

```text
aipet mcu stepper-stat
aipet mcu stop
aipet mcu disarm
```

v227起 `move ... 3` 为3个桌宠步×20=60脉冲，不是3厘米；`move-raw`才是不缩放的诊断脉冲。不能自行把参数写成100再乘一次20。ACK不代替真实位移验证；无响应或结果未知不自动重发。

只有确实安装并验证了舵机，才另测 `aipet mcu servo -15 500`、`aipet mcu servo 0 500`；没有舵机就不要拍“向左看/向右看”当成步进电机方向控制。

### 15C 规则解析与语音动作

以下三条仅检查规则，不会拍摄或运动：

```text
aipet route "识别前面的物体"
aipet route "前进三步"
aipet route "音量调到五十"
```

进入第16段的常驻服务后，说“前进三步”，run会开启本次桌宠人类指令会话，不需每句话反复arm；故障/失联仍应停止。不要为演示去盲清故障或写FLASH配置。

## 16 aipet 完整语音与识物动作链路

先保证camera停止、各独立测试完成。选择一个明确模式：

```text
aipet mode local
```

如果要拍联网，把上行替换为 `aipet mode cloud`，并先完成第12段的真实账号验证；**二选一，不要把两行连续粘贴**。

```text
aipet audio output i2s
aipet audio volume 50
aipet audio gain 5
aipet run
aipet audio service status
```

等READY后，逐轮说，**每轮都等回答结束及再次MIC ON**：

1. “请用一句话鼓励正在参加比赛的我。”——ASR→LLM→TTS，拍一条完整真实时序。
2. “识别前面的物体。”——ASR→确定性识物规则→新帧/YOLO/SPI→TTS，不要求经过LLM。
3. “音量调到三十。”——设置规则与反馈；需要时再说“音量调到五十”。
4. 机械安全且15A通过时说“前进三步。”——ASR→规则→UART/STM32→实际动作与反馈。

如没有安装下位机，跳过第4项，不伪造动作成功。强制CLOUD失败不能改播本地却标成联网成功。

结束：

```text
aipet stop
aipet audio service status
camera live-stop
camera live-status
aipet mcu stop
aipet mcu disarm
```

未安装MCU时跳过最后两条。等待active=0再断电；要关机使用已验证的 `poweroff`，不要在UFS写入或任务尚持有设备时拔电。

## 可选的自启动镜头

只在全链路已经现场通过、确认今后开机会自动监听且机械区域安全时才启用，不是拍视频必选项：

```text
service wifi on
service ssh on
service ufs on
service agent on
aipet autostart on
service status
aipet autostart status
```

它们保存下一次启动策略，不表示此刻所有服务已就绪。恢复默认人工启动：`aipet autostart off`；其他服务是否关闭按自己的维护需要决定。

## 拍摄失败时的最小取证

```text
aipet audio service status
aipet mode
aipet audio providers
camera live-status
ufs status
ps
free
cat /proc/net/tcp
cat /proc/net/udp
cat /proc/iobinfo
```

保存整段从启动到失败的串口日志，隐藏密钥。不要只截最后一个-110，也不要强杀NPU/音频所有者。`owned-WSS`只表示本传输拥有的连接，活动时非零正常；TIME_WAIT也不等于泄漏。
