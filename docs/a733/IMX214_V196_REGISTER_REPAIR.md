# IMX214 v196：接收端寄存器修正候选

## 2026-10-28 追加：一次真实包级实验通过

[原始板测日志与边界](../evidence/camera-v196-board-20261028/README.md)已保存。
ID=0214、模式14/14、已开流；RAW10 包头观察119次、FS/FE各14次且错误为零；
停流、接收端和控制端恢复均成功，owned归零。此次日志没有启动CRC/SHA，不能据此
独立复验精确Image身份。历史候选/待测文字保留，最新包级结果以本追加为准。
**没有 RAW 帧、没有 FPS 测量，也不是 YOLO/显示或长稳验收。**
固件和镜像保持上轮封存版本不变，另建包级通过标签与恢复包。

v195 原始板测失败已存档在 [板测记录](../evidence/camera-v195-board-20261028/README.md)。
ID 与模式继续通过，但接收端准备、恢复失败且未开流。v194 模式通过版本保留。

## 修正与边界

1. 按本地 Allwinner BSP `combo_csi_reg_i.h` 的独立定义，将 DESKEW1 从
   `PHY+0x14` 修正为 `PHY+0x18`。前者为 DESKEW0，不应写 lane-clock delay 高位。
2. TRESCAL 只配置 `0x1f7`，不再把 RESULT 位当作普通可恢复配置；校准 flag/result、
   offset flag、频率计数器不回写为 1，不参与“未拥有位发生变化”的拒绝判断。
   其他可写位与严格掩码/回读/所有权检查仍保留，不是忽略所有回读错误。
   厂商 `cmb_phy_set_trescal` 虽表面调用 RESULT 写入，但 `vin_reg_clr_set` 按掩码
   截取 set_bits；调用参数为位号 16 而非 RESULT_MASK，实际不写 RESULT 高位。
   v196 保留真正生效的 SET=0xA，不新增推测的 RESULT override。不能仅凭字段名
   宣称所有芯片模式下它都只读；本版只是不把动态输出当成可重放的普通配置。
3. 分离 `receiver-first` 与 `receiver-cleanup`，保留首次失败的 error/address/mask/
   expected/observed；清理失败仍保留所有权并要求重启，不重置共享 PLL、不重试。
4. 原主机 RAM 假寄存器遗漏了保留位不锁存和结果变化。补充独立语义模型，旧 v195
   恰好重现 93 次读、29 次写、最后 `05810004`；这支持诊断，但不证明原板未记录的
   首个失败值。修正版故障注入还检查首次错误不被清理覆盖。

保留 PMIC 只读 RAM 交接、一次实验、开流前再次检查 ID/接收端、停流与逆序恢复。
不访问 CSI parser、DMA、ISP、IOMMU，不创建 `/dev/video0`，不是 RAW 帧或 YOLO 验收。
供电保留和时钟名义频率仍为假设，不是电气实测。语音/UFS 及历史通过版本不改。

## 构建与验证（WSL，在仓库根目录）

```sh
python3 tools/check-a733-camera-v196.py --build ../../quickly-openvela/cmake_out/cubie-a7z_nsh_v192_camera --output out/camera-check-v196-r1 --evidence docs/evidence/camera-v196-host-r1
bash tools/build-a733-camera-v196.sh out/camera-build-v196-r1 out/ort-static-v185-r2
python3 tools/build-a733-camera-handoff-v192.py ../../quickly-openvela/cmake_out/cubie-a7z_nsh_v192_camera out/camera-handoff-v196-r1 --release 196 --mode-directory out/imx214-mode-v187-r1
python3 tools/package-a733-boot-v188.py out/camera-build-v196-r1 ../../quickly-openvela/cmake_out/cubie-a7z_nsh_v192_camera ../openvela-a733-cubie-a7z-sd-v196-camera-register-repair-candidate.img out/camera-package-v196-r1 --base-version 191 --purpose camera-register-fix --camera-handoff out/camera-handoff-v196-r1
```

每次输出目录必须全新，不覆盖旧镜像。先完成源码冻结，再启动构建；SDK 旧文件必须
字节级恢复。参考文件见 v195 资料溯源，厂商 GPL 头不链接进新驱动。
最终制品摘要、恢复点与完成证据将在本说明追加，不把主机测试写成实机通过。

## 板测（必须先重启）

烧写新的 v196 候选到 SD（不得写 UFS）；进入**正确的 SD U-Boot**，确认 GPT 后：

```text
sunxi_card0_probe
mmc dev 0
mmc rescan
mmc part
ext4load mmc 0:3 0x4fc00000 /boot/openvela/a733/camera-v196.scr && source 0x4fc00000
```

NSH 中仅运行一次，不先跑 id/mode 实验，也不先调用其他摄像头控制命令：

```text
camera control prepare
camera control link-experiment
camera control status
```

回传全部输出，重点包括 receiver-first/cleanup、source on/off、receiver owned/error/
cleanup、RAW header/FS/FE/errors。任何 cleanup 非零或 owned=1：停止并重启，不能重试。
新鲜 RAW 包头且 FS/FE、CRC/ECC 状态合格、开/停流及恢复成功，只证明包接收阶段；
后续仍需真实帧 DMA、颜色转换、NPU YOLO 和 SPI 显示。

## 首轮主机结果

`out/camera-check-v196-r1` 通过，证据保留在 `docs/evidence/camera-v196-host-r1`。
接收端 2249 个合成用例包含全部 2237 个 callback-after-effect 故障；首次回读失败与
独立清理失败均有验证。动态校准结果/计数器下实际控制适配器组合通过。
旧 v195 模型重放精确得到 93R/29W/05810004；厂商独立宏核对通过。
原模式、ID、图像、交接脚本和包装拒绝回归通过；AArch64 接收核心最大函数栈 160B。
这是源码/主机/目标对象证据，完整镜像和实机结果另记。

## 最终制品（2026-10-28，未板测）

- 固件源码提交：`da2fafac5c99cace6f0a6c0094c6b6aca8dfe98a`。
  其后仅增加工具/厂商辅助函数语义测试与文档，最终记录逐个核对固件源码无漂移。
- 最终主机检查：`out/camera-check-v196-r2`，全部证据在
  `docs/evidence/camera-v196-host-r2`；r1 证据原样保留。
- 完整构建：`out/camera-build-v196-r1`，退出 0，287 项 SDK 原文件逐字节恢复。
- 包装：`out/camera-package-v196-r1`；最终关联证据：
  `docs/evidence/camera-v196-final/summary.json`。
- 镜像：`../openvela-a733-cubie-a7z-sd-v196-camera-register-repair-candidate.img`，
  2147483648 字节，SHA256：
  `b29c27c98b5494a079b84892e3d8bc34b1e7e99298b011e355fe004d526f8ba1`。
  同目录 `.img.sha256` 已生成，Windows/WSL 独立摘要一致。
- Image：7045664 字节，CRC32 `a535a176`，SHA256
  `2d61ddf2b9af22d98ec8c30cd832bffec6d9a9d0691d299739f0a82375d5ac7a`。
- ELF：9590696 字节，SHA256
  `d65f658be4498e78498e916f0075b8afd1c4febad51232596bba0a4813db18c6`；
  24 个构造函数，运行范围 92045312 字节，末端 `0x459c8000`，小于现有启动安全上界。
- camera-v196.scr：1358 字节，SHA256
  `ded6480edf659a90f600cbf3ca2e9433c739d07236d75a8dd555ce0fe5f08fe4`。
- 十项构建/交接/模式资产封存在 `out/v196-release-20261028`，含隐藏 `.config`。
  BSP `vin_io.h` SHA256：
  `d1feecffcabcc9efedadaade2b64e94c702d063c04d6421875f0eb27e0d88ece`，
  与另外两份校准原始资料一起纳入最终核验；没有链接 GPL 头或函数实现。

GPT、根分区文件系统与五项摄像头资产回读、内核摘要、根分区以外的两个跨度摘要
均通过；模型 FAT 未变化。没有执行物理 SD/UFS 写操作；烧录前仍需备份用户 SD 私有文件。
完整关联复核命令：

```sh
python3 tools/record-a733-camera-v192.py out/camera-check-v196-r2 out/camera-build-v196-r1 ../../quickly-openvela/cmake_out/cubie-a7z_nsh_v192_camera out/camera-package-v196-r1 docs/evidence/camera-v196-final --source-commit da2fafac5c99cace6f0a6c0094c6b6aca8dfe98a --release 196
```

该命令的输出目录已存在，复跑时必须换全新名称，不能覆盖原证据。
本地最终恢复标签 `a733-v196-camera-register-repair-candidate-20261028`；
恢复包 `out/checkpoints/a733-v196-camera-register-repair-candidate-20261028`，
包含源码/Git bundle 及新镜像、摘要、v194 模式通过回退镜像和十项封存资产引用。
验证及实际新目录 Git 恢复：

```text
python tools/checkpoint-a733-project.py verify out/checkpoints/a733-v196-camera-register-repair-candidate-20261028 --check-references
python tools/checkpoint-a733-project.py restore out/checkpoints/a733-v196-camera-register-repair-candidate-20261028 out/restore-check-v196-final-r1 --with-git
```

恢复检查使用 Windows 原生 Python。恢复目录已经用于验证，复跑同样需换新目录。
**以上均不是 v196 实机通过记录；真实包接收、停流/恢复、RAW 帧和最终 YOLO 仍待验证。**
