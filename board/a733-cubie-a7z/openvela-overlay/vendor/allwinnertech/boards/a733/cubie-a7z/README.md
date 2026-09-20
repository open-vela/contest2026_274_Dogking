# openvela 在全志 A733 / 瑞莎 Cubie A7Z 上的适配

这是第一阶段、以 SD 卡启动为主的 ARM64 适配。它有意复用瑞莎官方已验证的
RadxaOS 镜像中的 Boot0、SCP、BL31 和 U-Boot 固件。openvela 当前不探测 UFS，
UFS 启动留给后续板级配置。

## 硬件约束

- DRAM：4 GiB LPDDR4，物理地址 `0x40000000`
- openvela Image 加载/入口：`0x40200000`
- 第一阶段 RAM 窗口：`0x40200000..0x47ffffff`
- BL31 保留区域：`0x48000000..0x48ffffff`
- 控制台：UART0，`0x02500000`，GIC IRQ 34，115200 8N1
- 中断控制器：GICv3，GICD `0x03400000`，GICR `0x03460000`
- 启动存储：SDMMC0，`0x04020000`（驱动在 NSH 基线后继续实现）

生成的 `nuttx.bin` 带 Linux ARM64 Image 头。这是因为本板 U-Boot 为 AArch32，
必须通过 `booti`/BL31 进入 AArch64；`go 0x40200000` 不是有效启动方法。

## 在 WSL 中构建

从 `quickly-openvela` 目录执行：

```sh
export PATH="$PWD/prebuilts/gcc/linux-x86_64/aarch64-none-elf/bin:\
$PWD/prebuilts/tools/linux/x86_64:\
$PWD/prebuilts/tools/cmake/bin:\
$PWD/prebuilts/tools/ninja:\
/usr/bin:/bin:$PATH"

./nuttx/tools/build.sh \
  vendor/allwinnertech/boards/a733/cubie-a7z/configs/nsh \
  --cmake -b cmake_out/cubie-a7z_nsh -j8
```

此前验证过的构建因规避旧 CMake 目录，使用过 `_py` 目录；正常新构建应以实际
输出目录为准：

```text
cmake_out/cubie-a7z_nsh_py/nuttx.bin
```

把它复制为 RadxaOS 根文件系统中的 `/boot/openvela/a733/Image`，并将
`tools/extlinux-openvela.conf` 中的条目加入已有的
`/boot/extlinux/extlinux.conf`。在 openvela NSH 基线通过前，保留 Debian 条目
作为默认恢复路径。

串口中预期首先出现的 openvela 标记为：

```text
A7Z0
```

随后应出现 NuttShell 横幅；这表明 GICv3、虚拟定时器、MMU、堆和 UART 中断路径
均已正常工作。
