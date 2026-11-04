# IMX214 v197：真实单帧采集与 SPI 预览候选

## 目标和验收边界

继承 v196 已上板通过的 ID、1080p 模式、MIPI RAW10 包和停流恢复。
v197 新增 parser1 → CSI RAW bridge → BK0 DMA 的一次性采集、RAW/RGB/PPM
导出和 ST7735 全屏预览。**这些新增功能尚待实机验收，不是已通过出图版。**
不将包计数当成帧数，不使用测试图或旧图替代摄像头内容；目前没有 ISP 调色、
连续视频、帧率验收或实时 YOLO。CPU Bayer 预览仅用于确认取帧和几何形状。
I2S、语音、UFS、Agent 的已有配置保持不变，不删除 UFS 引导数据。

## 实机命令

断电连接 Camera 13M 214 到 V1.11 的 J5，禁止带电插拔排线。
烧录 v197 镜像，从 SD 的 U-Boot 启动（不要误进 UFS 残留引导）：

```text
sunxi_card0_probe
mmc dev 0
mmc rescan
mmc part
ext4load mmc 0:3 0x4fc00000 /boot/openvela/a733/camera-v197.scr && source 0x4fc00000
```

保留脚本校验与启动输出。进入 NSH 后直接执行，不要先运行旧版 ID/mode/link 试验，
也不要手工 prepare：capture 会自动 prepare 并执行本次冷启动唯一一次采集。

```text
camera capture /data/camera197
camera control status
ls -l /data/camera197
display image /data/camera197/preview.rgb
```

该目录必须不存在；所有文件成功后才写 COMPLETE.txt。应生成：

| 文件 | 内容/长度 |
| --- | --- |
| frame.raw | 1920×1080，RAW10 放入 little-endian U16，4,147,200 字节 |
| preview.rgb | 128×160 RGB24，61,440 字节 |
| preview.ppm | P6，128×160，可直接在电脑查看 |
| COMPLETE.txt | 来源、格式、尺寸和未完成 ISP/硬件验收的声明 |

预览前停止其他 display pet/demo/fps 任务。背景静止/运动物体必须和镜头实际朝向一致，
改变场景后需要重启再捕获，排除旧帧。保存完整 status 和导出文件用于定位。

默认 Bayer=bggr，依据 BSP 的 SBGGR10 格式声明。需要比较颜色时，可以只重新导出
同一张已捕获帧到另一个全新目录，不重新操作硬件：

```text
camera export /data/camera197-rggb rggb
display image /data/camera197-rggb/preview.rgb
```

也接受 grbg/gbrg。此调整不能修复错误的打包、位对齐或路由。
若无文件，先提供 camera control status；owned=1/cleanup 非零时停止并重启，
不要重复初始化、手工写寄存器或尝试解除 DMA 资源保留。

## 实现与安全边界

- 新的 4 MiB 常驻、页对齐帧池，与 NPU 预留区完全独立；最终 ELF 必须证明其
  位于 0x40200000..0x48000000 的低地址恒等映射区且不越过运行时镜像末端。
- 接收 1920×1080，stride=3840、预留 1088 行、前后 64 字节保护区。
  DMA 地址使用硬件 word 编码，不把 CPU 地址直接当成任意 IOVA。
- 只读核验系统 IOMMU v0x16、CSI master1 的 bypass；共享映射已启用且该 master
  未 bypass 则拒绝。只管理 IOMMU 访问时钟，不重写公共页表、全局 enable 或 bypass。
- 依据 sun60iw2 BSP 的 RAW 旁路事实配置 bridge，不运行 ISP/scaler/tuning。
  不移用旧 sun50iw9 专属 dma_input_select，也不触碰 NPU 私有 MMU。
- 配置 parser PCLK，enable/disable/enable 清理；DMA FIELD_EITHER、RAW10、单帧，
  不安装 IRQ。所有轮询有时间和次数上限，失效后不无限重试。
- 停传感器，确认 DMA capture 关闭、FIFO drain，恢复寄存器和时钟，再验证保护区、
  像素高位和延迟二次读取的稳定性。若停机不能确认，常驻资源保留到重启，禁止发布帧。
- 发布需要 DMA frame-done、无接收错误、内容检查及控制恢复同时成功。
  全零/全同帧仍需实际场景验收；传输成功不等于几何/色彩/光学验收。

## 本地验证与复现

v197 主机 r3：272 个单帧事务场景，其中 265 个回调执行后报错场景；
组合控制路径、保护区/高位拒绝、RAW 导出、四 Bayer、PPM、屏幕 RGB565 转换均通过。
旧 v186..v196 回归、AArch64 对象编译和栈检查通过。模拟数据只能证明软件分支，
不能证明芯片路由或真实 DMA 格式。日志见 docs/evidence/camera-v197-host-r3。
最终配置 r4/r5 回归同样通过；r5 包含只读原生哈希回退工具的来源绑定。
最终构建、镜像和 ELF 地址验证记录已存 docs/evidence/camera-v197-final。

## 发布身份与恢复

- 固件源码提交：947cb9c133f5b9fd45bd23fbae9cd64efaa6e503。
- 镜像：openvela-a733-cubie-a7z-sd-v197-camera-frame-candidate.img，2,147,483,648 字节。
- 镜像 SHA256：580e5dbfebb986ffdad885ea22a4a4ef56c6d882b0cdb1fe4569038e87b18acc。
- Image：7,057,952 字节，启动脚本 CRC32=2eb71c48；
  SHA256=814fad17690b9eabe7d654a9e0e2fd78ad703d4d05ed162314016f891aec20d9。
- 运行时末端 0x45dcc000；独立 4 MiB 帧池 0x458fa000..0x45cfa000，
  位于恒等映射 BSS，不与 NPU arena 共用。最终 ELF 自动断言通过。
- SDK 暂存路径 289 项全部逐字节恢复。编译警告中的 Rust LLVM 未配置仅为未启用功能，
  不代表目标固件编译失败。
- 主机打包 r1/r2 已生成相同镜像，但 WSL 9p 区间读取遇 ENOMEM，未标为成功。
  r3 对既有镜像只读复验通过，保留原失败目录；未覆盖镜像、未跳过校验。
  Windows Get-FileHash 与最终摘要一致。原生整文件/非零区间哈希和注入 ENOMEM 的
  回退调用已与独立 Python SHA256 比较通过；未改摄像头固件。
- 固件/脚本十项封存：out/v197-release-20261028；本地候选标签与恢复快照
  a733-v197-camera-frame-candidate-20261028。先保留此版候选，用户板测后再建立通过标签。
- v196 包级通过恢复点 a733-v196-mipi-packets-board-pass-20261028 和原镜像不变。

```sh
bash tools/build-a733-camera-v197.sh out/camera-build-v197-r1 out/ort-static-v185-r2
python3 tools/check-a733-camera-v197.py \
  --build ../../quickly-openvela/cmake_out/cubie-a7z_nsh_v192_camera \
  --output out/camera-check-v197-r3 --evidence docs/evidence/camera-v197-host-r3
```

检查目录必须为全新目录，已存在时使用 r4 等后缀，不覆盖证据。
冻结后的源代码提交、原始厂商参考文件哈希、目标对象、Image CRC、整盘 SHA256
和独立常驻帧池检查由发布记录绑定。原始 v196 通过镜像和恢复点保留。
