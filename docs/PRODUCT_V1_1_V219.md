# v1.1 / v219：v1.0 板测反馈修复与复测说明

## 2026-11-01：晋升最新稳定版

用户反馈“v1.1 完美成功，所有功能正常”。附带日志确认本地 ASR／LLM／TTS 连续多轮、识物与播报、两次协作停止及恢复、Wi-Fi WPA2/DHCP 成功。
本次晋升使用原镜像 **相同的 2 GiB 字节及 SHA256**，不重编译、不修改内核，所以帮助中的 `CANDIDATE (v219)` 字样仍是历史版本标识。
最新稳定指针见 [STABLE_RELEASE.json](STABLE_RELEASE.json)，脱敏板测记录见 [验收记录](evidence/product-v11-v219/board-accepted-20261101.json)。原始串口日志含真实网络名称，仅在本地 `out/v219-board-accepted-20261101/` 保存。

- 稳定标签：`a733-v1.1-v219-stable-20261101`，保留原候选标签与 `out/checkpoints/a733-v11-v219-recovery-20261031` 全量恢复包。
- 另存稳定晋升 Git 包：`out/checkpoints/a733-v11-v219-stable-20261101.bundle`，6,778,439 字节，已 `git bundle verify`；SHA256 `89e6ed884b129c29ea969c511624c6b2556c1e9501711c328ead57a43c82fdf1`。这是私有代码/历史包，不含 2 GiB 镜像和模型本体，外部制品仍按原恢复包及校验和保留。
- 稳定镜像：`../openvela-a733-cubie-a7z-sd-v1.1-v219-candidate.img`，SHA256 `fe7a28ce9996e3344a1866610c7cdb9137a1088965b0207161df6ffab4c3359f`。
- 首字随历史增加：83／111／142／185 token 的实际对话中，LLM 首字约 3.797／10.480／13.851／24.672 秒。ASR 收尾约 0.95～1.51 秒；不能把整段等待都算作 ASR 或 NPU vocoder 变慢。
- 后续 v1.2 预览独立开发，不覆盖此镜像、标签或历史文档。没有新增云账号、机械动作或仪器测量首音的验收，也不宣称整机 3～5 秒目标达标。

以下各节保留 **2026-10-31 候选交付时** 的修复过程和边界；“待板测”属于当时状态，最新状态以上述晋升记录为准。

## 1. 基线与结论边界

保留 v1.0 / v217-audit2 镜像、`a733-v1.0-v217-audited-20261031` 标签及原恢复包，不覆盖或删除。
其镜像 SHA256 为 `98daee1f3b4653d3d65a14a3e92c2713560aac2fd6183946d2b06b7a3c79bc21`。
本轮不是重新移植摄像头、NPU 或 I2S：用户已确认 YOLO 识物及识物播报成功、速度和准确性满意。
本轮修改常驻语音的会话检查、能量门控及诊断、Wi-Fi WPA2 报文和密码输入；没有修改系统调度、NPU 时钟/IRQ/DMA/ABI、YOLO 六路输出校验或底层 I2S 时序。

**v219 是待板测修复候选，不是已完成整机验收。** 完整构建、26 项语音/产品门禁及继承的摄像头/ELF/ABI/ISA/布局检查通过；SDK 暂存 316 项字节还原。旧过程文档全部保留，不提交 PR、不推送。

### 本次交付

- 新整卡镜像：`../openvela-a733-cubie-a7z-sd-v1.1-v219-candidate.img`，2,147,483,648 字节。
- SHA256：`fe7a28ce9996e3344a1866610c7cdb9137a1088965b0207161df6ffab4c3359f`；旁边有 `.sha256` 文件。
- 运行时代码：`f6531dbc0db75590ef76904afe4c786ec4bd8503`；封装工具提交：`67dbf45e0cbdd7b8adcbda5118a3e550441b6885`。
- 固件：7,234,472 字节；SHA256 `feaae52072d6912bb8d3e107a781724f3885ec9955db01271618a366f029c040`。
- [小型封存证据](evidence/product-v11-v219/README.md)，包含实际 RuntimePorts、旧故障复现、Wi-Fi 向量和完整镜像回读记录。
- 本地恢复标签：`a733-v1.1-v219-candidate-20261031`；恢复目录：`out/checkpoints/a733-v11-v219-recovery-20261031`。

整卡烧录会覆盖 SD，先备份 SD 上的配置和个人文件；本次打包没有写物理盘或 UFS。
`aipet help` 应显示 `v1.1 CANDIDATE (v219)`；若还是旧版，先确认从新 SD 启动，不要因此擦写 UFS。UFS 旧 U-Boot 问题仍按此前约定保留。

## 2. 本次日志确认了什么

| 项目 | 日志证据 | 能得出的结论 |
| --- | --- | --- |
| 第一轮语音 | ASR=`你好`；`intent=direct-rule`；回复后 `turn failed=-16` | 这轮走规则，不是 LLM 生成；回复适配阶段返回 EBUSY，不能认定声卡或声学模型坏了 |
| 所谓 TTS 卡住 | 随后 `stopped=-16`、`active=0` | 服务实际因错误退出，不是永远阻塞在推理 |
| 第二次启动 | 已 READY；停止后 `monitored=6169440 accepted48=0 speech=0` | 采集监视循环在工作，但没有触发语音；仅凭原日志不能确定是输入幅度、门限还是电气问题 |
| Wi-Fi | M1/M2 提交四次；`m3=0`；断连 reason=15 | 停在四次握手，未进入 DHCP；USB 提交成功不代表 AP 接受密码或 MIC |
| 识物 | 新帧、不同输入 CRC、六路输出、识别结果和 `local-tts-stream status=0` | 该路径及真实声音有用户确认；不能用它替代常驻多轮验收 |

原始日志仅保存在本地 `out/v219-board-feedback-20261031/serial-original.txt`，包含真实 SSID/MAC，不作为公开附件。
本文件不记录任何真实密码或 API 密钥。

## 3. 确定的会话锁错误及修复

常驻 owner 打开通用 ASR/vocoder 后，会在整个会话期间持有 `configuration_lock`，防止引擎被更换。
规则回复和本地 LLM 回复都会调用真正的 `RuntimePorts`，进而调用 `audio_product_prepare_local()`。
旧代码再次 trylock 同一配置锁，连 owner 自己也得到 `-EBUSY`；下一轮准备也一样失败。
手动识物前会关闭 ASR/vocoder 会话，所以当时没有触发这条错误路径。

v219 增加独立、短时保护的 owner 元数据。只有同一线程、同一任务组的已准备会话能读取不可变 TTS 就绪信息；
其他线程仍返回 EBUSY，配置替换仍被长租约排除，所有权不确定仍隔离至重启。不释放保护锁绕过问题，也不强杀 NPU owner。

新回归使用实际 provider、实际 prepare 函数、实际 RuntimePorts：先用冻结 v217 源码复现 `-16`，再验证修复、规则回复、聊天回复、六轮/两次会话、外线程拒绝、取消和隔离。
ASR/TTS/PCM 硬件执行在这些主机测试中是明确的 MOCK，不把模拟输出当板测声音。

## 4. 麦克风等待与门限

旧门控会把未达到触发阈值的弱语音不断纳入噪声估计，从而继续抬高阈值。
修复限制向上学习为安静候选；下降可较快跟随。保留原 floor、DC 排除、160ms attack、400ms pre-roll、700ms hangover 和过长整段丢弃。
这不等于证明第二次板测未触发的唯一原因，也不是训练好的 VAD/AEC。

等待时每约五秒在消费线程输出 `mic-monitor-v219`：块计数、DC 去除后 RMS、原始峰值、噪声及触发 RMS。
PIO 回调不打印、不写录音文件；空闲无限等待不是消耗一个对话回合。
若继续无响应，请保留这些行：`blocks` 不增长与 `rms` 低于 `trigger-rms` 是不同问题。

## 5. Wi-Fi 修复及尚待证明的部分

1. RSN M2/M4 的 Key Length 按 RSN 回复语义置零，而不是误填 CCMP 密钥长度。
2. 同一握手重传 M1 保留 SNonce/PTK；旧 replay 不得回退计数器，也不得重新打开已完成握手。
3. 嵌套 NSH 密码输入显式关闭回显和内核行编辑，处理退格、Delete、Ctrl+U、取消和过长输入；不把控制字符混进密码，不截断、不去掉用户输入的空格，不输出密码。
4. 确认断连后尽快返回并指明 WPA2 阶段，不再空等 DHCP；M2 日志准确写“提交到 USB，等待 M3”，不称握手通过。

协议对照：[wpa_supplicant RSN 回复实现](https://w1.fi/hostapd/devel/src_2rsn__supp_2wpa_8c_source.html)。
主机测试执行生产 HMAC/PBKDF2/PRF 和报文构造，以 Python 标准库独立对照公开测试向量；伪终端执行真正的密码读取函数，验证不回显、编辑、取消及 termios 恢复。
**没有真实 AP 抓包或本轮上板证据，不能声称原 AP 失败一定只由以上字段造成，密码正确性也不能从旧日志推断。** 不自动降级加密、不重复尝试密码、不打印 PMK/PTK。

## 6. 上板复测顺序

烧写新镜像前保留现有 SD/UFS 数据；本轮生成的是普通镜像文件，不直接写物理盘或 UFS。

```text
aipet help
ufs status
aipet audio output i2s
aipet audio volume 50
aipet audio gain 5
echo init > /dev/a733-audio
echo tone=1000,500 > /dev/a733-audio
aipet mode local
aipet run
aipet audio service status
```

等待 READY，先说“你好”，确认完整语音；等再次 READY，再问一个需要 LLM 的问题，连续至少五轮。
说话和播放仍半双工；播报完重新 READY 后再说，不支持打断/AEC。
若一直没识别，等两个 `mic-monitor-v219` 周期并保存日志，不要反复 start。

```text
aipet stop
aipet audio service status
```

必须看到 `active=0`，然后再次 `aipet run`，再完成两轮；不要强杀任务。
再停止并确认 `active=0` 后测试 `aipet vision once` 的实拍、框和完整播报；最后再次启动对话。
也可在常驻模式说“识别物体”，等识物播报和再次 READY 后继续聊天，验证 ASR/YOLO/vocoder 的资源交接。不要并行启动 `camera live`。
常驻服务运行时，不要另开 `aipet ask` 或其他音频测试抢同一资源：外线程/外任务仍应被拒绝，本次只修复合法 owner 的误报。

若想单独复测英文尾句，先停止并确认 `active=0`，再运行：

```text
aipet audio local-speak-stream "Hello，后半句话也应该完整播放。"
```

网络单独测试，SSID 替换为实际名称，密码只在隐藏提示中输入：

```text
wifi disconnect
wifi connect YOUR_SSID 5
wifi status
ifconfig
```

若仍失败，提供 `wifi status` 与 `dmesg`，不要发密码。扫描不到与四次握手失败分开处理；可手动选择独立 2.4GHz 网络对照，不自动降级安全模式。
云语音凭据尚未配置时继续本地测试；连接 Wi-Fi 不等于云 ASR/LLM/TTS 已验收。

## 7. 构建、证据与恢复

使用已有 WSL/openvela 工作区与私有资产；不可把 `nuttx.bin` 当成整卡镜像。主要入口：

```bash
JOBS=12 bash tools/build-a733-product-v11-v219.sh out/product-v11-v219-build-r1 out/ort-static-v185-r2
python3 tools/test-voice-v216.py out/product-v11-v219-voice-base-r1 --build ../../quickly-openvela/cmake_out/cubie-a7z_nsh_v219_product_v11
python3 tools/test-product-gates-v219.py --base out/product-v11-v219-voice-base-r1 --build ../../quickly-openvela/cmake_out/cubie-a7z_nsh_v219_product_v11 --output out/product-v11-v219-gates-r1
python3 tools/check-product-full-v219.py --build ../../quickly-openvela/cmake_out/cubie-a7z_nsh_v219_product_v11 --build-report out/product-v11-v219-build-r1 --output out/product-v11-v219-full-r1 --firmware-commit f6531dbc0db75590ef76904afe4c786ec4bd8503
```

封装入口 `tools/seal-product-v11-v219.py`，依赖 `A733_PRODUCT_LLM` 指向锁定 SHA 的 MiniCPM5；它检查 26 项门禁和完整构建，再调用保留原有模型回读与 SWOUT1 保护的打包器。
所有输出必须是新目录/新镜像，已封存时会拒绝覆盖。需要重做同版时使用新的独立恢复工作副本，不删除现有通过版去凑目录。
打包仍使用锁定的 v210 私有母版，以及 `out/` 下的原始模型/ORT/摄像头资产；这些外部依赖不是无用文件，不要清空。

校验恢复包（WSL、项目根目录）：

```bash
python3 tools/checkpoint-a733-project.py verify out/checkpoints/a733-v11-v219-recovery-20261031 --check-references
```

恢复包保留 Git 历史和工作区源文件；大模型、镜像及私有运行库以外部路径/摘要关联，需连同原文件保留。
新模型推理速度、真实 AP 互通、连续多轮、停止/恢复、实际扬声器声音与端到端首音延迟，必须靠新镜像板测，软件门禁不能替代。
