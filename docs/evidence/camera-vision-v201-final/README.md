# v201 主机联调工具：最终证据（2026-10-29）

状态：**HOST_CHECKS_PASSED_NOT_BOARD_TESTED**。主机工具源码提交
`f4aab9bd97d5688a7490dd715e733d5f08af7d39`，未连接开发板，没有新固件或镜像。

- [Windows 最终摘要](../camera-vision-v201-windows-final/summary.json)：Python3.12，14组测试中
  12组通过，PTY/gcc两组按平台跳过。独立重算 v200 2 GiB 镜像 SHA，通过。
- [WSL 最终摘要](../camera-vision-v201-wsl-final/summary.json)：Python3.14，14组全部通过；
  真实 PTY 仅 exec 到本地模拟程序，无网络；实际 C 导出器四种 Bayer 顺序在 ASan/UBSan 下
  生成夹具，完整交给新离线校验器；旧 RAW 帧、导出及生产/消费 MQ 回归全部通过。
- 两份摘要的工具/测试/收集器 SHA 完全一致；全部287个固件文件与固定 SHA 的 v200
  恢复清单逐字节相同。旧 ORT 来源 JSON 的 WSL 行尾诊断见 [r1说明](../camera-vision-v201-wsl-r1/README.md)，
  本轮没有修改该文件。r1/r2早期报告保留，最终结论只以 final 两组为准。
- v200 内核 SHA：`19e2e0772816a44ee9ec671c3eac92f7978b2587a8a8c502c7abd60919cf96c0`。
- v200 镜像 SHA：`a729b7442ffc764063b191d8ac3ebb38c4a2812c18f21cc9dacf2cd02fbaaa33`。

## 恢复点

最终标签：`a733-v201-host-vision-final-20261029`，前置 candidate 标签保留。
本地封存目标：`out/checkpoints/a733-v201-host-vision-final-20261029`，包含源码与 Git 历史，
旧镜像及 v200 十项封存输入以外部文件 SHA 引用保存，不复制大模型和镜像。

复验与实际恢复到**全新目录**的命令：

```text
python tools/checkpoint-a733-project.py verify out/checkpoints/a733-v201-host-vision-final-20261029 --check-references
python tools/checkpoint-a733-project.py restore out/checkpoints/a733-v201-host-vision-final-20261029 out/restore-check-v201-host-vision-r1 --with-git
```

它们不回退或覆盖当前工作树。恢复工具在标准输出返回 JSON 回执，不会生成额外回执文件。

## 仍待硬件证明

v200 自动 SWOUT1 检查/单次受控开启/读回、真实 RAW 采集和颜色、完整目录导出、实际 SSH
联调以及屏幕肉眼确认都未在本轮执行。NPU模型准确率、连续取流、真实FPS、ISP/AE/AWB
没有新增验收结论。语音等其他链路原有待测状态不变。

详见 [操作和边界](../../a733/IMX214_V201_HOST_COORDINATOR.md)。
