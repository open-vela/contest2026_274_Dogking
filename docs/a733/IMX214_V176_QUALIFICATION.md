# Radxa 13M214 / IMX214：v176 身份门禁与真实硬件缺口

2026-10-23，接续已封存的 v175。用户更正型号为 **13M214**，不是12M314。
本次尝试推进移植，但**尚不能交付完整 MIPI 摄像头驱动/实时 YOLO**。
v175 UFS 镜像不包含或启用本节新探测核心，不因后续摄像头实验变化而失去恢复性。

## 发现的关键差异

官方 V1.11 原理图单 CSI 插座 J5，31pin / 0.3mm，MCSIB 四条数据lane与一个时钟对；
TWI3 SCK/SDA 为 PE3/PE4，MCLK1 为 PE5，STBY/RESET 为 PE8/PE9。
插座带板上3V3/5V供电；并非直接把旧 OV13850 的三路传感器LDO配置写到摄像头裸芯片。
PE/PK/MCSI 电平来源与模块内部稳压必须分别核对，不能把插座供电等同逻辑电平。

当前 Radxa overlays main 的 `cubie-a7z-radxa-camera-13m-214-cam2.dtso`
却指定 **TWI4 PE10/PE11、MCLK0、PE12/PE13、CSI0/MIPI0**。该文件标题明确为 CAM2，
不覆盖单口原理图；不能自动猜用户板型然后切换电源/复位/引脚。
本次保留两种profile标识，但未实现根据板名猜profile，也未授权未确认的供电provider。

新上游Linux驱动严格从 `0x0016` 读取16位ID并核对 `0x0214`；旧全志驱动从0000/0001
读取后无条件成功，不可作真实检测。I2C 的0x20是8位地址表示，本端7位地址为0x10。
传感器名、设备节点存在或静态图片YOLO成功均不等于已接通摄像头。

## 实际新增的代码（未启用硬件）

`a733_imx214_probe.c/.h` 在厂商芯片层，独立编写，只读身份/流状态/线格式/lane模式/
输出尺寸。仅在实际板级provider持有电源、MCLK和总线所有权锁，已核对板型/供电且
实际24MHz MCLK成立后调用。它没有默认provider、没有自动上电、没有注册 `/dev/video0`。
这不是现成可运行的完整sensor lower-half，更没有CSI/VIN/ISP。

- 两种profile必须明确限定，未知profile/26MHz/未限定电源时零总线调用。
- 总预算500ms，每次传输最多100ms；实际总线provider也必须实现有限硬件等待，
  本核心不能强行中止一个不遵守超时契约的底层回调。
- 五次固定寄存器读取，不扫描、不复位、不重试、不写流控制或模式值。
- ID错误立即ENODEV；每次传输错误保留，正数非法返回EPROTO；不发布半份完成快照。
- ASan/UBSan覆盖板型/时钟/电源拒绝、错误ID、五个阶段全部错误、超时与单调时钟回绕。
- 真实SDK AArch64编译最大栈80B，见 `out/imx214-v176-arm/summary.json`。
  主机回归在 `out/v176-imx214-probe-host-final.log`。都不是硬件通过。

## 为什么还不能进入DMA

缺少这块板子实际启用的DT/overlay、MCLK与供电实证、IMX214严格ID读取、lane/settle参数。
Linux官方1080p链路还使用全志ISP，openvela没有该ISP控制/IQ/缓存及流停止实现；
CSI与ISP使用各自IOMMU master，不能拿NPU DMA映射当摄像头映射复用。
现阶段不得把任意0x058xxxxx寄存器猜值写入，更不得把静态测试帧装作实时帧。
`embedded-soc-porting` 的电源/时钟/身份先决门禁未通过，所以没有启动更后的DMA阶段。
下一步须先补真实板级限定，再实现sensor模式、MIPI/D-PHY、VIN单帧DMA、颜色处理、
标准V4L2接口，最后连接既有YOLO/LVGL结果通道。

上游/GPL Linux代码与大段模式表只读参考；本次没有把其表达性代码/寄存器表改名为
Apache驱动。若后续复用，须单独处理许可与构建边界，不修改原Linux工程。

## 一次收集 Linux 实证（稍后有板时使用）

复制 `tools/test-linux-imx214.sh` 到官方Radxa Linux（用独立/备份介质，不刷UFS测试卷）。
先按照官方文档在rsetup选择实际插口对应的13M214 overlay并重启；脚本不会代改overlay。

```sh
bash test-linux-imx214.sh
```

默认只读现有DT、媒体拓扑、格式、绑定驱动、已挂载debugfs中的时钟/稳压器信息；
不会安装工具、扫描I2C、devmem、modprobe、操作存储块设备或启动视频流。
部分命令缺失会明确记录返回码，不作为成功数据。
若已知官方1080p预览可用，额外显式采三帧：

```sh
sudo bash test-linux-imx214.sh --capture --video /dev/video0
```

只使用官方 `en-awisp=1 en-largemode=0`、NV12 1920×1080、30/1、3帧流程；
20秒限时、2秒终止宽限，不切另一节点重试。预期文件9331200B；精确长度/退出码通过
才发布不带partial的文件，失败保留partial供调查。**Linux拍摄通过不是openvela通过**。
报告为新建 `imx214-report-XXXXXXXX.tar.gz`，内含元数据/原始BE32 DT值/退出码/SHA256。
回传该包和主板摄像头插口/板号照片，即可消除单口/CAM2冲突；不要上传API key/密码。

## 主来源与版本界限

- [Radxa官方摄像头流程](https://docs.radxa.com/en/cubie/a7z/accessories/camera-13m-214)
  验证1080p与全分辨率，低分辨率不能视为已验证。
- [Radxa V1.11 原理图](https://dl.radxa.com/cubie/a7z/docs/hw/radxa_cubie_a7z_schematic_v1.11.pdf)，
  本地只读参考SHA256：`eae921f432cef107e9c3229c113b0267e62669c035c467ada982fbf968c14a4d`。
- [Radxa CAM2 overlay](https://github.com/radxa-pkg/radxa-overlays/blob/main/arch/arm64/boot/dts/allwinner/overlays/cubie-a7z-radxa-camera-13m-214-cam2.dtso)，
  本地参考SHA256：`6307193a08285f3d60e5bc1a39777df73c779bba997c1860a2769e88cbb6cce5`。
- [Linux上游IMX214驱动](https://github.com/torvalds/linux/blob/master/drivers/media/i2c/imx214.c)，
  本地参考SHA256：`eaf4c123b7fe41874dbc72eb3145925c25d1b4a3a9f34895dede2b42b1253399`。

上述参考下载到 `out/imx214-reference-v176/`，官方原理图第7/10页已渲染核对。
main/master会变化，后续必须与固定摘要对照，不把今天网页状态当作未来版本。
原v175的478源归档与974文件恢复点保持原样；本阶段新代码另存新checkpoint，不覆盖。
