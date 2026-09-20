# A733 / Cubie A7Z NPU 官方资料审计与 openvela 移植索引

更新时间：2026-08-24

## 1. 已确定的驱动路线

A733 官方 device 配置使用：

```text
CONFIG_AW_NNA_VIP=m
CONFIG_NNA_VIP2=y
```

因此 openvela 的唯一主线是 `aw_nna_vip/vip2`。`aw_nna_galcore` 和 VIP1 可以
用于理解 Vivante 类接口或其他芯片历史实现，但不得混入 A733 主驱动，否则会
造成寄存器、MMU、任务格式和固件 ABI 错配。

## 2. 官方硬件资源

| 资源 | A733 值 | 用途 |
| --- | --- | --- |
| NPU MMIO | `0x03600000`, `0x1000` | VIP2 核心寄存器 |
| 中断 | GIC SPI 65 / NuttX IRQ 97 | 任务完成和错误中断 |
| 电源域 | PCK600 domain 4 | NPU 电源开关与状态 |
| PCK600 | `0x07060000` | 电源域控制 |
| CCU | `0x02002000` | NPU 时钟、bus gate、reset |
| NPU functional clock | CCU `0x0b00` | divider、parent mux、gate |
| NPU bus/reset | CCU `0x0b04` | bus gate，core/AXI/AHB reset |
| AHB protected gate | CCU `0x05c0`, bit 6 | key `0x010000ff` |
| MBUS protected gate | CCU `0x05e0`, bit 18 | key `0x41055800` |
| 首版频率 | 600 MHz | parent mux 2，`pll-peri0-600m` |
| 首版电压 | 800 mV | 官方最低安全 OPP，不动态调压 |

VIP2 身份寄存器使用官方调试层定义：`0x20` version1、`0x24` version2、
`0x28` date、`0x30` CID/PID。

PCK600 domain 4 的官方延时初始化值为 `0x1f1f1f`、`0x1f1f`、
`0x08080808`、`0x808`、`0x8`；电源命令/状态的 on 值为 8，状态掩码为
`0xf`。所有轮询必须有确定超时，严禁在板级初始化中无限等待。

## 3. 本地官方资料位置

- Linux 5.15 SoC DTS：
  `bsp-product-aiot-stable/configs/linux-5.15/sun60iw2p1.dtsi`
- Cubie A7Z board DTS：
  `allwinner-device-device-a733-v1.4.6/configs/cubie_a7z/linux-5.15/board.dts`
- VIP2 驱动：
  `bsp-product-aiot-stable/drivers/npu/aw_nna_vip/vip2`
- Runtime API 文档：
  `docs-product-aiot-stable-Software 软件类文档/Software 软件类文档/SDK模块开发指南/NPU模块开发指南/NPU_RuntimeAPI_参考指南.pdf`

VIP2 目录内需要按层阅读：硬件寄存器与 feature database、内部 MMU、内存
管理、task/提交与同步、Linux OS/platform/allocator 适配层以及
`inc/vip_lite_config`。移植时保留硬件公共层，Linux 线程、锁、DMA、cache、
IRQ、文件接口和用户地址转换必须改写为 NuttX 实现。

当前资料中尚未发现可以直接确认用于 A733 的完整 Windows/WSL 模型转换器与
最终 NBG 产物工具包。该缺口不影响硬件和 runtime 移植，但在端到端模型阶段
必须先确认官方转换器版本、算子覆盖、量化规则和生成格式，不能自行猜测 ABI。

## 4. 已落地的 v34 检查点

相关实现位于：

- `quickly-openvela/vendor/allwinnertech/chips/a733/a733_npu.c`
- `quickly-openvela/vendor/allwinnertech/chips/a733/include/a733_peripherals.h`
- `quickly-openvela/vendor/allwinnertech/chips/a733/Kconfig`
- `quickly-openvela/vendor/allwinnertech/chips/a733/CMakeLists.txt`
- `quickly-openvela/vendor/allwinnertech/boards/a733/cubie-a7z/src/a7z_boardinit.c`
- `quickly-openvela/vendor/allwinnertech/boards/a733/cubie-a7z/configs/nsh/defconfig`

v34 只验证电源、时钟、reset 与 VIP 身份，不提交 NPU 命令，不碰 PMIC，不声称
模型推理已完成。状态节点为 `/dev/a733-npu`，失败不会阻塞系统启动。

### v34 首次上板结论

首次日志证明 PCK600 domain 4 已经成功进入 on 状态，但 AHB、MBUS、NPU clock
和 BGR 四个 CCU 寄存器全部读回零。根因不是 NPU 缺失，而是首版错误使用了
`0x02001000`；官方 `sun60iw2p1.dtsi` 中主 CCU 节点实际位于
`0x02002000`。v34.1 已修正该基址，所有寄存器偏移和门控位保持官方定义。

## 5. 后续阶段与完成条件

1. **v34 硬件身份**：板端 `checkpoint=0`，ID 非全零/全一；
2. **v35 IRQ 与恢复**：挂接 IRQ 97，验证有界中断/软复位/超时恢复；
3. **v36 内存和 MMU**：低地址连续一致性 heap、cache 操作、guard，以及
   VIP2 内部 MMU 页表；绝不能把 4 GiB 高地址直接截断给 32-bit DMA；
4. **v37 最小命令**：提交官方确定性 NOP 或小算子，等待完成中断，核对输出
   hash 和前后 guard；
5. **v38 openvela runtime**：形成 `/dev/npu0` 与 VIPLite 子集，支持 context、
   buffer、submit、sync、cancel、reset；
6. **v39 模型工具链**：先跑 YOLOv8n INT8 固定输入，并与 CPU 参考逐元素比较；
7. **v40+ 离线助手**：小型 ASR/TTS、约 1.5B INT4 LLM 与人脸特征模型采用
   CPU/NPU 混合执行，依据算子覆盖和实测内存决定下沉比例。

每阶段上板都必须回归 NSH、4 GiB split RAM、SD/FAT、温度节点和 FCU760K。
FCU760K 已冻结，不得把无线修改混入 NPU 提交。最终目标是稳定可用的离线
多模态应用，不以“能加载模型”代替数值正确性、超时恢复和长期稳定性验收。

## 6. v34.1 板端身份与 v35 IRQ/reset

v34.1 已在真实 Cubie A7Z 上确认：PCK domain 4 为 on，NPU clock 为
`0x82000000`，BGR 为 `0x000f0001`，VIP2 identity 为
`00009000/00009202/20230518/1000003b`。这完成了阶段 1。

v35 依据官方 VIP2 公共硬件层加入：

- 最多 10 次、要求连续两次状态正确的有界软复位；
- reset 后验证 idle `0x7fffffff`、control `0x30000` 和 MMU disabled；
- 读取 `0x10` 清除遗留 IRQ ACK，挂接 level-high NuttX IRQ 97；
- 向 `0x14` 写入官方全事件使能值，并记录 IRQ count 和 last ACK；
- 不生成未知测试命令，因此 v35 只证明 IRQ 路径已安全挂接，首个真实中断随
  后续最小命令提交一并验证。

## 7. v36 low32 连续一致性内存

v35 板端确认 reset checkpoint 0、idle `7fffffff`、MMU `0`、IRQ 97 attach
checkpoint 0，且没有伪中断。v36 新增独立 `a733_npu_memory.c/.h`：

- 在 stage-1 `.bss` 中保留 2 MiB、4 KiB 对齐的 NPU arena；实际链接地址为
  `0x4028a000..0x4048a000`，低于 4 GiB 且远离 BL31；
- 所有 payload 至少 64 字节对齐和取整，前后各有一条 cache line guard；
- 分配时拒绝地址截断、高地址越界、非 2 次幂对齐及 arena 越界；
- 提供 ARM64 D-cache clean/invalidate 接口和 guard 扫描；
- 启动时用 64 KiB 确定性模式执行 clean、invalidate、逐字核对和 guard 自测，
  成功后回卷 arena，供后续 MMU 页表与命令缓冲区使用。

这一阶段只建立 CPU/NPU 可共享内存的安全基础，不在没有页表定义时提前打开
VIP2 MMU。

## 8. v37 官方最小命令与真实 IRQ 交付

官方 `vip_drv_hardware.c` 和 `vip_drv_task_schedule.c` 已确认以下编码，v37
不再猜测命令格式：

- `cmd_event(block=(1<<6), id=4)` 产生 `0x08010e01, 0x00000044`；
- END 为 `0x10000000, 0x00000000`；
- 物理命令地址写 `VIP+0x654`；
- `VIP+0x3a4` 写 `0x00010000 | (vipdMAX_CMD_SIZE >> 3)`，其中
  `vipdMAX_CMD_SIZE=65535*8`，最终值为 `0x0001ffff`；
- IRQ enable 为 `VIP+0x14=ffffffff`，IRQ ACK 从 `VIP+0x10` 读取并清除；
- 官方 Linux ISR 对非零 ACK 逐 bit 计数，event ID 4 对应 ACK bit 4。

v37 保持 MMU bit 0 为零，直接让 VIP2 从 identity-mapped low32 arena 读取
16 字节命令。这一步同时验证寄存器提交、NPU 对共享内存的可见性、前端取指、
EVENT、END、GIC SPI65/NuttX IRQ97 以及 cache/guard 路径。测试必须手动触发，
所有等待有上限，失败必须复位恢复；板端通过后才有依据进入内部 MMU 页表。

## 9. v38 Page-Descriptor MMU 翻译路径

官方 `inc/vip_feature_database.h` 中，板端读到的 `CID/PID=0x1000003b`
对应 `mmu_pd_mode=1`。官方 `vip_drv_hardware.c` 对 32-bit VA 的 Page-Descriptor
初始化顺序为：

- MTLB 物理地址右移 12 位；
- page-directory entry 取 `(mtlb_physical >> 12) << 4 | 1`；
- entry 写到 VIP 寄存器偏移 `0x3b4`；
- MMU control 偏移 `0x388` 写 `0x21`；
- MTLB 索引使用 VA bit 31..24，共 256 项，表大小 1024 字节，但 PD 模式要求
  4 KiB 对齐；
- 4K STLB 索引使用 VA bit 23..12，共 4096 项，表大小 16 KiB；
- MTLB present bit 为 bit 0，4K page type 为 0；
- STLB 有效项为物理页地址加 present、exception 和 writable 位，即低三位
  `0x7`；空闲项按官方实现填 `0x2`；
- IRQ `0x40000000` 是 MMU fault，不能当作普通完成事件接受。

v38 使用 VIP VA `0x01000000`，因此 MTLB index 为 1、STLB index 为 0。页表、
命令页均来自已经通过 v36/v37 验证的 low32 coherent arena；写表和写命令后
执行 cache clean 与 DSB，再以虚拟地址提交 EVENT+END。只有收到 event bit 4、
未出现 MMU fault、最终全模块 idle 且所有 guard 完整时才判定成功。测试出口
统一软复位并关闭 MMU，避免失败页表状态污染后续 NSH 或外设。

### 9.1 low32 arena 的大页表对齐陷阱

v38 首次板端日志显示 `mtlb=4028c000`、`stlb=4028f000`，驱动在写 MMU
寄存器前以 `-EFAULT` 拒绝测试。这证明不能只对 allocator 的相对 offset 做
16 KiB 对齐：arena 基址仅有 4 KiB 保证时，最终物理地址仍可能带有非零的
16 KiB 模数。v38.1 使用 `align_up(arena_cpu + used + guard, align) -
arena_cpu` 计算 payload offset，使 CPU 地址和 identity-mapped DMA 地址同时
满足调用者对齐，并在地址计算前增加剩余空间检查。

真实板端验证得到 MTLB `0x4028c000`、STLB `0x40290000`、命令物理地址
`0x40295000`、VIP VA `0x01000000`、PD entry `0x004028c1` 和 control
`0x21`。EVENT IRQ `0x10` 正常到达且没有 `0x40000000` MMU fault，证明上述
Page-Descriptor 编码、cache clean、页表 walk 和翻译后命令取指均正确。

## 10. v39 openvela runtime 边界

官方 Linux VIPLite driver 的完整 command union 包含 initialize、allocation、
wait、cancel、submit、cache、database 和 task 生命周期等大量 Linux 相关状态。
openvela 第一阶段不照搬 dma-buf、ION、进程数据库和 Linux ioctl，而是保留
已被 A733 硬件证明的最小语义：driver-owned low32 buffer、VIP VA、cache
ownership、单核任务串行化、IRQ completion、timeout/cancel/reset。

公共结构使用固定宽度字段和 64-bit CPU address，buffer handle 含 generation，
避免释放后旧 handle 误用。共享池的全部物理页在初始化时写入同一 4K-STLB；
用户只能取得其中的 handle，不能让驱动映射任意地址。当前 submit 是同步的，
但保留 task id 和 sync ABI，后续可在不破坏应用接口的情况下改为队列 worker。
命令流必须包含 EVENT 4 并以 END 结束，这是现阶段完成中断的明确契约。

## 11. v3 NBG、VIPLite 2.0 与许可证边界

本地官方《NPU 模型部署开发指南》明确把 A733 列入 v3 NBG 平台，并给出硬件
目标 `VIP9000NANODI_PLUS_PID0X1000003B`。官方 Runtime API 的标准次序是
`vip_init -> vip_create_network -> vip_prepare_network -> query/create/set I/O ->
vip_run_network -> vip_finish/destroy_network`。其中 `vip_prepare_network()` 会
分配内部命令缓冲区并修补命令，因此 NBG 不是可直接提交给 v39 ABI 的裸命令流。

为了交叉验证，研究目录仅按需取得公开 Cubie AI SDK commit
`fc90006d0f6569da2f6726c2d8395877686f5aca` 的文件索引、VIPLite 2.0 头文件/
二进制、`vpm_run.c` 和 Lenet v3 样例。结果如下：

- `libNBGlinker.so` 是 AArch64 glibc ELF，依赖 `libVIPhal.so`、libm、libc 和
  pthread；导入大量 `viphal_task_*`、MMU、内存、cache、文件与 OS 接口；
- `libVIPhal.so` 同样是 AArch64 Linux ELF，不能直接链接进 NuttX flat build；
- Lenet v3 文件头为 `VPMN/00010020/1000003b/lenet_uint8_NCHW`，文件大小
  444152，标准 CRC32 为 `77b1738e`；
- `vip_lite.h` 文件头明确写明 Vivante confidential、proprietary、all rights
  reserved；仓库根目录没有 LICENSE/COPYING。

因此这些文件只能作为本地互操作研究证据，禁止复制实现、自动打包或假定具有
再分发授权。v40 只实现独立编写的元数据探测 ABI。真正模型执行有两条合规路线：

1. 获得 Allwinner/VeriSilicon 对 VIPLite/NBG runtime 的 NuttX 源码或移植授权；
2. 获得文档化、可再实现的离线已链接命令包规范，在主机端完成 prepare/link，
   openvela 只负责经过签名/边界/重定位校验的命令和张量提交。

在任一路线成立前，不得把 NBG 文件头探测、EVENT+END 自检或 CPU 计算包装成
Lenet/YOLO NPU 推理成功。
