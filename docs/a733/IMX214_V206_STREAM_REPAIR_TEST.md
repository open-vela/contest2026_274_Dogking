# v206：修复 v205 连续采集重新武装与错误归类

日期：2026-10-29。**候选修复，必须板测；主机模拟不能证明物理采集成功或提升 FPS。**
v204 用户通过版与 v205 全部源码、镜像、封存、故障日志均保留。本轮不修改 NPU/ASR/TTS、模型、内存布局、PMIC、UFS、屏幕接线。

## v205 现场与边界

- `live-verify` 同输入普通/常驻冷/常驻热六路输出与检测一致；7 帧完成，错误/清理均为 0。
  含初始化与两次额外核验的整体 1.071 FPS，日志稳态 1.536 FPS，不能混为纯性能比较。
- `live-stream` 仅完成第一帧，下一帧 CSI DMA `0x05830254=0x00220083`，返回 `-74/EBADMSG`。
  厂商 `dma140_reg_i.h` bit17 是地址未就绪/下溢，bit21 是丢行；不是内存容量不足。
  已记录的 MIPI 错误为 0，但出错第二帧没有完成新的 MIPI 快照，不能据此排除所有物理链路问题。
- 控制驱动实际 `cleanup=0 owned=0`，而应用 `cleanup=-74`。旧 STOP 把采集错误当作停止失败返回，导致应用错误隔离。
- 旧 rearm 只读取旧地址，没有重新写入下一帧地址。厂商 Linux 队列会为下一缓冲提交地址。
  这与现场相符，但“芯片地址就绪锁存行为”仍需 v206 板测验证，不能仅凭软件模拟认定根因已经硬件证实。

## 修改

1. 在已暂停、排空且地址/映射归属通过后，重新写入三个已拥有的地址端口并逐一读回；全部成功才启用 DMA/parser/单帧捕获。
   不更改 FIFO 模式，不开放循环 DMA，不复位、不自动重试、不屏蔽 `0x002f8044` 错误位。
2. 首个 DMA/parser 数据错误记录准确地址、mask、完整 observed 原值。晚到数据错误与排空/恢复失败分开记录。
3. STREAM STOP 返回独立清理结果；TAKE/status 保留原始采集错误。数据错误且已恢复时 `error=-74 cleanup=0`。
   **失败采集仍禁止自动重试，保持内核“一次失败需重启”的保守门禁**，不是清除错误就允许继续采集。
4. 新增检查：若 DMA/接收端仍持有资源，外层仅断言传感器控制脚，保留时钟/缓冲并报告清理失败；不能把不确定排空当安全恢复。
5. 状态头更新为 `camera-live-v206`、控制流报告 `stream-v206`；分段计时仍沿用 `live205` 协议，离线审计同时接受 v205/v206。

## 回归与可复现构建

```sh
python3 tools/test-a733-perf-v206.py --output out/perf-check-v206-new
bash tools/build-a733-camera-v206.sh out/camera-build-v206-new out/ort-static-v185-r2
```

新构建默认新缓存 `cubie-a7z_nsh_v206_camera_perf`。本地快速构建可显式复用原配置匹配的 v205 **生成缓存**：

```sh
A733_V206_BUILD_DIR="$PWD/../../quickly-openvela/cmake_out/cubie-a7z_nsh_v205_camera_perf" \
  bash tools/build-a733-camera-v206.sh out/camera-build-v206-new out/ort-static-v185-r2 --incremental
```

增量会更新生成缓存，**不覆盖** v205 封存、内核、镜像、Git 标签或完整恢复包。SDK 临时覆盖仍逐字节恢复。
新增回归以归档 v205 CSI 源码重放“地址读回保留但下一帧就绪已消费”的保守模型，旧版第二帧得到 `0x00220083`，
修复版 64 帧及所有 26 个 rearm 回调后生效错误通过；该模型明确不是完整芯片模拟器。
还测试实际生产 broker/app 的第二帧 EBADMSG、真实清理失败隔离、永久 DMA 缓冲/时钟保留、读者租约和成功重启。

## 板测：先短测，不直接跑长测

通过 v206 镜像的自动脚本启动；如进入旧 UFS U-Boot，先检查 SD GPT，再执行：

```text
sunxi_card0_probe
mmc dev 0
mmc rescan
mmc part
ext4load mmc 0:3 0x4fc00000 /boot/openvela/a733/camera-v206.scr && source 0x4fc00000
```

进入 NSH：

```text
display raw8
camera live-verify 5 1
```

等任务结束，`camera live-status` 必须 `active=0 error=0 cleanup=0` 且冷/热均 `equal-legacy=1`；再依次测试：

```text
camera live-stream 10 0
camera live-status
cat /dev/a733-camera-control
```

`live-stream` 后台执行，状态 active=1 时只是尚未结束，不要重复启动。10 秒后仍应看见多张变化的预览，
最终 `active=0 stage=complete error=0 cleanup=0` 且 `frames>1`。满足后再运行：

```text
camera live-stream 30 1
camera live-status
cat /dev/a733-camera-control
```

移动目标并让目标离开，核验同帧框与清框；需要提前停止则 `camera live-stop`，等 `active=0 cleanup=0`。
若任一步失败，立即保存 `camera live-status`、`cat /dev/a733-camera-control`、`dmesg`，不继续重试、不 kill/reset。
失败后，即使 `cleanup=0`，本轮也仍需重启再采集；真正 `cleanup!=0/owned=1` 则保持隔离。

这轮优先修复正确性，不宣称已提升 NPU 吞吐。NPU 硬件约 17ms 的旧板测与约 153ms 输出/权重复核不是同一个阶段。
下一轮性能优化必须保留所有输出/权重完整性和同输入冷/热一致性证据。

## 交付状态

源码回归、完整构建、镜像回读和 Windows 独立完整 SHA 已通过。
[封存证据](../evidence/camera-v206-final/README.md)；**尚未板测，不宣称连续采集或新 FPS 已通过。**

| 项目 | 实际值 |
| --- | --- |
| 新镜像（仓库上一级） | `openvela-a733-cubie-a7z-sd-v206-camera-stream-repair-candidate.img` |
| 大小 | 2147483648 字节 |
| SHA256 | `1a11333831fec58a247e3d61a85eb7cbf1350347dae1903432536a0f6f0746eb` |
| 哈希文件 | 同名 `.img.sha256` |
| 内核大小 / U-Boot CRC32 | 7111272 / `6e71a03f` |
| 内核 SHA256 | `793803d6c3285b13167b83acf9bf546ae4d96055d7c30efa1c21e6d2079ce201` |
| 固件源码 | `8cb44ea2dab68709203e3ae3f04ec2058bf0c603` |
| 源码恢复标签 | `a733-v206-camera-stream-source-20261029` |
| 最终交付标签 | `a733-v206-camera-stream-sealed-20261029` |
| 二进制/配置/模式/脚本封存 | `out/v206-release-20261029`，10 项 |
| 完整源码/Git 恢复包 | `out/checkpoints/a733-v206-camera-stream-sealed-20261029` |
| 构建报告 | `out/camera-build-v206-r1`，状态 0，293 项 SDK 路径逐字节恢复 |
| 最终验收 | `out/camera-check-v206-r1`，15 组回归 + 原生 ARM/完整链接/ELF |
| 包装报告 | `out/camera-package-v206-r1`，普通镜像文件，不写板/UFS |

本轮复用了显式选择的 v205 生成缓存；v204/v205 原封存、整镜像、哈希、标签和恢复包全部保留。
相对 v205 固件，生产文件仅 4 项摄像头代码修改；NPU/语音所有源码保持不变。
24 个构造项、4 MiB 永久帧缓冲、运行区结束 `0x45dda000`、原公开 NPU ABI 均通过检查。

重新验收/包装使用全新输出路径：

```sh
python3 tools/check-a733-perf-v206.py --build ../../quickly-openvela/cmake_out/cubie-a7z_nsh_v205_camera_perf --build-report out/camera-build-v206-r1 --output out/camera-check-v206-new --firmware-commit 8cb44ea2dab68709203e3ae3f04ec2058bf0c603
python3 tools/package-a733-camera-v206.py out/v206-release-20261029 ../openvela-a733-v206-rebuilt.img out/camera-package-v206-new --firmware-commit 8cb44ea2dab68709203e3ae3f04ec2058bf0c603
```

烧写前备份 SD `/data` 中自行增加的文件；生成镜像保留的是基线镜像 FAT，不包含后来只写在实卡里的数据。
UFS 未修改。要完整回退，使用保留的 v204/v205 整镜像，不能用旧启动脚本启动新内核。
恢复包不把数 GiB 镜像塞入 Git，而是保存外部文件完整哈希引用；这些镜像和封存文件也必须保留。
