# v1.2fix／v221 镜像交付（2026-11-02）

已完成独立完整构建、28项软件门禁、摄像头／NPU ABI／ISA／ELF／内存布局审计和最终镜像回读。**尚未执行本镜像板测，稳定版仍为 v1.1／v219**。

## 文件与校验

- 镜像：仓库上一级 `openvela-a733-cubie-a7z-sd-v1.2fix-v221-preview.img`
- 大小：2,147,483,648字节（2GiB）
- SHA256：`c07d26842cc0ddaa4a1e16b957c2c06297c94980c1af9c92bd6a303f5d8bac2c`
- 同目录 `.img.sha256` 为校验文件；Windows独立 Get-FileHash 与 Linux 结果一致。
- 内核 SHA256：`0618fa61eb30683607ed77f25118e3a9c5711f7ba1af950445050e263be16727`
- 固件源码恢复点：`716ac8c5b070355abc53db2720976c837bcbfa3c`（包含此前 b6000ba 运行时代码）；封装工具提交 `aa7183e`。

包含 v220 预览的默认本地历史0、常驻权重、精确前缀KV及工作区复用，叠加 [v221 STM32 新协议对接](PRODUCT_V1_2FIX_V221.md)。主语音仍走 I2S；UART4 专供115200下位机通信。模型、SOUL人设、YOLO六路模型和SWOUT1启动脚本都从最终镜像回读核对；SD包含 MiniCPM5 备用模型，既有UFS仍优先，不迁移或擦写UFS。

## 烧录与先测命令

烧录会覆盖所选SD卡，请先备份配置和个人文件，核对目标盘，勿误选UFS或电脑磁盘。本次仅生成普通镜像文件，没有操作物理盘。旧 v1.1／v220 原镜像及源代码恢复点保留。

新固件启动后，先不解锁运动：

```text
aipet help
aipet mcu start
sleep 1
aipet mcu status
aipet mcu ping
aipet mcu config-get
aipet mcu stepper-stat
aipet mcu link-stat
aipet ask "电机状态"
aipet ask "链路状态"
aipet ask "下位机配置"
aipet mcu stop
```

`aipet mcu status` 应多出 `mcu-v221: upstream=efdb437` 行；帮助应包含 `config-get`、`stepper-stat`、`link-stat`。如仍只有旧帮助，先确认实际启动的SD／内核，不要把旧固件误判为新命令故障。不要把 Markdown 的围栏行（三个反引号）粘贴进 NSH。

UART接线：A7Z物理16脚PJ24/TX→STM32 PA10/RX；物理18脚PJ25/RX←PA9/TX；共地；115200、8N1、无流控。不要同时接旧 UART TTS 模块，也不要直接相连两板供电引脚。

只有确认 fault=0、有效心跳、下位机配置与实际硬件相符，且已把电机悬空／做好急停后，再手动：

```text
aipet mcu arm
aipet mcu move forward 0 16
aipet mcu stepper-stat
aipet mcu stop
aipet mcu status
aipet mcu disconnect
```

STOP后会解除授权；这不是故障。没有自动 arm、配置写入、清故障或运动重试。ACK不是运动完成；位置是步进脉冲计数，不是编码器实测。其他配置与可选MCU TTS命令见v221对接文档，不作为默认测试必做项。

## 新回传 v220 日志的结论与已知限制

[2026-11-02板测反馈](evidence/product-v12-v220/board-feedback-20261102.md)已归档。v220的 UART 已连通、两次小步命令获ACK；但“电机状态”没有规则，因此进入普通LLM解释并最终长回复超时。v221新增精确MCU查询路由，避免这条特定请求误入LLM。

**普通长回复流式链路的 -110 尚未作为本轮通用问题修复。** v220该条首次可见29.074秒包含9.905秒权重冷加载及冷预填充，不能当热态性能，也不能据此宣称v221达到3～5秒首音。本轮不更改ASR／LLM／TTS数值路径或全系统调度；不把残句、模拟PCM和内部计时当作完整声学通过。

## 构建、证据与恢复

[封存证据](evidence/product-v12fix-v221-image/README.md)。SDK暂存316项已按字节还原。重新执行的基础19项加产品／Wi-Fi／预览策略／新MCU，共28项门禁；真实GGUF参考报告明确继承v220，逐个验证数值源文件不变，product_runtime只允许反转一条版本banner后完全同哈希，不将旧oracle冒充重跑。

实际命令在WSL仓库目录执行；输出目录及缓存必须是新的，不能覆盖旧版：

```bash
JOBS=12 bash tools/build-a733-product-v12fix-v221.sh out/product-v12fix-v221-build-r1 out/ort-static-v185-r2
python3 tools/test-voice-v216.py out/v221-voice-base-r1 --build ../../quickly-openvela/cmake_out/cubie-a7z_nsh_v221_product_v12fix_preview
python3 tools/test-product-gates-v221.py --base out/v221-voice-base-r1 --build ../../quickly-openvela/cmake_out/cubie-a7z_nsh_v221_product_v12fix_preview --output out/v221-product-gates-r1
python3 tools/check-product-full-v221.py --build ../../quickly-openvela/cmake_out/cubie-a7z_nsh_v221_product_v12fix_preview --build-report out/product-v12fix-v221-build-r1 --output out/v221-full-link-r1 --firmware-commit 716ac8c5b070355abc53db2720976c837bcbfa3c
A733_PRODUCT_LLM=/ABS/PATH/MiniCPM5-1B-Q4_K_M.gguf python3 tools/seal-product-v12fix-v221.py --build ../../quickly-openvela/cmake_out/cubie-a7z_nsh_v221_product_v12fix_preview --build-report out/product-v12fix-v221-build-r1 --voice-report out/v221-product-gates-r1 --full-report out/v221-full-link-r1 --firmware-commit 716ac8c5b070355abc53db2720976c837bcbfa3c
```

这些输出目录本次已存在；重跑须选择新目录并版本化封装路径，不删除证据来重用名称。底层SDK、私有模型、v210封装基底和sensor参考沿用既有可恢复资产。

最终ELF／Image／map／配置封存：`out/v221-product-v12fix-release-r1`。
镜像发布标签：`a733-v1.2fix-v221-image-20261102`；本地源码恢复包：`out/checkpoints/a733-v12fix-v221-image-20261102.bundle`。原2026-11-01源码标签与bundle不覆盖。未提交PR、未推送。
