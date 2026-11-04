# v140 启动失败与 v141 链接/MMU 修复候选

后续实测：v141 已进入 NSH、8 核及计时/双频 Wi-Fi 扫描通过，见 [实机证据](BLE_V141_BOOT_HARDWARE_PASS.md)。下文保留故障定位和构建时的判断；BLE Host/ACL/GATT 仍未通过。

用户实测：未到 NSH，N4/SMP 输出后出现 CPU0 fault，随后重复 F0/F 字符。v140 不可记为 boot-passed；其 build/image 校验通过记录仍保留，但不再推荐烧录。

## 用户寄存器摘录（不是完整原始串口文件）

```text
FAULT cpu=0000000081000000 CurrentEL=0000000000000004
ESR_EL1=0000000096000047
ELR_EL1=000000004020328c FAR_EL1=00000000424c3000
regs=00000000424c2a00 LR_X30=00000000403ec26c
SP_ELX=00000000424c2d40 SP_EL0=00000000425295c0
saved_ELR=000000004020328c SPSR=00000000600003c5
X00=00000000424c2e70 X01=00000000425293d0
X02=0000000040498ed0 X03=00000000019e3040
X04=00000000001e2fc0 X05=0000000000000004
X06=000000000000000a X07=000000000000000a
X08=000000000000000a X09=00000000425295c0
X10=00000000600003c5 X11=0000000000000000
X12=0101010101010101 X13=0000000000000030
X14=0fffffffffffffff X15=0000000000000028
X16=0000000000000000 X17=0000000000000000
X18=0000000000000000 X19=0000000040493a9c
X20=00000000001e3000 X21=000000004067d000
X22=0000000001800000 X23=0000000000000040
X24=000000004249cc08 X25=0000000000000007
X26=0000000040498000 X27=0000000040498000
X28=00000000019e3000 X29=0000000000000000
```

## 确定事实与未确定项

- 使用保留的 v140 ELF（对应已交付 kernel SHA256 `0298d4a7...f2edb10`）解析：PC 是 `arm64_fpu_save` 的 `stp q24,q25,[x0,#384]`，LR 是当前 EL/SPx 同步异常入口保存 FPU 后的返回点。
- `x0 + 384 = 0x424c2ff0`，32-byte 写跨入 FAR 指向的 `0x424c3000`。ESR 表示同 EL Data Abort、写访问、level-3 translation fault。这很可能是异常处理过程中的二次异常，不能仅凭它断言最初哪个任务或哪条指令有错。
- v140 ELF 栈区实际在 `0x4250e000..0x42526000`；打印 SP_ELX 却在 BSS 范围，不能据此宣称只需加大栈即可解决。也不能断言第一次异常地址已恢复。
- **确定的链接契约缺陷**：`_srodata=0x403ed000`、`_erodata=0x4046d354`、`_szrodata=0x80354` 非整页。官方 `init_xlat_tables` 要求地址/长度均为 4KiB 页对齐，而 MMU_ASSERT 原先关闭。v139 也有此潜在缺陷（只读区长度 `0x73c08`）；其实机成功不等于这项契约正确，不能把 v140 失败唯一归因为它。
- **确定的 orphan sections**：`.z_init_POST_KERNELCONFIG_KERNEL_INIT_PRIORITY_DEVICE_0_` 位于显式 rodata/data 区之间，`.z_devstate` 位于原 `_image_file_end` 之后。需要把不可变 init entries、可变 device state 分别安置到 RO/RW 区。
- **确定的 header 契约缺陷**：Image offset 0x10 仅记录 `_image_file_end - _image_start`，没有完整 BSS/多核栈占用。ARM64 Image 协议要求 `image_size` 范围为内核保留，不是仅磁盘文件长度。依据：[Linux 官方 ARM64 启动协议](https://docs.kernel.org/arch/arm64/booting.html)。是否实际发生 DTB/启动器分配覆盖仍需 bootloader 地址和实机输出证明。

## v141 修复范围

1. 板级 dramboot.ld 页对齐 rodata 末端；显式收纳 Zblue immutable init entries、mutable state；加入 text/rodata/data 链接断言。
2. Image header 使用 `_image_end - _image_start`，完整声明运行期占用；文件本身仍不填充几十 MiB BSS。
3. 开启 ARM64_MMU_ASSERT，避免无声忽略 page alignment/table bound 条件。
4. 新增 `tools/check-a733-elf-layout.sh`：检查 MMU 区间、BSS/栈次序、orphan、BL31 边界、Image magic、文件长度与完整运行期 footprint。已用 v140 ELF 作负例，返回 `_szrodata is not 4KiB aligned`；不替代实机启动测试。
5. 新增启动标记 `M0 v141-layout`（MMU 前）与 `M1`（MMU 后），确认实际烧录版本及更早故障位置。

恢复分支：`backup-before-v141-bootfix`。原 v139/v140 镜像与 v140 ELF 不覆盖。永久变更位于比赛仓库，官方环境仅临时暂存构建并恢复。Bluetooth 扫描/GATT 再次暂停，先通过 boot/SMP/Wi-Fi 基础回归。

## v141 构建与镜像校验结果

源码提交 `9814948`，WSL clean build（12 jobs）成功；仍未上板，不宣称首次异常已完全定位或硬件问题已全部解决。

- ELF/内核目录：`quickly-openvela/cmake_out/cubie-a7z_nsh_v141_boot_layout`。
- `nuttx.bin`：2577816 bytes，SHA256 `0fa1cbc1b4bbe259f74febf7ce3da5f1b061f5e0653f063b800acd5d8daf659e`。
- Image header effective runtime size：36855808 bytes（35.15 MiB）。`_szrodata=0x81000`、`_srodata=0x403ed000`、`_sdata=0x4046e000`、`_e_initstack=_image_end=0x42526000`。
- 新 ELF layout checker 的页对齐、区域顺序、栈覆盖、无 Zblue orphan、Image magic、文件/完整运行期长度检查全部通过。
- 镜像：`A733-A7Z-ALL-Files/openvela-a733-cubie-a7z-sd-ble-v141-boot-layout-candidate.img`，2147483648 bytes，SHA256 `a81e1005c4ba33b107b502b347dd7ec5e8e74908c17dde0f0eeaf1e1ee5caf6b`。
- 从 v139 副本仅替换 partition 3 Image；源 v139 不覆盖。e2fsck 通过；sgdisk 显示 No problems found（继承 data 分区结尾非 2048-sector 边界提示）；提取内核 SHA256 与本轮内核一致。
- 构建日志：`docs/evidence/20261013-v141-build.log`，打包/校验日志：同目录 `20261013-v141-package.log` / `20261013-v141-verify.log`。
- 官方工作区 EXIT 恢复后，Image header SHA256 `f0574aeb2cfd3409c7231fa0cc8504dac7192fd43f0cee90283cbf78314ba733`，dramboot.ld SHA256 `f6f6acf99289131f6ebee3fdbb69d8f81257d9e333292130ed83c5c186752dc5`，均等于本轮暂存前备份。永久修复在比赛仓库 overlay 中。

## 上板顺序

先备份 SD 的 /data 配置、凭据和模型；候选整卡镜像会替换卡上数据。写入 v141 后断电冷启动，保存完整启动输出。应看到 `M0 v141-layout` → `M1` → NSH。出现 assertion/FAULT 或缺失 NSH 就停在启动故障排查，不进入 BLE 测试。

NSH 基础回归：

```text
ps
aipetllm cpucheck
time "sleep 5"
ifconfig
wifi scan
dmesg
```

首先确认 8 核、计时及 Wi-Fi 正常；当前不要启动 bluetoothd/bttool。基础门通过后才恢复 `BLE_OFFICIAL_HOST_TEST.md` 的手动官方服务、扫描与共存验收。
