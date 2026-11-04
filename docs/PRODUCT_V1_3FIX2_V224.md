# v1.3fix2／v224：常驻短句结束与安全丢弃

2026-11-02。v223板测证明独立ASR、长句TTS和LLM预加载成功，但常驻“你好”“前进三步”都被录满8秒保护丢弃。详见[原始反馈](evidence/voice-v223-board-20261102/README.md)。这不是本次日志已证明的LLM/TTS推理失败，也不能据最后一行日志断定系统死锁根因。

## 修改范围

1. 结束判定不仅要求能量超门限，还要求80ms连续活动才能重置700ms静音计数。10～70ms短促脉冲不再无限延长录音；脉冲期间不计静音、每块PCM仍原样保留。初始160ms启动确认、400ms前滚、8秒硬上限保留。尚不具备训练VAD、AEC或打断能力，弱尾音／噪声仍需板测。
2. 超长音频已被明确拒绝且采集／consumer都健康结束时，不再运行注定会丢弃的ASR尾部推理。丢弃只销毁CPU识别stream、不触碰硬件、不重新打开模型；下一轮仍有原有一次初始decoder调用和全部状态初始化，并重新校准噪声。不把NPU错误或所有权不明当成可恢复轮次。
3. 新增 `gate-end-v224` 短脉冲／确认／重置计数，以及 `resident-ASR-finalize`、`resident-ASR-discard-no-final-NPU` BEGIN/END日志。它们用于区分最后一步进展，不能代替新的卡死现场或实际声音验收。
4. v223的长句TTS修复、v221 MCU协议、v220低延迟无历史设置、YOLO／SPI、既有云服务保留；不修改模型、NPU驱动／ABI、CPU全局调度或UFS布局。自然英文仍未提供，Latin为中文读音拼读；未加入尚待确认的MiMo新方案。

本次按 embedded-npu-porting 技能审查了应用端NPU生命周期和完成后清理；没有进行新芯片bring-up，也没有将主机桩、参考捕获重放当作板上NPU验收。

## 制品状态

源码固定 `e54e867cfb567e1cc6c124572e0861dacd538135`，分支 `preview/v1.3fix2-v224`。独立完整构建、33项门禁、最终ELF／ABI／ISA／摄像头检查及镜像回读已通过，SDK316项按字节还原；[最终封存证据](evidence/product-v13fix2-v224-image/README.md)。

- 镜像：仓库上一级 `openvela-a733-cubie-a7z-sd-v1.3fix2-v224-candidate.img`，2,147,483,648字节。
- SHA256：`30ca81ce9d0aeb846207b0f816603bfcafb389804763a393c4a18186cb0be25b`，旁有 `.img.sha256`；Windows独立Get-FileHash与Linux最终封装值一致。
- 内核：7,242,664字节，SHA256 `49c7f8dca641bdeefd335e5d73732e22c5eee34a7c3226079c3fa327ffc36443`。实际二进制已验证v224产品/服务及新端点／丢弃标识，不是重命名旧镜像。
- 本地恢复标签 `a733-v1.3fix2-v224-image-20261102`；源码bundle `out/checkpoints/a733-v13fix2-v224-image-20261102.bundle`。源码bundle不包含私有模型与SDK；本地模型和旧版完整资产均需保留。

旧v219稳定、v220、v221和v223镜像与标签保留。没有新板测、物理盘写入、PR或推送，最新稳定仍v1.1/v219。

## 上板优先回归

接线沿用[上一版完整接线](PRODUCT_V1_3FIX_V223.md#接线)：PB5=物理12/BCLK；PB6=物理35/LRCLK；PB7=物理40/DIN到功放；PB8=物理38/麦克风SD。不要再交换PB5和PB6。

只输入命令本身，不输入Markdown的三个反引号或 `text`。烧录前先备份所选SD，勿选UFS或电脑磁盘。

```text
aipet help
ufs start
ufs status
aipet mode local
aipet audio output i2s
aipet audio volume 50
aipet run
aipet audio service status
```

帮助／服务状态必须带 **v1.3fix2／v224**。冷启动先等模型准备；看到READY／MIC ON再说“你好”，说完静默至少1秒。先不要混入运动或识图。应看到 `limited=0`，然后 `[asr backend=local]`、LLM和完整声音。再说一遍“你好”，检查第二轮。

如果再次8秒拒绝或停住，不重复run、不强杀线程；尝试：

```text
aipet audio service status
ps
aipet stop
aipet audio service status
```

把最后一条 `BEGIN stage=...`、所有gate计数、status与ps发回。状态/ps也完全无法输入时，记录最后串口内容；只在必要时断电，不能把它当作已经安全释放NPU。

STOP后等active=0，再run，复测一轮。当前半双工，扬声器讲话时不接受打断。

## 动作测试：单独授权

“前进三步”不是普通聊天，会走动作路由。服务启动先disarm，因此必须在READY后，通信有效、fault=0、配置与实际硬件一致且有急停保护时，才手动：

```text
aipet mcu ping
aipet mcu stepper-stat
aipet mcu arm
```

随后说“前进三步”，核对ACK与 `aipet mcu stepper-stat`；再说“停止”。禁止因播报异常重发运动。ACK是接收，不是实测位移；STOP后解除授权正常。没有自动arm／清故障／运动重试。

## 软件检查与恢复

新增同合成脉冲对照：v223输出800块且limited，新版172块且正常结束。生产采集线程＋FIR＋session回归、10～70ms脉冲／80ms续说／500ms停顿／超长／取消／错误／保留owner均执行。

真实ASR适配器使用216条Linux参考捕获重放，三次部分循环状态丢弃后再跑6轮样例；输入输出逐个核对，discard零新推理，下一轮原有decoder初始化一次。首个测试误断言reset也应零调用，已按真实decoder-open语义更正；失败日志r1保留，r2完整通过，不是修改输出容差或跳过状态检查。

实际完整报告：`out/product-v13fix2-v224-build-r1`、`out/v224-voice-base-r1`、`out/v224-product-gates-r1`、`out/v224-full-link-r1`。8个修复单元另以实际目标参数编译；继承的真实LLM数学oracle逐源同哈希，仅版本banner归一化，不冒充新板测。旧文档与失败日志不删除。

完整固件封存 `out/v224-product-v13fix2-release-r1`，封装报告 `out/product-v13fix2-v224-package-r1`。构建入口 `tools/build-a733-product-v13fix2-v224.sh`，目标缓存 `quickly-openvela/cmake_out/cubie-a7z_nsh_v224_product_v13fix2`；基础19门禁仍为 `tools/test-voice-v216.py`，其后跑 `tools/test-product-gates-v224.py`；完整审计 `tools/check-product-full-v224.py`，封装 `tools/seal-product-v13fix2-v224.py`。这些输出目录已使用，重跑必须新建版本化目录，不能删除原证据复用名称。
