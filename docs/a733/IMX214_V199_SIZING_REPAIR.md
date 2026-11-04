# v199：修复 v197/v198 的尺寸寄存器语义及启动引导问题

## 发布身份

镜像：`../openvela-a733-cubie-a7z-sd-v199-camera-sizing-repair-candidate.img`，2,147,483,648 字节。
SHA256：`78d74c627ba002d5a94bcd8a877bef58b17ba848155858e3188f0d118bbad724`；
同目录 `.img.sha256` 是 Windows 独立整文件复核结果。
固件源码提交：`6d1ece1c1a385db8f1f6163b172fbbf51a99a002`。
Image=7,062,048 字节，SHA256=`19e2e0772816a44ee9ec671c3eac92f7978b2587a8a8c502c7abd60919cf96c0`，
U-Boot CRC32=`7ae9ab95`。script=1,358 字节，SHA256=`9517c20f23e874f2d7b018a6d92b4ea75e4ecaab6838a011e77088dce508de33`。
运行映像96,260,096字节，末端0x45dcd000；独立4MiB帧池0x458fb000..0x45cfb000，仍在低地址恒等映射内。
基底 v191 除内核分区外完全一致，模型 FAT 未变；本次并未操作物理 SD/UFS。
十项固件/脚本封存 `out/v199-release-20261028`；可恢复标签和 checkpoint：
`a733-v199-camera-sizing-repair-candidate-20261028`。
[最终身份链证据](../evidence/camera-v199-final/summary.json)、
[最终主机/ARM 回归](../evidence/camera-v199-host-r2/summary.json)。
完整链接、24 个构造函数、289 项 SDK 逐字节恢复、分区摘要和镜像回读均通过，**不是硬件成功**。

## 已知事实与修复

[两版原始板测与 SHA](../evidence/camera-v197-v198-board-20261028/README.md)保留。
v197 真正失败于开流前的 BUF_LEN 写/回读，恢复又对同一派生字段错误写回；
v198 日志则是普通启动未带交接记录，零 MMIO 拒绝，随后不存在的文件引发下游错误。
缺屏幕不导致这两种摄像头失败；v198 共用的旧尺寸逻辑同样需要修正。

最终方案跟随厂商 vin_subdev_logic_s_stream() 的 DMA140 路径：

- TOP 29/28/30 保持自动模式，不再将 BUF_LEN/FLIP_SIZE 作为普通可写保存寄存器。
- 从可写配置表、上电前 strict check 及恢复写入表移除这两个派生字段；write-mask=0。
- 几何、输出格式、地址、IRQ、接收端和内存保护仍严格验证；不跳过真实完成/停机检查。
- 先恢复几何/格式，最后恢复原 TOP 模式，然后只读观察派生值。
  原来若是软件或混合模式，其隐藏软件寄存器从未被改写；八种模式均测试可恢复。
- 不把自动长度寄存器读数直接解释为实际导出字节 stride；3840 是 RAW10-LE16 导出契约，
  必须同时通过真实 DMA、完整内容/保护区/稳定性检查才能发布。
- 增加 frame-sizing active/restored 原始诊断。未改变 NPU arena、帧池大小或 ISP 执行。

曾用显式软件尺寸模式做主机对照，模型通过；但它会引入未板测的硬件配置假设，
最终不作为发布方案。没有为了消除报错而强写步长、复位公共 IOMMU 或放宽帧发布条件。

启动问题保留安全门禁，不把历史 PMIC 常量冒充当前读数，也不让 NuttX 抢占 SCP 电源总线：

- 新增 `camera boot-help`，只打印本版启动命令，无设备访问。
- 缺 handoff 时仍返回 ENODATA、零 MMIO，但直接提示完整 U-Boot 步骤。
- capture/export 失败提示停止下游；背景模型文件缺失时明确说明该命令尚未初始化 LCD。
- 保留 v198 的同输入模型文件、CRC/序号/完整批次绑定与迟到 DMA 错误检查。

## 上板步骤

本版是修复候选，**尚未证明真实 RAW 出图或摄像头 YOLO 联调通过**。
v197/v198 旧镜像、Git 标签、源码和历史开发文档保留，修复合并到 v199 而不覆盖旧包。
开发过程不写物理 SD/UFS、不清 UFS 引导；用户烧录前需备份 SD 中模型和配置。

连接屏幕和 J5 摄像头前先断电。屏幕沿用 [既有接线](../ST7735_LVGL_PORT.md)：
SCK=23、MOSI=19、CS=24、DC=22/PL5、RST=7/PB0，VCC/BL=3.3V，共地。

烧录后进入 SD U-Boot，按完整顺序执行（包含 mmc part）：

```text
sunxi_card0_probe
mmc dev 0
mmc rescan
mmc part
ext4load mmc 0:3 0x4fc00000 /boot/openvela/a733/camera-v199.scr && source 0x4fc00000
```

进入 NSH，先单测显示；这些显示命令不消耗摄像头一次性试验机会。

```text
display raw8
display test 10
camera capture /data/camera199
camera control status
```

不要先手工跑 camera prepare/id/mode/link；capture 自动串接，每次脚本启动只尝试一次。
目录必须不存在。失败就停下，发回完整 status；owned/cleanup 非零时重启，禁止手动恢复寄存器。
正常应看到 frame published=1、owned/cleanup/error=0；frame-sizing 有 active/restored 观察值。
旧的0x07800780派生读数本身不再被当作可写寄存器回读失败，也不单凭该值证明图像正确。

仅当采集成功并且 COMPLETE.txt 存在时，继续：

```text
ls -l /data/camera199
cat /data/camera199/COMPLETE.txt
display image /data/camera199/preview.rgb
npu selftest
display vision-image /data/camera199/model-rgb.bin 120 &
sleep 2
npu yolo-ui /data/camera199/model-rgb.bin
```

须已有固定模型 /data/npu/yolov8n-pcq-v3.a7pm；等显示端提示等待推理后再提交。
若失败，不要继续输入后面命令。底图/检测框同输入验收及错 CRC 负测沿用 v198 文档。
仍不是连续取流、ISP 色彩校准或实时 YOLO 的完成声明。

## 回归与复现

主机模型明确模拟自动派生读取、自动模式写入无效，避免所有寄存器都像普通 RAM 的假设。
分别取两个已发布源码提交、核对来源 SHA，再复现用户报告的首错/恢复错三元组。
修复后 276 个事务场景（259 个回调后故障注入），包含八种原模式/隐藏软件值保持；
控制/TWI/模式/RX/RAW 组合测试及四 Bayer/CRC/原子完成标志/缺 handoff 提示通过。
这些是软件证据，不是硬件成功；最终配置检查和 ELF/镜像身份记录另存最终发布证据。

```sh
bash tools/build-a733-camera-v199.sh out/camera-build-v199-r1 out/ort-static-v185-r2
python3 tools/check-a733-camera-v199.py \
  --build ../../quickly-openvela/cmake_out/cubie-a7z_nsh_v192_camera \
  --output out/camera-check-v199-r2 --evidence docs/evidence/camera-v199-host-r2
```

每次使用全新输出目录；构建在 trap 中恢复 SDK。保留所有旧故障日志和恢复点。
