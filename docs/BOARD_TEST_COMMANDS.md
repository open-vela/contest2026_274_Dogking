# 板端功能测试命令手册（Cubie A7Z / openvela）

串口 115200 8N1，无硬件流控。所有命令都在 NSH（`nsh>`）提示符下执行。

> **证据口径**：下表中「预期结果」写的是**实测过的值**。标 ⚠️ 的项目尚未通过，
> 请照实记录实际输出，不要因为与预期不符就跳过——评审看的是真实日志。

---

## 0. 一键完整回归（先跑这个）

按顺序执行，把整段串口输出保存下来就是完整验收证据：

```text
uname -a
free
ps
uptime
mount
ls /dev
cat /dev/a733-hw
sensortest -n 3 temp0
cpucheck

dd if=/dev/mmcsd0 of=/dev/null bs=512 count=1
dd if=/dev/mmcsd0 of=/dev/null bs=16384 count=64
dd if=/dev/mmcsd0 of=/dev/null bs=65536 count=16
ls /data

wifi scan
wifi connect <你的SSID>
ifconfig
ping -c 4 <网关IP>
nslookup example.com
curl -s -o /dev/null -w "%{http_code}\n" http://example.com/

cat /dev/npu0
npu info
npu selftest

aipet status
aipet ask "请只回答：在线链路成功"
aipet local status
aipet local ask "请只回答：本地链路成功"

aipet tts status
aipet tts test "串口语音测试成功"

display status
display test 30
display fill ff0000 00ff00 0000ff
cat /dev/a733-lcd
dmesg
```

---

## 1. 基础系统

| 命令 | 预期结果 | 状态 |
| --- | --- | --- |
| `uname -a` | `NuttX 0.0.0 ... arm64 cubie-a7z` | ✅ |
| `free` | 约 4.27 GB 托管内存 | ✅ |
| `ps` | 空闲线程、高优先级工作队列、NSH 任务 | ✅ |
| `uptime` | 自启动以来的运行时长 | ✅ |
| `mount` | procfs 已挂载，`/data` 为 FAT | ✅ |
| `ls /dev` | 见下方设备清单 | ✅ |
| `cat /dev/a733-hw` | 合并硬件检查点（LED/WDT/THS/I2C/SPI/PWM/NPU/UART/I2S） | ✅ |
| `sensortest -n 3 temp0` | `value:34.47` 左右，三次读数 | ✅ |
| `cpucheck` | CPU0..5 = Cortex-A55，CPU6..7 = Cortex-A76，亲和掩码覆盖 8 核 | ✅ |
| `dmesg` | 完整启动日志（含各子系统 checkpoint） | ✅ |
| `poweroff` / `reboot` | 走板级/PSCI 路径关机或重启 | ✅ |

**关键设备节点**

```text
/dev/userled          /dev/watchdog0        /dev/uorb/sensor_temp0
/dev/i2c2  /dev/i2c7  /dev/spi1             /dev/pwm0
/dev/mmcsd0           /dev/a7z-firmware     /dev/a7z-efi
/dev/a7z-openvela     /dev/a7z-data         /dev/npu0
/dev/ttyS4            /dev/random           /dev/urandom
/dev/a733-hw  /dev/a733-npu   /dev/a733-wifi  /dev/a733-lcd
/dev/a733-uart4  /dev/a733-audio  /dev/a733-uvc  /dev/a7zconsole
```

---

## 2. 存储（SD / GPT / FAT）

```text
dd if=/dev/mmcsd0 of=/dev/null bs=512 count=1
dd if=/dev/mmcsd0 of=/dev/null bs=16384 count=64
dd if=/dev/mmcsd0 of=/dev/null bs=65536 count=16
ls /data
```

预期：三条 `dd` 均无 I/O 错误；`/data` 可列出内容（Wi-Fi 固件在 `/data/aic/`）。

写入与长文件名（FAT 支持 UTF-8 长名）：

```text
echo hello > /data/test.txt
cat /data/test.txt
ls /data
rm /data/test.txt
```

GPT 分区由启动日志确认：

```text
dmesg
```

预期包含：

```text
A733 GPT: entry=1 node=/dev/a7z-firmware first=32768 blocks=32768
A733 GPT: entry=2 node=/dev/a7z-efi first=65536 blocks=614400
A733 GPT: entry=3 node=/dev/a7z-openvela first=679936 blocks=237568
A733 GPT: entry=4 node=/dev/a7z-data first=917504 blocks=131072
A733 FAT: /dev/a7z-data mounted read/write at /data
```

---

## 3. Wi-Fi 与网络

### 3.1 扫描与连接

```text
wifi status
wifi scan
wifi list
wifi connect <SSID>          # 密码为隐藏输入，不会回显
wifi status
ifconfig
```

预期：2.4 GHz 与 5 GHz BSS 均能扫到；WPA2 关联成功；DHCP 获得 IPv4 地址。

### 3.2 连通性

```text
ping -c 4 <网关IP>
ping -c 4 223.5.5.5
nslookup example.com
curl -s -o /dev/null -w "%{http_code}\n" http://example.com/
```

### 3.3 断线重连与记忆

```text
wifi disconnect
wifi status
wifi reconnect
wifi status
wifi autoconnect on          # 开机自动连接上次网络
wifi forget                  # 忘记已保存网络
```

### 3.4 高级

```text
wifi dfs                     # DFS 信道信息
wifi time                    # 时间/NTP 相关
```

### 3.5 网络服务（从 PC 侧验证）

板端启动 FTP/SSH 服务：

```text
ftpd start
services status
services enable sshd
services enable ftpd
services enable agent
services status
```

从 PC（Windows）侧：

```powershell
ssh <user>@<板子IP>                    # 进 NSH
ftp <板子IP>                           # FTP 登录
curl http://<板子IP>/                  # 若启用 HTTP
```

板端 `iperf`：

```text
iperf -s                    # 服务端（板子）
# PC 侧：iperf -c <板子IP> -t 30
```

实测基线：TCP **4.13 Mbit/s**（30.25 s / 14.9 MiB），UDP **10.8 Mbit/s**（丢包 5.7%）。

---

## 4. NPU（VIP2）

```text
cat /dev/npu0
npu info
npu selftest
npu report
npu run
npu yolo
```

### 4.1 跑准备好的模型

A7PM 模型需先放到 `/data/npu/`（见 `docs/ARTIFACTS.md`）：

```text
echo prepared=/data/npu/lenet.a7pm > /dev/npu0
cat /dev/npu0
```

预期 LeNet 输出 CRC32 = **`d50f5d79`**，与 Linux VIPLite 黄金结果一致。

YOLOv8n 六输出预期 CRC：

```text
55d0becd ead06611 ba209c57 2637657c 9e7f743a 2eb4d5ce
```

### 4.2 双向验证（排除"固定输出回放"）

```text
npu yolo dog                 # 有检测框
npu yolo black               # 0 个检测框
```

这是证明"结果来自给定输入"而不是回放固定输出的关键测试。

---

## 5. AI 桌宠

### 5.1 首次配置

```text
aipet init
```

按提示选择 DeepSeek/MiMo、默认或自定义模型，**隐藏输入 API Key**
（用板级密钥加密保存到 `/data/ai_agent/`，不写入源码、不烧进镜像）。

### 5.2 云端链路

```text
aipet status
aipet route "你好"
aipet ask "请只回答：在线链路成功"
aipet cloud
```

预期：首次 TLS 请求约 **7.5 秒**，连接复用后约 **1.8 秒**；中文回答正常。

### 5.3 本地 Qwen 兜底

```text
aipet local status
aipet local ask "请只回答：本地链路成功"
aipet local stop
aipet cancel
```

预期：本地 1.1 GiB Qwen2.5-1.5B 常驻内存，单 token 前向约 **0.6 秒**，
中文流式输出。**首次加载**约 175 秒（从 SD 卡读 1.1 GiB，仅第一次）。

断网降级测试（关键）：

```text
wifi disconnect
aipet ask "断网后还能回答吗"
```

预期：云端失败后**自动切到本地 Qwen** 并给出回答，而不是报错。

### 5.4 其他子命令

```text
aipet start
aipet --boot
aipet emotion
aipet actions
```

---

## 6. UART4 语音播报（TW-TTS）

```text
cat /dev/a733-uart4
aipet tts status
aipet tts test "串口语音测试成功"
aipet tts test "第二次播报"
```

### 6.1 预期诊断值（v118 已实机通过）

```text
bgr=00010001 gate=1 reset=1
PJ24=TX/function4 PJ25=RX/function4 cfg3=0000ff44
lcr=00000003 lsr=00000060 thre=1 usr=00000006 tfnf=1
```

```text
uart-tts test: frame sent (0)
initialized=1 frames=4 failures=0 last=0 stage=complete
```

`frames=4` = 音量、语速、语调三帧 + UTF-8 文本一帧。

### 6.2 故障排查

若返回 `-22`：termios `B9600`/数值波特率混用（v116 已修）。
若返回 `-110` 且 `frames=0`：UART FIFO 不可写（v117/v118 已修 CCU/pinctrl）。
若仍失败，把 `cat /dev/a733-uart4` 的完整输出发出来。

---

## 7. 显示（ST7735 / LVGL）

```text
display status
display test 30
display fill ff0000 00ff00 0000ff
cat /dev/a733-lcd
```

### 7.1 `display test 30` 的场景

白底 + 顶部 20 像素高红/绿/蓝三色带 + 两只深灰眼睛（`0x202020`）+ 深灰嘴。

### 7.2 ⚠️ 当前已知问题：颜色仍是竖条纹

**这一步是目前唯一没通过的**。已确证的部分：

```text
display: [a7z_st7735] bring-up: SPI1 bus init
display: [a7z_st7735] bring-up: RESET pulse done (PB0 high, SPI1 ready)
display: [a7z_st7735] bring-up: st7735_lcdinitialize start
display: [a7z_st7735] bring-up: st7735_lcdinitialize done
display: [a7z_st7735] /dev/lcd0 registered on demand
```

`cat /dev/a733-lcd` 预期：

```text
pio-pd: cfg0=ffffffff cfg1=ff6666ff drv0=00700000 pul0=11111111 data=00700000
rpio-pl: cfg0=ff1ff122 drv0=11111110 pul0=00000025 data=00000024 pl5=1
```

- `cfg1=ff6666ff` → PD10..13 已复用为 SPI1 function 6 ✅
- `pl5=1`、`data` bit5=1 → D/C 已驱动为高（3.3 V）✅

### 7.3 请重点跑这条并回传结果

```text
display fill ff0000 00ff00 0000ff
```

它会**绕过 LVGL** 直写纯色，每种颜色连续显示**两遍**：

```text
display: fill ff0000 rgb565=f800 order=msb
display: fill ff0000 rgb565=f800 order=lsb
display: fill 00ff00 rgb565=07e0 order=msb
...
```

共 12 秒。请**录像或连拍**，然后告诉我：

1. 六段里**哪一段是干净的纯红/纯绿/纯蓝**（无竖条）？对应 `order=msb` 还是 `lsb`？
2. 若出现纯色但**颜色串了**（该红却蓝）→ 是通道顺序问题，关掉
   `CONFIG_LCD_ST7735_BGR` 即可。
3. 若**六段全是竖条** → 字节序不是原因，我去查列地址窗口与 LVGL flush 路径。

### 7.4 运行时字节序切换（手动）

```text
echo wordmsb > /dev/a733-lcd
display test 10
echo wordlsb > /dev/a733-lcd
display test 10
```

### 7.5 其他诊断命令

```text
echo probe    > /dev/a733-lcd      # 打印 R_PIO/R_CCU 全部寄存器
echo dcon     > /dev/a733-lcd      # 手动把 D/C 拉高
echo dcoff    > /dev/a733-lcd      # 手动把 D/C 拉低（可用万用表量 Pin 22）
echo selon    > /dev/a733-lcd      # 手动拉低片选
echo seloff   > /dev/a733-lcd      # 手动拉高片选
echo pl5mux 1 > /dev/a733-lcd      # 手动设置 PL5 复用功能
```

**接线参考**（ST7735）：SCK=PD11(Pin23)、MOSI=PD12(Pin19)、CS=PD10(Pin24)、
**DC=PL5(Pin22)**、RESET=PB0(Pin7)、VCC/BL=3.3V(Pin1/17)、GND。

---

## 8. 音频 I2S（诊断阶段）

```text
cat /dev/a733-audio
```

**注意**：这只是**非破坏性诊断**，可读 PLL/CCU/引脚状态。MAX98357A 与 INMP441
**没有 PCM lower-half，没有播放/采集数据流**。⚠️ 未完成，不要当成已通过。

---

## 9. USB 摄像头（诊断阶段）

```text
cat /dev/a733-uvc
echo probe > /dev/a733-uvc
dmesg
```

**注意**：xHCI 能力寄存器可读，但**设备尚未枚举**。⚠️ 未完成。

---

## 10. 服务自启动

```text
services status
services enable sshd
services enable ftpd
services enable wifi
services enable agent
services disable ftpd
services on
services off
```

---

## 11. 构建与镜像（PC 侧，不是板端）

```bash
# 构建（WSL）
cd ~/openvela-contest
bash contest2026_274_Dogking/tools/build-a733.sh

# 生成可烧写镜像 + 三条独立校验
python3 tools/make_flashable_image.py --all
python3 tools/reverify_image.py <镜像路径>
bash tools/verify-minimal.sh
```

烧写后核对镜像 SHA：

```powershell
Get-FileHash .\openvela-a733-cubie-a7z-minimal.img -Algorithm SHA256
```

---

## 附：Rockchip 两平台

RK3506 / RV1103 的测试命令见各自板级 README：

- `board/rockchip/boards/rk3506/luckfox-lyra-zero-w/README.md`
- `board/rockchip/boards/rv1103/luckfox-pico-mini/README.md`

RK3506 冒烟测试（控制台 **1,500,000** baud，注意不是 115200）：

```text
hello
free
ps
dmesg
ls /dev
gpio -o 1 /dev/actled
sleep 1
gpio -o 0 /dev/actled
```

RV1103（115200 baud）核心验收：

```text
free
ps
time "sleep 5"          # 约 5 秒返回，验证定时器
ls /dev                 # /dev/workled /dev/watchdog0 /dev/uorb/sensor_temp0
sensortest -n 5 temp0   # 约 50.54 C
cat /dev/random | wc -c # TRNG
```
