# 小派 v1.2fix／v221：STM32 新版协议对接

> 后续交付：2026-11-02 已完成 [v221完整测试镜像](PRODUCT_V1_2FIX_V221_IMAGE.md) 与28项门禁／完整链接／回读校验。下方“本轮未生成镜像”特指2026-11-01源码同步批次，作为过程记录保留；最新制品以镜像交付文档为准。

日期：2026-11-01。基线为 v1.2／v220 预览；**最新稳定版仍是 v1.1／v219**。
本轮仅同步 UART4 控制应用、Agent 工具、版本标识和软件测试，不改 CPU 调度、I2S、NPU、摄像头、UFS 或模型数值路径。历史文档和恢复版本保留；不提交 PR、不推送。

源码恢复点 `b6000ba43e26a8d8c2511190dabd45427cf7a799`，预览标签 `a733-v1.2fix-v221-preview-20261101`。[软件回归／ARM编译封存证据](evidence/product-v12fix-v221/README.md)已保存：新版603组编码器对照、实际应用虚拟串口／Agent联合回归及10个真实 ARM 单元编译通过。尚未新整卡构建或实机验收。

## 1. 对照来源

- [STM32 仓库](https://github.com/hxy299/STM32C8T6Mini-Play)，固定到 `efdb437948eda62cbc782692eb8fd291e5d914ae`。
- 主要依据：[上位机对接手册](https://github.com/hxy299/STM32C8T6Mini-Play/blob/efdb437948eda62cbc782692eb8fd291e5d914ae/docs/上位机对接手册.md)，同时检查真实 `Service/link_service.c`、协议头和实现。
- `tools/mcu_link_example.c` 仅作参考，没有直接替换 A7Z 常驻 UART worker。参考程序会超时重发，但 MCU 没有 SEQ 去重；**A7Z 不自动重发运动命令**，防止同一动作执行两次。
- GitHub API 源码归档 SHA256：`a17b36b143c8906e13ddd769189c6a99ede841fc7ceb13a18385a78374fd737c`。测试执行前校验归档，并对照其中协议、配置、服务、手册和参考代码的解包字节。
- 手册中提及的旧 A7Z 默认 9600 不适用于本项目：现有产品本来就使用 **115200、8N1、无流控**；没有把 UART4 改回 9600。

## 2. 接线

全部断电后接线，使用 3.3V UART 电平，共地；不要把两板的供电引脚直接相连。

| A7Z 40-pin 排针 | 信号 | STM32 |
| --- | --- | --- |
| 16／PJ24 | UART4 TX | PA10／USART1 RX |
| 18／PJ25 | UART4 RX | PA9／USART1 TX |
| 例如 20 | GND | GND |

UART4 节点为 `/dev/ttyS4`，专用于下位机；不同时挂接旧 A7Z UART TTS 模块。主语音仍走 I2S。STM32 自身 USART2 的可选 TW-TTS 模块是另一条链路，不能混淆。

## 3. 新增与修正

| 功能 | A7Z 命令／行为 | 协议 |
| --- | --- | --- |
| 读取配置 | `aipet mcu config-get`，不写 Flash | OP08 → OP87／4B |
| 电机诊断 | `aipet mcu stepper-stat`，左右有符号位置、剩余脉冲、Hz、运行／冷却标志、热跳闸、故障 | OP09／24B |
| 链路诊断 | `aipet mcu link-stat`，好坏帧、字节、溢出、帧龄、owner、双方活跃及蓝牙初始化 | OP0A／32B |
| TOF 状态 | `aipet mcu status` 显示最后广播的 mask、距离、数据年龄；`-1` 表示禁用／无效，绝不当作 0mm | EVT92／9B |
| TOF 周期 | `aipet mcu tof-poll 50`；只改轮询周期，不启用传感器 | OP04／1B，10～255ms |
| 配置写入 | `aipet mcu config-set 4f confirm`，仅控制台，先 disarm／请求 STOP | OP07／4B，已知位 00～7f |
| 可选下位机播报 | `aipet mcu tts "测试"`；UTF-8 正文≤190字节，原始 TW-TTS 帧外包协议 | OP03；不是默认 I2S 输出 |
| 清故障 | `clear-fault` 改用手册标准 flag=0；显式 `blind-ack` 才附加 5A | OP05；不自动调用、不自动 arm |
| 运动 ACK | 解释第二字节为预计距离，单位 0.1mm；255 为饱和值 | ACK81／2B；不是运动完成 |
| 事件 | EDGE91、OBSTACLE95、FAULT93、CONFIG_CHANGED96 更新缓存并关闭运动授权；MOTION_DONE94 保存左右位置 | 广播必须 TYPE03、SEQ0 |
| 序号／响应 | 命令序号从65535回到1，跳过0；查询同时匹配 TYPE、SEQ、OP 和精确长度 | 错序号／短帧不能完成请求 |

电机位置是下位机步进脉冲位置，不是编码器实测。两路 MOTION_DONE 都是 SEQ0 广播，不能据此断言某次调用完整执行成功。TTS ACK00 和 ACK02 都表示入队，后者提示积压；**没有播放完成事件**。NAK02 表示没有入队，不混同 ACK02。

诊断查询可在未 arm 时执行。规则路由新增精确说法：`电机状态`／`查询电机状态`、`链路状态`／`查询链路状态`、`下位机配置`／`查询下位机配置`。Agent 工具增加对应只读 action，仍限制本轮原文授权、一次消费、无额外参数。

## 4. 保留的安全边界

1. 250ms 常驻心跳沿用原实现，不依赖 LLM／TTS 命令返回；PONG 新鲜度 750ms。MCU 的1000ms A7Z失联保护仍由下位机执行。软件周期不能代替实机调度验收。
2. 开机、重连、故障、超时、STOP、配置变化均不自动恢复运动授权。`arm` 为五分钟；`arm session` 是操作者显式选择，仅当前连接会话有效。
3. 默认不写配置、不清故障、不 blind-ack、不改变 TOF 启用集。配置写 Flash 失败可能已在 MCU RAM 生效；显示错误后应重新查询，不能自动重试。
4. 新固件完整能力期望 `impl=3f`；旧的部分实现按能力位检查，不因为不是3f就盲目拒绝只读诊断。老固件不认识新查询时会拒绝／超时，不凭空填充数据。
5. A7Z 继续限制单次1～1024步、速度档0～3、舵机±45°和100～2000ms；没有暴露下位机65535连续运动、单电机校准 OP0B 或强制退出冷却给 Agent。
6. MCU owner=0为A7Z、1为蓝牙、255为无；仲裁拒绝按 NAK 真实结果上报。03/01须查冷却状态，不能把 detail=01直接当 fault=01。
7. 配置写入、TOF周期修改、下位机 TTS 仅是手动 CLI；没有偷偷加入自主运动规则或更换现有 I2S 播报路径。TOF 状态为带年龄缓存，不是连续可用或全方向防跌落承诺。

## 5. 测试与构建

WSL 仓库目录下：

```bash
python3 tools/test-mcu-v221.py --output out/mcu-v221-tests-NEW
python3 tools/check-mcu-arm-v221.py \
  --build ../../quickly-openvela/cmake_out/cubie-a7z_nsh_v220_product_v12_preview \
  --output out/mcu-v221-arm-NEW
```

测试需要前述固定源码归档放在 `out/stm32-v221-efdb437.tar.gz`，并完整解压至 `out/stm32-v221-source-efdb437/`。不依赖主分支继续变化。输出目录必须新建；失败日志不删除。

软件测试包含 ASan／UBSan、603组真实 STM32 编码器逐字节对照、CRC／解析边界、实际应用虚拟串口收发与 ASR→规则／Agent→UART→TTS 路由；新查询、配置手动确认、非法配置拒绝、下位机 TTS、TOF 周期、序号回绕及同批多事件均有检查。PCM／云调用和 MCU 硬件是模拟的，不能当作机械／声学验收。

独立完整构建入口：

```bash
JOBS=12 bash tools/build-a733-product-v12fix-v221.sh \
  out/product-v12fix-v221-build-NEW out/ort-static-v185-r2
```

使用独立 `cubie-a7z_nsh_v221_product_v12fix_preview` 缓存，保留 v219／v220。**本轮交付范围为源码、软件回归和实际 ARM 单元编译；尚未生成或板测 v221 整卡镜像。旧 v220 镜像不包含这些新命令。** 不拿旧27项封存报告冒充 v221 整机通过。

## 6. 后续上板验证顺序

以下命令须在包含本轮源码的新固件上运行。先不装负载，确保电机悬空／有急停与安全空间。只读阶段不需要 arm：

```text
aipet mcu start
sleep 1
aipet mcu ping
aipet mcu config-get
aipet mcu stepper-stat
aipet mcu link-stat
aipet mcu status
aipet ask "电机状态"
aipet ask "链路状态"
aipet ask "下位机配置"
aipet mcu stop
```

先核对 cfg／fault／owner／TOF 年龄。默认配置常见4e；4f另外打开舵机，需要实际装有舵机；Flash旧配置不会随重新烧录自动恢复默认。不要为了测试盲目写4f或清故障。

确有需要且下位机无故障，再**手动**解锁并小步试验：

```text
aipet mcu arm
aipet mcu move forward 0 16
aipet mcu stepper-stat
aipet mcu stop
aipet mcu status
aipet mcu disconnect
```

本地语音／联网语音沿用既有路由，明确运动原文仍受授权与故障保护；云端仍需独立凭据。断线、故障、TOF告警、蓝牙抢占和配置改变后的实机急停行为待联合 STM32 验证。
