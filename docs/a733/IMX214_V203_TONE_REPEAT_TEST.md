# v203：偏黑/方向修正与受控重复 RAW 采集候选

2026-10-29 实机补充：用户确认两次采集/显示通过、颜色正常。
[日志、RAW/串口指纹及生产C重放证据](../evidence/camera-v203-board-20261029-r2/README.md)。
原镜像不变，作为双次实拍/显示通过版保留；恢复标签`a733-v203-camera-board-pass-20261029`。
下面是构建时的操作和限制，待测项仅按上述证据补充，不扩大为完整ISP/连续视频/长稳通过。

2026-10-29。独立新固件/镜像候选；**新路径仍须板测，不把主机C效果当作新固件已上板通过。**

## 依据与保留项

用户v200日志已证明该次SD自动执行boot.scr、SWOUT1从17置1f并读回、电压配置12保持17；
内核7062048字节CRC=`7ae9ab95`，IMX214 ID=`0214`、模式14/14、RAW4147200字节、
published=1、validation/cleanup=0、owned=0。两份实际RAW均与导出的线性RGB/PPM逐字节匹配。
原始数据中主体很暗，但有可恢复细节；并有青偏和180°倒置，不是SPI把正常图片画黑。
这些新增证据补充历史v200待测记录，但不构成摄像头颜色、连续视频、YOLO准确率验收。

原v128/v196/v199/v200镜像、v201/v202源码、失败日志和开发文档全部保留。用户人像及RAW
只在本地私有测试目录/out处理，不加入Git、不打入镜像或上传远端。

## 本轮修复

1. 保留原始 `frame.raw`、旧 `preview.rgb/ppm`、`model-rgb.bin`、`COMPLETE.txt` 的计算约定。
2. 新增 `view.rgb/ppm`：先在RAW10域扣除可调经验黑电平，再做可调Q8软件增益、手动RGB系数、
   Gamma2.2查表和180°RGB旋转。Bayer插值先完成再旋转，不能通过错误交换Bayer排列修正方向。
3. 默认实验预览参数：black64、软件增益512(2x)、RGB系数512/256/256、旋转180°。
   红通道2x是针对现有样例的**经验预览系数，不是已校准AWB**；可恢复为全部256。
4. 新增 `vision-rgb.bin`：相同增强/方向处理的640×640可选YOLO输入；114填充，
   有效640×360、上下各140行。`VIEW.txt`记录参数、RAW和增强输入CRC。
   旧模型输入不变，用户可对照；增强输入的检测准确率仍需实测。
5. 重复采集仅允许上次RAW已经通过、停流/接收端/DMA/控制恢复全部成功且没有读者持有帧时。
   复用同次启动已校验的电源快照和SHA校验模式，不重复访问SCP/PMIC，也不伪造新的电气测量。
6. 硬件/传输/恢复失败不清故障、不强行重置、不重新启用不确定的DMA；需要重启恢复。目录已存在、
   参数错误、读者占用属于操作拒绝，不是故障复位入口，需更换目录/修正参数/释放读者。每次启动最多32次
   成功采集，是手动重复单帧候选，不是连续视频流。帧设备打开期间禁止覆盖缓冲；重复CLI互斥。
7. 新增传感器帧长/曝光行数/模拟增益代码只读诊断；**未加入传感器AE/增益写入**。
   软件增益不等于改变曝光。当前模式名义曝光接近帧长，不能盲目延长积分时间。

## 上板先测这些

交付镜像：`../openvela-a733-cubie-a7z-sd-v203-camera-tone-repeat-candidate-r2.img`。
不要烧录不带r2的打包草稿：其固件相同，但PMIC清单描述未更正；r2才是本次封存交付。
继续保留旧v200镜像。烧录前备份SD上的配置/文件；UFS和个人模型不会由打包工具擦写。
镜像重新烧入会替换SD内容，不能拿“模型FAT与旧候选一致”当作替用户保留实卡新增数据的保证。

正常v203自动启动后，先用两个全新目录测试重复采集（**同名目录仍返回-17拒绝覆盖**）：

```text
camera tone
camera capture /data/camera203a
display image /data/camera203a/view.rgb
camera control status
camera capture /data/camera203b
display image /data/camera203b/view.rgb
camera control status
```

应看到 `camera-control-v203`，两次generation分别1/2，readers=0、ready=1，
frame published=1、validation/cleanup=0、owned=0；改变镜头方向后内容/指纹应不同。
不要在采集/文件写入过程中Ctrl+C，不要同时从echo控制节点操纵另一个实验。
失败后保留完整状态，不盲目重试；清理/所有权异常时重启。

若UFS残留引导抢先启动，继续按既有SD选择流程，不在这一版擦UFS U-Boot。
手动入口为当前镜像的 `/boot/openvela/a733/camera-v203.scr`，不能混用v200脚本和新Image。
运行 `camera boot-help` 可查看完整命令。

## 不重新拍摄也能调预览

参数依次为：经验黑电平、软件总增益、R/G/B增益、旋转角度。Q8中256=1x，
增益范围64..2048，黑电平0..255，角度仅0/180；配置RAM内保存，重启回默认。

```text
camera tone 64 512 512 256 256 180
camera export /data/camera203-adjust1
display image /data/camera203-adjust1/view.rgb
```

`export`只重处理当前RAM中最后一帧，不是新拍摄。恢复无红通道经验补偿：

```text
camera tone 64 512 256 256 256 180
camera export /data/camera203-adjust2
display image /data/camera203-adjust2/view.rgb
```

如果旧RAW已保存到SD/UFS，即使重启过也能只用CPU/文件重新处理：

```text
camera process /data/camera200/frame.raw /data/camera203-fromfile
display image /data/camera203-fromfile/view.rgb
```

输入必须是精确1920×1080右对齐RAW10-LE16普通文件；process不触碰摄像头，
COMPLETE标记为 `user-supplied-raw-file`，不会伪造为本次真实采集。
输出失败不发布新的COMPLETE，不覆盖旧目录。

## YOLO与屏幕同输入叠框（可选，两个SSH窗口）

窗口1（先启动并等待底图CRC就绪）：

```text
display vision-image /data/camera203a/vision-rgb.bin 60
```

窗口2：

```text
npu yolo-ui /data/camera203a/vision-rgb.bin
```

模型包仍需已有 `/data/npu/yolov8n-pcq-v3.a7pm`。本轮不改变NPU驱动/模型。
生产/消费两端都读取同一个增强且旋转过的文件，框与底图一致；增强输入CRC必须对照VIEW.txt。
离线映射回未旋转RAW时必须先逆转180°，不能把旧v202不带旋转的原图坐标直接套上。
旧v201/v202仍面向旧线性五文件，可用于保留的旧数据对照，不能把它们的待测/锁定假设当成新路径通过。

## 构建与回归（仓库根目录，WSL）

```text
python3 tools/test-a733-camera-v203.py
bash tools/build-a733-camera-v203.sh out/camera-build-v203-NEW out/ort-static-v185-r2
python3 tools/check-a733-camera-v203.py --build ../../quickly-openvela/cmake_out/cubie-a7z_nsh_v192_camera --build-report out/camera-build-v203-NEW --seal out/v203-release-NEW --source-commit 5235276368811ae694c5119491d113067de154d4 --output out/camera-check-v203-NEW
```

检查前应把本次build中的nuttx、nuttx.bin、nuttx.map、.config复制到新的封存目录，并复制既有
imx214-1080p.a7cm、imx214-reference.c、SOURCE-LICENSE.txt；不得覆盖旧封存。
后续有源码改动时需另建源码提交，不能套用本次源码SHA作为新构建依据。

完整构建复用可变v192编译缓存，但不覆盖旧封存；覆盖层由包装器临时安装并恢复，
必须检查 `wrapper-status.txt=0` 和 `sdk-restored.json` 的289项逐字节恢复。
打包使用新增 `package-a733-camera-v203.py`，只修改新2GiB普通镜像的rootfs副本，
保留旧Image及引导脚本备份，非root分区摘要必须与v200基础镜像一致。

主机验收包括ASan/UBSan、32个不同模拟帧、读者阻止重采、故障不重采、缓存模式SHA、
四种Bayer导出、参数边界、Gamma端点/单调、180°几何含奇数填充，以及新C代码处理两份
实际私有RAW后的原文件SHA不变。**这些不是新固件的实机循环/画质/热稳定性通过。**

## 本次封存与恢复入口

- 固件源码提交：`5235276368811ae694c5119491d113067de154d4`；打包器清单修正提交：`5c80ada24795a747b53578a5e48346bb033239e8`。
- 新Image大小：7066168字节，CRC32=`adbc228c`；SHA256=`5fb5bf9277fb26a816049ef5983603def5d140ee943a897d754ab560eaa18687`。
- 交付2GiB镜像SHA256：`7f30ba7597b9a87a5d4ac99018040ddcfb57e275ab64a6154a51c0c11e5ed481`。
- Windows原生与WSL独立摘要一致；临时rootfs与封装后ext4回读/e2fsck、镜像检查及所有非root字节区间与v200基础镜像一致。
- 完整构建`wrapper-status=0`，官方SDK289项逐字节恢复；帧池4MiB位于`0x458fc000`、runtime end=`0x45dce000`，24个构造器。
- ELF包含新tone函数、1024项Gamma表及读者/采集代数/重采状态符号，不是只改了脚本文字的旧内核。
- 构建封存：`out/v203-release-20261029`；证据：[camera-v203-final](../evidence/camera-v203-final/summary.json)。
- 恢复标签/检查点入口：`a733-v203-camera-tone-repeat-candidate-20261029`。

核验/恢复应在仓库根目录运行；恢复只写全新目录，不原地撤销工作树，不擦卡：

```text
python3 tools/checkpoint-a733-project.py verify out/checkpoints/a733-v203-camera-tone-repeat-candidate-20261029 --check-references
python3 tools/checkpoint-a733-project.py restore out/checkpoints/a733-v203-camera-tone-repeat-candidate-20261029 out/restore-v203-NEW --with-git
```

交付镜像使用新的`camera-v203.scr`与新Image配对，禁止混用v200脚本/旧地址。
先验证自动启动、`display raw8`、`npu selftest`，再做上文两个不同目录的采集。
如出现硬件/恢复错误，保留完整日志并重启回退，不自行清空隔离状态或循环重试DMA。
本轮没有实现完整ISP/AE/AWB、实时摄像头循环，也没有新增语音/NPU模型验收结论。
