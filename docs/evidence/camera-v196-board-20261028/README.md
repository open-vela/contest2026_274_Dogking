# v196 一次真实 MIPI 包接收实验通过（2026-10-28）

用户原始附件完整复制为 `serial.txt`，未改写或整理内容。SHA256：
`3416af45424496aa740be3152fd2490ead366f8388269a2c0ca995adc42772a5`。

## 实际观察

```text
camera control prepare
camera control link-experiment
camera control status
```

- NSH 可用，日志包含最后一个构造函数完成标记；本段未覆盖全部构造函数过程。
- prepare：state=1、owned=1、elapsed=13ms，全部错误为零。
- 真实 sensor ID=`0214 match=1`，无 TWI 隔离；固定模式 writes=153、reads=17、
  verified=14/14，blob-error=0、mode error=0。
- 已尝试 STREAM_ON，on-error=0；500ms 收集窗口内 raw-headers=119，
  fs-observations=14、fe-observations=14、status=0000004f、errors=00000000。
- 结束 stop-error=0，receiver-error=cleanup=0、receiver owned=0；
  control error=cleanup=command-error=0、owned=0、state=2。
- receiver-first 与 receiver-cleanup 均全零；receiver elapsed=504ms，控制实验
  elapsed=608ms。结束后的 status 与实验结束结果一致。
- stage=`MIPI-packets-observed-NOT-RAW-frame`；ready=0 是结束后接收端已停用，
  不是本轮准备失败。不能把 prepare 时的 owned=1 与结束后的 owned=0 混淆。

这一次日志证明：ID/模式→接收端准备→开流→新鲜包级事件→停流→恢复的组合通过。
它支持 v196 对 v195 接收准备/恢复故障的修正效果，**不是重复冷/热启动或长稳验收**。

## 制品关联及限制

上轮封存发布提交 `80f3930f105e7c76d46e5c54fad9b6e8bd7f961e`，固件源码提交
`da2fafac5c99cace6f0a6c0094c6b6aca8dfe98a`。关联的 v196 镜像 2147483648 字节，
SHA256=`b29c27c98b5494a079b84892e3d8bc34b1e7e99298b011e355fe004d526f8ba1`。
镜像与源码/配置/交接脚本的构建核验见 `../camera-v196-final/summary.json`。

本次日志自报 `camera-control-v196`，但**没有本次 U-Boot Image 加载大小、CRC 或
SHA256**，不能声称已从这段日志独立复验精确二进制身份；保留此证据缺口。
没有电压/时钟物理测量，firmware-retained 电源假设不升级为严格测量资格。

119/14/14 是锁存状态的观察计数，不是完整帧数，不能换算 FPS。
没有访问 parser/ISP/DMA/IOMMU，没有像素帧/RAW 文件，未证明颜色、画质、YOLO 或
SPI 摄像头画面成功；PHY debug/frequency 原值也不冒充物理仪器测量。
UFS 引导问题仍暂停，语音待测状态不变，没有改动固件或旧镜像。

## 下一门槛

先实现 CSI1 parser 路由与独立 CSI 主设备的 DMA/IOMMU 所有权、地址转换、缓存维护和
有界停机，取得带保护区校验的单张真实 RAW 帧，验证实际内存格式/尺寸/内容后导出。
现有 `a733_csi_raw10_frame_plan` 只有 1920×1080 RAW10-in-LE16 的数值规划，
**不包含分配器、实际映射或硬件寄存器执行**，不能直接当成已完成的取帧后端。
之后才接 Bayer→RGB、NPU YOLO 和 SPI/LVGL 展示。

通过版标签：`a733-v196-mipi-packets-board-pass-20261028`。
恢复包：`out/checkpoints/a733-v196-mipi-packets-board-pass-20261028`；包含源码/Git
历史和现有 v196 镜像/摘要、v194 模式通过回退镜像、十项封存资产的校验引用。
这些引用不是硬件重新烧录，也不表示完整摄像头链路完成。
