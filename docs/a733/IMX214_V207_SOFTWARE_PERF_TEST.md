# v207：CRC、输出扫描、所有权租约与像素/显示等价优化

2026-10-29。目标是减少 v206 中软件开销，并保留所有六路输出与冷/热一致性验证。
**这是待上板的性能候选，主机加速比不能当成 A733 FPS；不承诺 30 FPS 已达标。**

## 已通过基线及物理边界

v206 用户两项短测通过：[日志](../evidence/camera-v206-board-20261029/README.md)。
预览稳态 8.667 FPS，每帧 YOLO 稳态 2.483 FPS；均 `error=0 cleanup=0`，不是长稳验收。
原完整镜像、封存、标签 `a733-v206-camera-stream-sealed-20261029`、短测标签
`a733-v206-camera-short-board-pass-20261029` 和完整恢复包都保留。

每次推理原耗时约：准备 97.15ms，硬件含初始化/等待 17.10ms，输出/权重扫描 152.40ms，解码 16.73ms。
额外预处理 67.58ms，显示 44.68ms，均需优化，不能把 NPU 17ms 换算成整机 58FPS。
厂商当前 1080p 模式参考为 HTS=5008、VTS=2128、PCLK=320MHz，名义约 30FPS；不是实测帧率。
超过 30FPS 还需要单独验证传感器模式，当前不改曝光、链路或模式文件。
128x160 RGB565 在 SPI12MHz 上仅线速下限就是约27.3ms；轮询和命令开销另计。

## 实现及完整性边界

1. 原始 CRC 等价加速：反射多项式 `0xedb88320`、原 seed、无首尾异或，portable slicing-by-eight。
   8字节 load 使用 builtin memcpy，无对齐/别名 UB，不依赖 CRC/NEON 等可选 CPU 指令，不设置系统 ISA。
   应用/内核两个头文件逐字节相同，并以独立 zlib 约定、全部256字节模式、非对齐、分块和六种输出尺寸验证。
2. 输出 CRC 和 `!=0xa5` 变化计数在同一遍扫描完成。仍处理每一字节、全部六路输出、尾部守卫；不是跳采样或关闭校验。
   变化计数使用无跨字节借位误判的精确零字节掩码，保留旧计数语义。
3. 权重逻辑租约仅限相同不可变已核验包、同一 fd 会话、同一 prepared 池所有者和先前检查不变的 LOAD-only 区。
   所有合法变更 ioctl（现在包括 SYNC）、写入、任何 fd close、失败/释放和其他会话运行均使租约失效。
   GET_INFO/PROFILE 只读操作不触碰池。异步 CANCEL 不改池，执行失败仍使会话失效。
   **省掉的是两轮之间重复的一次 pre-run 权重扫描，不是取消权重检查。**
   每次硬件执行后仍 invalidate+检查全部候选 LOAD。曾被保留的区若变化，则该次运行失败，拒绝发布输出；
   不像旧分类逻辑仅在下一帧重新加载。首次发现的合法可变 LOAD 只标为不可保留，后续仍重新装载。
   私有池不 mmap、不向应用暴露可写指针；运行全过程仍持 tensor_gate，命令/输入/可变区继续重写和同步，MMU仍每次恢复。
   手动 `/dev/a733-npu` 诊断仍不得并发运行；这不是抵御任意内核写入或其他不受管理 DMA 的安全沙箱。
4. RAW10-LE16 内部点直接按行读取，提前计算 Bayer 颜色，避免每个 tap 调通用解包/颜色函数。
   插值、截断平均、掩码、Gamma/增益、旋转、padding、元数据与旧参考完全相同；边缘与 packed RAW10 保留原逻辑。
5. 图像对象每帧标脏，不再无条件重刷静态整屏；框、旧框隐藏、标签与状态仍由 LVGL setters 标脏，真实 CRC绑定仍检查。
   先完整绘制初始屏幕；显示频率、12MHz SPI、像素格式、控制器时序均不变。

NPU 底层执行、时钟、MMU映射实现、内存分区规格和公开 ABI不改；链接地址随代码增长改变，启动脚本从最终ELF生成地址，实际布局另验。
原硬件超时、缓存同步、守卫、错误/恢复检查不放宽。
语音应用与默认 tensor CRC代码不改；本实验配置下 prepared runtime 的 raw CRC 也使用等价快实现。
保留普通、legacy、同输入 verify、单帧及已修复的连续采集；不新增自动 retry/reset。

## 构建与回归

```sh
python3 tools/test-a733-perf-v207.py --output out/perf-check-v207-new
bash tools/build-a733-camera-v207.sh out/camera-build-v207-new out/ort-static-v185-r2
```

默认新缓存 `cubie-a7z_nsh_v207_camera_perf`。可显式复用配置匹配的 v205 生成缓存进行增量完整链接：

```sh
A733_V207_BUILD_DIR="$PWD/../../quickly-openvela/cmake_out/cubie-a7z_nsh_v205_camera_perf" \
  bash tools/build-a733-camera-v207.sh out/camera-build-v207-new out/ort-static-v185-r2 --incremental
```

这只更新生成缓存，不覆盖 v204/v205/v206 原镜像或封存。新增两个临时头文件，SDK 必须验证295项路径逐字节恢复。
回归17组，含 CRC/融合计数、实际所有权包装器、像素逐字节、六输出/冷热协调器、脏区/清框、CSI与旧故障回放。
主机 mock和静态编译不能替代真实 NPU 等价验收。

## 板测顺序

先备份 SD `/data` 自增数据，再烧写独立新镜像，UFS不改。自动启动包含相同SWOUT1检查。
若进入旧UFS U-Boot，先核对 SD 正常 GPT，再执行：

```text
sunxi_card0_probe
mmc dev 0
mmc rescan
mmc part
ext4load mmc 0:3 0x4fc00000 /boot/openvela/a733/camera-v207.scr && source 0x4fc00000
```

进入 NSH，先做正确性，不直接长测：

```text
display raw8
camera live-verify 5 1
```

任务结束后 `camera live-status`：应有两次 `equal-legacy=1`、全部六路 CRC/检测一致、`verified=1`、错误/清理为0。
该命令多做两次同输入 NPU执行，不作为FPS对照。
通过后分别运行，等每项 `active=0 error=0 cleanup=0` 再启动下一项：

```text
camera live-stream 10 0
camera live-status
cat /dev/a733-camera-control
camera live-stream 30 1
camera live-status
cat /dev/a733-camera-control
```

每次 `live-stream` 是后台任务，必须等10/30秒结束后再发下一项。记录完整串口日志、移动目标/离开目标时的清框。
统计显示 frames和真实 inference，`every=1` 才是每帧检测；不能用跳帧、重复框或预览FPS冒充推理FPS。
同场景/同推理频率对照v206；分别报告setup/稳态、CPU像素/CRC、NPU准备/硬件/输出/解码、实际SPI耗时。
分段行仍采用 `live205` 兼容协议，状态头为 `camera-live-v207`。读回 `stream-v206` 是未改动的已通过采集协议，不是旧镜像证据。
失败立即保存 `live-status`、control、dmesg；不继续重试，不强杀DMA/NPU。停止用 `camera live-stop`，等清理完成。
任何失败采集仍需重启；不因本次性能优化开放故障后的强制继续。

## 交付状态

完整构建、295项SDK恢复、17组回归、原生ARM/完整链接/ELF、镜像回读和Windows独立完整SHA已通过。
**最终上板前不宣称达到30FPS、不宣称长期稳定或更换模型后的泛化性能。**

| 项目 | 实际值 |
| --- | --- |
| 新镜像（仓库上一级） | `openvela-a733-cubie-a7z-sd-v207-camera-software-perf-candidate.img` |
| 大小 | 2147483648 字节 |
| SHA256 | `b174d29ca6525397b7b7890afe9462e9fc30a1e3fcf73ce431a2fd4efdc65492` |
| 哈希文件 | 同名 `.img.sha256` |
| 内核大小 / U-Boot CRC32 | 7131752 / `e30b716c` |
| 内核 SHA256 | `98093b92c37742b07399f6e965411b297026d9c2dacabe4608169605aea4765b` |
| 固件源码 | `ef35488375f5415b6cae49c4fa3b05dd5511728d` |
| 源码恢复标签 | `a733-v207-camera-perf-source-20261029` |
| 最终交付标签 | `a733-v207-camera-perf-sealed-20261029` |
| 二进制/配置/模式/脚本封存 | `out/v207-release-20261029`，10 项 |
| 完整源码/Git恢复包 | `out/checkpoints/a733-v207-camera-perf-sealed-20261029` |
| 构建/验收/包装 | `out/camera-build-v207-r1` / `out/camera-check-v207-r1` / `out/camera-package-v207-r1` |

[最终封存证据](../evidence/camera-v207-final/README.md)绑定日志和源文件完整哈希。
最终 ELF 24 个构造项，永久帧池 `0x4590c000` / 4MiB，运行区结束 `0x45ddf000`，地址/对齐/各保留区无重叠。
生产源文件相对v206仅8项（含两个等价CRC头），CSI/传感器/模式、NPU硬件执行和内存实现未改。
主机8MB CRC对照约4倍，仅代表x86主机对照；不是A733吞吐或30FPS验收。

重新验收/包装使用全新输出路径：

```sh
python3 tools/check-a733-perf-v207.py --build ../../quickly-openvela/cmake_out/cubie-a7z_nsh_v205_camera_perf --build-report out/camera-build-v207-r1 --output out/camera-check-v207-new --firmware-commit ef35488375f5415b6cae49c4fa3b05dd5511728d
python3 tools/package-a733-camera-v207.py out/v207-release-20261029 ../openvela-a733-v207-rebuilt.img out/camera-package-v207-new --firmware-commit ef35488375f5415b6cae49c4fa3b05dd5511728d
```

包装基于受保护v206整镜像，仅更新普通文件中的内核/启动资产；FAT/模型/GPT/其他分区哈希不改。
这不包含后来仅写在实卡中的用户文件；烧写前备份实卡`/data`。UFS未写。
回退使用原v206完整镜像，不要用旧启动脚本配新内核。
恢复包以外部哈希引用大型镜像和封存资产；保留这些文件才能核对完整恢复链。
