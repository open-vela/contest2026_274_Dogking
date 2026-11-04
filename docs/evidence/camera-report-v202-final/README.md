# v202 最终主机证据（2026-10-29）

状态：`HOST_REPORT_CHECKS_PASSED_NOT_BOARD_TESTED`。工具源码提交
`41da1866362d78a835a07c9f0560bbf52095a022`。

- [Windows最终摘要](../camera-report-v202-windows-final/summary.json)：v202共12组，11组通过，
  gcc/实际C对照组按平台跳过；v201基线14组中12通过、PTY/gcc两组跳过。v200镜像SHA重算通过。
- [WSL最终摘要](../camera-report-v202-wsl-final/summary.json)：v202全部12组、v201全部14组通过，
  以及旧RAW帧、导出和MQ生产/消费回归通过。实际C导出器四种Bayer顺序在ASan/UBSan下生成
  CPU夹具，Python独立重算的模型RGB与显示预览均逐字节相同。
- 两个平台工具/测试/收集器源码SHA相同，合成示例PNG/HTML字节SHA相同。
- `v201-base/summary.json` 证明全部287个固件文件与原v200恢复清单的字节SHA相同；
  内核、内存布局、SPI/MIPI/NPU驱动及启动脚本未改。没有新固件镜像，没有开发板连接。
- r1初步日志与示例保留；最终12组结果及后来补充的预览损坏、带日志整体验证仅以final为准。

合成外观示例明确标为 `SYNTHETIC_UI_DEMO_NOT_CAMERA_OR_NPU`，不生成真实验收的REPORT.json，
无摄像头数据、无NPU框，不能作为实机演示。图像示意已做本地像素查看，HTML为静态无JS页面。

## 恢复

最终标签：`a733-v202-raw-report-final-20261029`。
本地封存：`out/checkpoints/a733-v202-raw-report-final-20261029`。
保存源码/Git历史，旧镜像和v200十项封存输入以外部SHA引用保留；不删除旧版本。

```text
python tools/checkpoint-a733-project.py verify out/checkpoints/a733-v202-raw-report-final-20261029 --check-references
python tools/checkpoint-a733-project.py restore out/checkpoints/a733-v202-raw-report-final-20261029 out/restore-check-v202-raw-report-r1 --with-git
```

恢复工具只向全新目录写入，在标准输出返回JSON回执，不覆盖当前工作树。

## 未验收项

v200自动SWOUT1检查/开启/读回、真实RAW来源/内容、实际颜色和Bayer排列、板上v201 SSH、
LCD肉眼效果、YOLO准确率、连续取流和真实FPS都没有新增板测结论。语音等其他链路状态不变。
统计告警只辅助定位，不能自动诊断传感器坏、电源异常、过曝原因等，也不会修改传感器配置。

[使用方法与边界](../../a733/IMX214_V202_OFFLINE_RAW_REPORT.md)。
