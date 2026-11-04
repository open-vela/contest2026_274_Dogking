# UFS v166：板级只读取证候选与测试关口

2026-10-21。用户要求持续推进到需要上板测试的可烧写镜像。本版将 v165 后续板级
检查纳入实际固件，**不是 UFS 已可读写或高速存储通过版本**。旧镜像、源码和开发过程
文档均保留，不格式化、不迁移模型、不借用 NPU 池。

## 为什么这一步需要实际板测

UFS 芯片为用户所报 Kingston UFS128-CY14。订货后缀、板号/贴装电阻、芯片端实际电压、
固件供电交接、UFS DMA 互连映射仍未验证，不能根据型号或 CCU gate 直接访问 HCI。
此前 v164 的 gate/reset 检查只是必要条件，不足以证明窗口可访问，本版明确收紧。

本地 U-Boot `drivers/smc/sunxi_smc_v2.c` 声明 `ARM_SVC_ARISC_READ_PMU=0x8000ff12`。
但本地 `a733-product-aiot-stable/bin/bl31.bin` 的 32 位 SMC 分派在文件偏移
0x24f8..0x2570 对该值走未实现路径，而不是 PMU 读取。**不能凭头文件存在就调用。**
该二进制不等于用户板上当前 BL31 已核验；本版不调用它，也不更换固件。
相邻 `0x8000ff14/15` 在源中是关机服务，尤其禁止“试邻近编号”。

不直接抢占系统 S-TWI0/PMIC 总线；它可能同时由固件/ARISC使用。不会盲写 PMIC、
共享 PLL、STORE/MSI gate 或 UFS reset。需要从真实供电和当前固件状态建立下一版 adapter。

## 实现

- `a733_ufs_board.c/.h`：固定13项 CCU/RTC地址，两次顺序采样，最多26次读；不重试。
  包括 PLL_REF/PERI0/pattern、STORE AHB/MBUS、MSI-lite1、UFS AXI/CFG/BGR/REF、RTC XO/XO1。
- 无 HCI、SID/芯片序号、PMIC I2C、SMC 或 MMIO-write 回调，无 DMA/介质访问。
- 两次值不同返回 `-EAGAIN`，保留失败地址/阶段，`stable=0`；非法正回调转 `-EPROTO`。
- 只解释 mux/divider/reset/gate 位；`dcxo-selected-hz` 是 RTC selector 对应值，
  **不是物理频率测量**。不凭 `pll-peri0-480m` 名字假定频率，更不调整共享 PLL。
- `echo board > /dev/a733-ufs` 显式采样；`cat` 展示缓存报告及现有CCU状态。
  注册节点不执行采样，`board` 不进入启动关键路径。
- 没有真实板级供电/domain/ownership/DMA provider，因此生产 HCI 门禁保持关闭，
  `probe`、`attach-ro=N` 返回 `-EACCES`，没有 force 参数/配置文件可绕过。
  `board` 采样成功 **不会** 解锁它们；“只读取证成功”不等于“资格通过”。
- 修复旧 `/dev/a733-hw` 诊断直接读取 UFS HCS/HCE 的旁路，改为报告CCU且标 `HCI-not-read`。
- v165 M-PHY/UIC核心保留并参与构建源清单，但无真实 caller，完整链接时可被 GC；
  不宣称它已在镜像上执行，也没有虚假的冷启动入口。
- 历史只读 transport 保留，生产中被门禁阻止。其主机正向回归使用明确的 **host-only**
  许可 shim；真实 `a733_ufs_board_hci_allowed()` 单独链接/断言始终拒绝。测试许可不是固件功能。

## 上板测试（只执行本节，不执行旧版 attach 建议）

镜像：`../openvela-a733-cubie-a7z-sd-v166-ufs-board-candidate.img`。
大小/SHA以同版封存 `out/v166-ufs-board-candidate-20261021/manifest.json` 为准。
已生成 2147483648B（2GiB），SHA-256：
`fced203da3290c40533f7dea28ce61304d6c34718432d65e74bd687157e1bcd4`。
内核2701272B，SHA-256：
`fbc7417cc9e94cd5dddd54c0d27ddca713828d4c44ae2fc842e293567126145c`。
这是整卡 SD 镜像；**烧录会覆盖目标 SD，包括原有 /data 和配置，先备份**。
确认刷写目标是正确 SD，不是系统盘、其他数据盘或 UFS。制作镜像本身没有刷写设备。
旧 v162/v163/v164 镜像不覆盖，恢复时重新刷入需要的旧版并还原自己的数据备份。

从冷断电启动，完整保存串口日志。进入 NSH 后：

```text
free
ls -l /dev/a733-ufs
cat /dev/a733-ufs
time "echo board > /dev/a733-ufs"
cat /dev/a733-ufs
time "echo board > /dev/a733-ufs"
cat /dev/a733-ufs
echo probe > /dev/a733-ufs
cat /dev/a733-ufs
echo attach-ro=0 > /dev/a733-ufs
cat /dev/a733-ufs
ls -l /dev/ufs0
free
dmesg
```

预期：

- 初次 `cat` 可见 **v166**、`board-v166: sampled=0`，没有自动UFS初始化。
- `board` 应迅速返回，报告 `reads=26 stable=1`、13项raw和解码字段，第二次 `attempts=2`。
  `snapshot-changing/-11` 表示状态发生变化，请原样保存，不连续重试。
- `qualification: ... unknown ... hci-access=blocked` 是本版刻意保留的安全限制。
- `probe`、`attach-ro=0` 的 **Permission denied/-13 是预期负例**，不是存储通过，
  不据此反复重试；`/dev/ufs0` 应不存在。不会读容量/分区/数据或产生DMA。
- `free` 不应因这两个负例新建 UFS DMA 驻留区。
- 若卡住/异常，不继续试其他 UFS命令，保存最后输出、冷断电，并用保留镜像恢复。
  软件有界读次数不是设备总线挂死时的软件解救保证。

第一轮结束后可再次冷断电复测一次。请回传两次完整日志，并提供实际板号和供电信息；
如具备安全测量条件，由熟悉硬件的人测 VCC-UFS、VCC12-UFS、PHY 供电等，不在BGA球脚上
随意探测/短接。不要求用户为运行这版改压或补焊。

可选 Linux 取证工具包保持独立：`out/a733-linux-ufs-kingston-20261021.tar.gz`，
只读取证 `sudo python3 collect-a733-ufs-linux.py`；不要加 benchmark，先确认供电/枚举。
Linux 成功仍不能直接授权 openvela 接管 DMA，详见 [器件核对](UFS_KINGSTON_CY14.md)。

## 软件验证和构建

```sh
python3 tools/test-a733-ufs-board.py
python3 tools/test-a733-ufs.py
python3 tools/test-a733-ufs-link.py
python3 tools/test-ufs-linux-collector.py
# NEW_BUILD必须是quickly-openvela/cmake_out下尚不存在的独立目录。
A733_TENSOR_EXPERIMENTAL=1 A733_UFS_EXPERIMENTAL=1 \
A733_BUILD_DIR="$NEW_BUILD" JOBS=8 bash tools/build-a733-wapi.sh
# 在暂存仍生效时从另一个终端执行：
python3 tools/check-a733-tensor-staged.py "$SDK" --ufs
# 完成后审计SDK恢复，再使用新名称生成，不覆盖BASE或旧镜像：
bash tools/package-a733-kernel-image.sh "$BASE" "$NEW_IMAGE" "$NEW_BUILD/nuttx.bin"
bash tools/verify-a733-image.sh "$NEW_IMAGE" "$KERNEL_SHA256"
```

构建工具链、配置、完整日志、34项critical源码hash、SDK恢复校验、Image/ELF、
源归档和镜像提取比对一并封存。完整构建/镜像校验与实机boot/hardware/stress分开记录。
源码对象最大栈：UFS224B、board sampler/report208B、UIC112B、板初始化64B；
旧综合hwdiag仍有3392B栈帧（未在本轮扩展），不要在小栈线程中调用它。

## 下一版，不在本版宣称完成

确认实际供电/板级选件和当前固件支持的仲裁接口 → 独立 power/clock/reset adapter →
真实 M-PHY/UIC执行 → NOP/Query/device init → DMA映射/lifetime → 只读容量与数据 →
经用户确认的独立scratch写回/flush/分区/挂载 → 性能与模型迁移。
在供电证据缺失时继续编造“通过”或直接开HCE只会引入新故障，故此处是必要的实测关口。

恢复点：`out/checkpoints/a733-pre-ufs-board-v166-20261021/` 保存修改前880个文件。
所有旧开发过程文档、失败/成功日志、旧版本镜像和新候选分别保留。
