# v200：固化 SWOUT1 启动检查，继续 v199 RAW 实验

## 发布身份与恢复点

镜像：`../openvela-a733-cubie-a7z-sd-v200-swout1-raw-candidate.img`，2,147,483,648字节。
SHA256：`a729b7442ffc764063b191d8ac3ebb38c4a2812c18f21cc9dacf2cd02fbaaa33`；
同目录 `.img.sha256` 已用 Windows 独立文件读取复核，旧镜像不覆盖。
启动层源码提交：`d8eefd500ac325ccaebdb2bc99ca56578c418cf8`。
复用固件源码：`6d1ece1c1a385db8f1f6163b172fbbf51a99a002`。
kernel=7,062,048字节，SHA256=`19e2e0772816a44ee9ec671c3eac92f7978b2587a8a8c502c7abd60919cf96c0`，
CRC32=`7ae9ab95`；新script=2,215字节，SHA256=`41094803268aad1165ec0f97afa8d61d3afac74163b0843aa7ffc8963425b04c`。
原extlinux备份196字节，SHA256=`d9692319a64351e0e000259fba6261d70c2e7803bc3680c332bba29865f679b5`。
独立4MiB帧池仍是0x458fb000..0x45cfb000，运行映像末端0x45dcd000；不以新脚本绕过任何 DMA 门禁。
十项封存：`out/v200-release-20261028`；恢复标签/checkpoint：
`a733-v200-swout1-startup-candidate-20261028`。
[最终身份链](../evidence/swout1-v200-final/summary.json)、
[最终主机/ARM回归](../evidence/swout1-v200-host-r2/summary.json)。
内核是复用而非重编；封存v200目录中的同字节固件/模式也可作为构建工具的seal参数。

## 版本边界

这是启动层修复，**内核逐字节复用已封存 v199**，不重编或修改 SPI、LCD、摄像头、
DMA、NPU、内存预留或语音代码。NSH 的 camera-control-v199 标记因此是预期，不是刷错。
v128 手动开 SWOUT1 后的三色块/LVGL/七表情通过证据见
[板测记录](../evidence/swout1-v128-board-20261028/README.md)。
v200 自动脚本和 v199 真实 RAW 采集仍待本版上板，主机模拟不是硬件通过。

## 电源门禁

在 U-Boot 阶段加载精确 Image、核对尺寸/CRC/两个 ABI、加载固定模式文件和 DTB，
随后只接受以下已验证供电配置：PMIC ID03/version00，DCDC开启字节10=ff，
DCDC1电压配置12=17；控制11只允许17或1f。

- 11=17：只执行一次 `sunxi_axp pmu write 11 1f`，仅改变bit3。
- 11=1f：不写，仍完成读回检查。
- 其他配置、读写错误、电压变化或读回不符：本脚本不进入内核，不重试，不提交 handoff。
- 等待1秒，再读11必须为1f、12仍为17，才提交摄像头交接记录并 booti。
- 不写12或任何电压，不写 UFS/SD，不 saveenv，不在 NuttX 访问 PMIC。
- 写后发生错误时不自动关电；SWOUT1可能已开，需保留输出检查，不能假称全回滚。

复用旧记录的 crc_scratch 三字节保存前值/后值/电压，时机在 CRC 验证之后；
不改变64字节ABI，reserved[42]仍为全零，运行时仍只消费六项原供电快照。
日志应包含 `swout1-v200: VERIFIED`，后面的三字节应为 `17 1f 17` 或 `1f 1f 17`。
这是配置读回证据，仍不是供电电压/MCLK的物理测量。

## 自动启动与恢复

厂商 distro 扫描按 `/`、`/boot/` 查找，且 extlinux 优先于脚本。因此新候选镜像：

- 在根分区新增 `/boot/boot.scr`，与 `/boot/openvela/a733/camera-v200.scr` 完全相同。
- 将原 `/boot/extlinux/extlinux.conf` 完整备份到 `extlinux.conf.before-v200`，
  移除新镜像中的活动文件名，避免跳过电源检查；不删除原内容。
- v199 内核、模式数据/GPL来源、旧camera-v199脚本和所有非根分区均保持不变。
- 不修改板上 U-Boot 二进制、环境或 UFS 中残留引导。

普通 SD 自动启动应出现 `Found U-Boot script /boot/boot.scr`、`swout1-v200: VERIFIED`、
`camera-handoff-v200: committed`。外层扫描器可能在脚本失败后继续尝试其他设备，
所以不能声称已修复 UFS 抢先启动或所有固件的全局失败停机。确认没有这些标记就停止测试。
若当前固件/环境没有按预期扫描脚本，可手工启动，不需要 saveenv：

```text
sunxi_card0_probe
mmc dev 0
mmc rescan
mmc part
ext4load mmc 0:3 0x4fc00000 /boot/openvela/a733/camera-v200.scr && source 0x4fc00000
```

仅使用 v200 脚本。内核保留的 `camera boot-help` 指向旧 v199 手工入口，旧入口没有新电源门禁，
不能用它替代 v200 自动启动。保留它用于历史恢复，不把旧脚本悄悄改成另一份内容。
恢复旧普通启动入口可在离线镜像/SD根分区中恢复备份文件名；旧镜像和 Git 标签始终保留。
烧录前备份当前SD的模型、配置和数据；“模型FAT未改”仅是相对v199基底，不是保留当前SD内容。

## 上板 RAW 门禁

断电连接 J5 13M214 与 SPI 屏幕。第一次开机不手工写 PMIC，用上述标记确认脚本真的生效。

```text
display raw8
display test 10
camera control status
camera capture /data/camera200
camera control status
```

目录必须不存在；不要先执行 prepare/id/mode/link，capture 自己串接且每次新交接仅一次。
屏幕 raw8 若仍白就停止。capture 有任何错误也停止，保留首错/cleanup/owned/保护区输出；
owned或cleanup非零时重新启动，不强行写寄存器或重复实验。

只有 capture 成功、frame published=1、owned/cleanup=0 且完成标记存在时才执行：

```text
ls -l /data/camera200
cat /data/camera200/COMPLETE.txt
display image /data/camera200/preview.rgb
```

验收包括：真实RAW文件4,147,200字节；preview.rgb=61,440；model-rgb.bin=1,228,800；
供电/ID/模式/RX/帧完成、停机/迟到错误、格式/内容/保护区/稳定性检查通过，且实体图像可辨认。
不是只看 shell returned 或文件名；不把 v128 屏幕通过当成摄像头通过。
本轮先验证 RAW/预览，不在失败基础上继续 YOLO；尚非连续取流/ISP/实时推理完成。

## 构建与检查

```sh
python3 tools/build-a733-swout1-v200.py out/v199-release-20261028 out/swout1-assets-v200-r1
python3 tools/check-a733-swout1-v200.py \
  --build ../../quickly-openvela/cmake_out/cubie-a7z_nsh_v192_camera \
  --output out/swout1-check-v200-r1 --evidence docs/evidence/swout1-v200-host-r1
python3 tools/package-a733-swout1-v200.py out/v199-release-20261028 out/swout1-assets-v200-r1 \
  ../openvela-a733-cubie-a7z-sd-v200-swout1-raw-candidate.img out/swout1-package-v200-r1
```

需要本地完整厂商/SDK引用及已封存v199内核；缺少正确封存就停止，不从可变构建目录盲取。
全部输出用新目录，旧镜像不覆盖。551项命令模型场景穷举全部控制/电压字节和异常，
真实临时ext4安装/备份/回读/冲突拒绝测试，再执行原v199全套主机/ARM回归。
新脚本的 Hush 控制流还需真实 U-Boot 执行验证；不能以 Bash 模型替代。
