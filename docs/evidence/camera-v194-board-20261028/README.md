# v194 一次真实模式回读通过（2026-10-28）

用户原始附件完整复制为 `serial.txt`，未整理或改写，SHA256：
`e433ecb54711b1481a96658a76dd1be7120564f4dff7eb92859286768b80d806`。

对应候选发布提交 `b96c0bc468cee17627645c1e138fdf37e7d5f09e`，固件源码 `afd8e59`。
脚本 1357B、Image 7041568B/CRC32=`4de89c0c`、外置配置 660B，
与独立封存的 v194 制品一致；本次 PMIC 交接字节为 `03/00/ff/17/c1/0d`。
正常进入 NSH，24 个构造函数执行完成。

实测命令：

```text
camera control prepare
camera control mode-experiment
camera control status
```

- prepare：state=1、owned=1、elapsed=13ms、错误为零。
- 模式实验：实际 ID=`0214 match=1`，blob-error=0、writes=153、reads=17、
  verified=14/14、last-reg=0100、error=0。
- 结束：state=2，stage=`mode-readback-passed-controls-restored-NOT-capture`，
  elapsed=102ms，error=cleanup=command-error=0、owned=0、bus-quarantined=0。
- 紧接着 status 输出与实验结束状态一致。

通过范围是这一次实际传感器 ID/模式写入/关键字段回读/恢复，不是连续稳定性、
物理电压/时钟测量、MIPI 链路、CSI/DPHY/DMA/ISP、RAW 帧或 YOLO/屏幕联动验收。
102ms 是该控制实验时间，不能换算摄像头 FPS。
日志开头有 UFS Check Condition，但后续正确识别 SD GPT、加载配套脚本并完成此实验；
该片段没有 BOOT0/version，不能据此单独判定 U-Boot 来源或证明 UFS 问题已修复。
不执行 UFS 引导清理、不修改已通过镜像。

后续先实现接收端的时钟/复位、MCSIB/CSI1 路由、DPHY、独立 DMA/IOMMU 映射、
缓存及有界停机，再验证真实 RAW 内容，最后对接像素预处理、YOLO 和 SPI 展示。
