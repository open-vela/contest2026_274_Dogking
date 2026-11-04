# v1.2fix／v221 软件证据

日期2026-11-01；运行时代码恢复点 `b6000ba43e26a8d8c2511190dabd45427cf7a799`。
上游固定 `efdb437948eda62cbc782692eb8fd291e5d914ae`，不是会继续变化的 main。

## 已执行

- `tools/test-mcu-v221.py --output out/mcu-v221-tests-r6` 通过，原始输出完整保留；失败迭代 r1～r3 也保留在 out。
- `software-summary.json` 为软件报告，源文件、上游归档及日志 SHA256 绑定；`state.log` 与 `pipeline.log` 为原始输出。
- 603组帧与新版 STM32 C 编码器逐字节一致；ASan／UBSan、CRC／解析边界、查询 SEQ/OP/长度、序号回绕、同批 ACK+TOF+双路事件、无效距离与负位置、TTS积压 ACK02、故障和配置变化保护通过。
- 实际应用经虚拟串口验证规则／Agent→UART链路，包括新只读意图、跨意图运动拒绝、重复工具消费、手动配置确认、非法配置拒绝、可选下位机 TTS 和 TOF 周期设置。
- 保留原 ASR／规则／Agent／UART／TTS 联合回归；迟到 ACK、NAK、错误序号、心跳丢失、故障先于 ACK 均不恢复授权；运动不自动重试。`openvela-boundary` 通过。
- `tools/check-mcu-arm-v221.py` 使用 v220 独立缓存的真实 NuttX ARM 命令、当前 overlay 源码编译10个单元，含本轮 `mcu_agent_tool.cxx`。版本标识和预览配置检查通过，SDK源文件没有修改。`mcu_link.cxx` 最大静态栈帧704字节，工具224字节；所检查各函数静态栈帧均≤4096字节。这不是任务总栈用量证明。

报告 SHA256：

- `software-summary.json`：`d19d3d47a19486491829a06f36dfaadb9b96bfc89758554bb3b3d80d60582073`
- `arm-summary.json`：`7c714d11a44c9fc41494071b99099608307a1c0b9658f3f162edb349f4af7251`

## 未执行／不声称

本轮未完整链接／封装新整卡镜像，未烧录、未执行 STM32 真机运动、TOF 防护、真实仲裁抢占或声学验收。PCM／云调用和 MCU 是主机模拟；ARM 编译不是目标板执行。没有重跑或重新标记 v220 的整机门禁／真实 GGUF 报告。

## 恢复

预览分支 `preview/v1.2fix-v221`，标签 `a733-v1.2fix-v221-preview-20261101`。
本地源码 bundle：`out/checkpoints/a733-v12fix-v221-20261101.bundle`，使用 `git bundle verify` 验证。
bundle 包含 v221 及已有 v219 稳定／v220 预览标签，不包含外部 SDK／模型／镜像。
SDK、模型和完整旧制品仍按原 v220 恢复目录保存，v1.1 稳定指针没有改动。

使用新目录恢复，不覆盖当前工作区：

```bash
git clone /ABS/PATH/a733-v12fix-v221-20261101.bundle dogking-v221-recovered
cd dogking-v221-recovered
git switch --detach a733-v1.2fix-v221-preview-20261101
```

上游固定归档另保留在原仓库 `out/stm32-v221-efdb437.tar.gz`，可用对应 GitHub API commit tarball 重新取得并校验固定 SHA256。上板步骤与独立构建入口见 [v221 对接记录](../../PRODUCT_V1_2FIX_V221.md)。不提交 PR、不推送。
