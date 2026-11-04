# v202：RAW 预处理逐字节核验与离线图像报告

2026-10-29。**主机工具版本，不是新镜像，不是摄像头或 YOLO 硬件验收。**

[最终证据/恢复说明](../evidence/camera-report-v202-final/README.md)。
源码恢复标签：`a733-v202-raw-report-final-20261029`。
沿用 v200 镜像和 v201 联调，继续保留自动 SWOUT1、真实 RAW 与屏幕联合板测。
按芯片/NPU分阶段验证边界，不绕过未通过的采集门禁、不启用连续取流、不修改驱动/内存布局。

## 本轮实际完成

v201 检查文件、输入 CRC、消息序号和整批框，但 CRC 自洽仍不能证明 RGB 来自该 RAW。
v202 新增独立整数参考算法：从1920×1080 RAW10-LE16重算640×640模型输入和128×160预览，
与文件逐字节比较。**“旧RGB＋重新计算的CRC”不能通过此项。**

- 严格右对齐10位、小端16位；高位异常拒绝，不悄悄掩掉错误位。
- 按 COMPLETE 中的 Bayer 顺序，复现3×3邻域按颜色取平均、先整数平均再10→8位转换。
- 复现中心采样、640×360有效图像、上下各140行114填充；模型输入不增强、不做伪 ISP。
- 单独复现128×72预览、上下44行黑边和仅显示用全局亮度拉伸；不把显示拉伸污染模型输入。
- 输出RAW直方图、四个Bayer位置均值、1080行亮度、零值/饱和比例和均匀帧告警。
- 可绑定v201结果：重算日志SHA、重解析COMPLETE/CRC/序号/整批框，不仅相信summary中的成功字符串。
- 框从模型空间裁掉填充后映射到1920×1080原图坐标；纯填充区域的框不伪造原图坐标。
- 报告包含模型原输入、模型叠框、预览映射框和128×160/RGB565布局示意。

四种 Bayer 顺序均与**实际生产 C 导出器**的CPU夹具逐字节对照，C侧用ASan/UBSan编译。
这证明参考算法与当前C约定一致，**不证明实际摄像头 Bayer 排列正确、颜色正确或真实 RAW 已采到。**
源约定固定SHA：

```text
a733_camera_image.c cd80e7f75b9ef7f3ae3e2a9d59d3a9fae705c5c6efd318eacfdf854eac0da8a6
a733camera_main.c   f14496cc3babfe3c1ccead50dac9d125c43ff690e2473c24c0f9afcd942b4b77
```

## 之后有实拍文件时的命令

先按 [v200启动与RAW测试](SWOUT1_V200_RAW_TEST.md) 和 [v201一次联调](IMX214_V201_HOST_COORDINATOR.md)
获取单帧目录；原始数据应完整包含 `frame.raw`、`preview.rgb`、`preview.ppm`、`model-rgb.bin`、`COMPLETE.txt`。
把整个目录下载到电脑，不只下载图片。v202不连接SSH、不启动推理、不读写SD/UFS设备。

仓库根目录下，Windows/Linux/WSL均可，Python3.10以上标准库，无额外依赖：

```text
python tools/a733-camera-report-v202.py --bundle <下载的单帧目录> --output <全新报告目录>
```

如果另有 v201 联调产生的 `summary.json`、`producer.log`、`viewer.log`：

```text
python tools/a733-camera-report-v202.py --bundle <下载的单帧目录> --inference-dir <v201结果目录> --output <全新报告目录>
```

仅当日志完整、日志SHA和输入CRC一致、所有阶段通过时才展示检测框。
没有日志就只输出图像诊断，**不编造框、不运行CPU替代YOLO、不声称NPU执行过。**
零目标是正常结果；原始完整检测最多32框，LCD示意只取前七框。

打开生成的 `report.html` 即可离线查看；图片嵌入页面，没有JS、远程字体、图片或脚本。
同目录 `REPORT.json` 保存输入/输出SHA、所有统计与坐标。只有最后发布的 `REPORT.json` 才表示
本次文件检查完整结束；I/O失败留下不完整目录用于排查，不覆盖后再跑。
这只是程序内最后发布，不是经过掉电验证的事务。输入目录和日志保持只读，输出禁止与其重叠。
报告含摄像头画面，可能涉及隐私，不要未经检查直接公开。

## 运行主机回归

```text
python tools/test-a733-camera-report-v202.py
python tools/check-a733-camera-report-v202.py --output out/camera-report-v202-new
```

第二条需要本机已有v200封存内核/恢复清单，先复验v201基线，再执行新工具测试。
Windows跳过gcc专用组；WSL运行全部，包括实际C四种Bayer输出对照。可加 `--verify-image` 独立重算v200镜像。
结果目录中的 `synthetic-ui-demo` 是明确标记的**主机合成外观示例**，既没有RAW采集也没有NPU结果，
不生成COMPLETE/REPORT.json冒充实际验收。

## 状态和下一步

工具成功状态：`OFFLINE_PREPROCESSING_VERIFIED_NOT_HARDWARE`。
告警仅提示排查，不能由“偏暗”直接断言供电/坏摄像头，也不能由“未告警”断言画质通过。
LCD图是坐标/采样/RGB565示意，不实现LVGL完整栅格化、抗锯齿和面板扫描，不是屏幕截图。

下一次实机仍按顺序验：v200冷启动供电 → RAW发布/清理 → 实拍内容 → v201同输入联调 → 本工具报告。
通过后再决定连续捕获、曝光/白平衡/ISP和实时FPS优化，不能从本轮主机夹具推断硬件成功。
旧镜像、所有开发文档和原始日志保留；本轮新增工具/测试/文档单独保存Git恢复点。
