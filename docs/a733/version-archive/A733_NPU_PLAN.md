# A733 / Cubie A7Z openvela NPU 优先适配计划

更新时间：2026-08-30

## 当前进度：v47 命令尾页实板通过，进入通用模型/YOLOv8

- v46 已在实板完成 YOLOv5s 640x640 三输出金标准验证，CRC32 为
  `53127e9c`、`ddb5675e`、`6e01633e`；LeNet、runtime selftest 和 apitest
  回归全部通过；
- 唯一未闭环项是所有正确输出完成后出现的尾部 MMUv2 fault
  `0x01014000`；它正好位于命令区最后映射页之后，A7PM 不在该页放置数据；
- v47 已加入不与模型区域重叠的 4 KiB 安全 END 尾页、运行前后 CRC 保护和
  状态输出；实板确认 `hw-status=0`、`mmu=0`、`fault=0`、`recovery=0`、
  `tail-changed=0`，三个输出 CRC/changed 不变；
- YOLOv5 后再次运行 LeNet、selftest、apitest 全部通过，证明页表恢复、固定
  轨迹和通用 ABI 没有回归；
- 现在进入通用模型准备/链接和 YOLOv8n 模型转换，不再重复已完成的
  VIP2 供电、IRQ、MMU、低地址 DMA、提交 ABI 与 YOLOv5 执行链路。

### YOLOv8 工具链输入要求

官方 2026 文档确认 A733 使用 NPU v3 / software v2.0，当前匹配的 x86 Linux
Docker 镜像为 `ubuntu-npu:v2.0.10.2`，来源包为 `docker_images_v2.0.x.zip`
内的 `ubuntu-npu_v2.0.10.2.tar.zip`。现有目录只有 YOLOv5 NBG 和 VIPLite
运行库，没有该 ACUITY/Vivante 编译器镜像，也没有 YOLOv8 A733 `.nb`，因此
不能在不伪造模型的情况下生成下一份授权轨迹。取得该包后按以下顺序继续：

1. 在 WSL/x86 Linux 导入 `ubuntu-npu:v2.0.10.2`，核对 ACUITY 与
   VivanteIDE 版本；
2. 使用 A733 v3 配置将 YOLOv8n ONNX 截断为 6 个原始检测头，NMS、DFL 和
   box decode 留在 CPU；
3. 用代表性图像分别评估 PCQ INT8 与 INT16，先比较 ACUITY float/quantized
   输出，再导出目标 `VIP9000NANODI_PLUS_PID0X1000003B` NBG；
4. 在官方 Debian VIPLite 上运行固定输入并采集 trace，生成多输出 A7PM；
5. 在 openvela `/dev/npu0` 执行并逐输出对比官方 golden，随后实现图像预处理、
   DFL 解码、置信度筛选和 NMS。

## 当前进度：v41 NBG/输入 low32 staging 等待上板

- 已确认 A733 官方产品配置实际选择 `CONFIG_AW_NNA_VIP=m` 与
  `CONFIG_NNA_VIP2=y`，后续只移植 VIP2 公共硬件层；
- 已实现独立、非阻塞的 NPU 供电域、时钟、reset 和 VIP 身份寄存器检查点；
- 已加入只读状态节点 `/dev/a733-npu`，即使探测失败也保留诊断信息且不阻塞
  NSH 和现有外设启动；
- 当前固定使用官方 OPP 600 MHz / 800 mV，不修改 PMIC 电压，不启用 DVFS；
- WSL 全量编译、最小 SD 镜像更新、内核哈希核对及 ext4 离线检查均已通过；
- 下一步只有在板端确认 `checkpoint=0` 且 VIP ID 有效后，才进入 IRQ、内部
  MMU/低地址一致性内存和最小命令提交。
- v34 首次上板确认电源域开启但 CCU/VIP 全零；已依据官方 DTS 将误用的
  `0x02001000` 修正为主 CCU `0x02002000`，v34.1 已重新构建和封装。
- v34.1 板端已经得到有效 VIP2 身份 `00009000/00009202/20230518/1000003b`
  和 `checkpoint=0`；v35 已加入官方有界软复位流程、IRQ 97 挂接、读清 ACK、
  全事件 mask 以及 IRQ 计数/最后状态诊断。
- v35 板端 reset、idle、MMU-disabled 和 IRQ attach 全部通过。v36 已增加位于
  stage-1 低地址 BSS 的 2 MiB 连续 arena、64 字节 cache-line 对齐、每缓冲区
  前后 guard、32-bit 物理地址检查及 clean/invalidate 自测。

## 已确认的官方参考

- Cubie A7Z Linux 5.15/6.6 设备树均启用 `&npu`；5.15 明确使用
  `reg_dcdc2` 作为 `npu-supply`；
- BSP 含 `drivers/npu/aw_nna_vip/vip2`，包括硬件、MMU、内存、任务调度、
  电源管理与 Allwinner 平台层；
- BSP 同时含 `aw_nna_galcore` 的 Linux 和 VxWorks OS 端口。VxWorks 层适合
  用来识别 RTOS 所需的线程、锁、事件、DMA、cache、IRQ 与用户/内核接口；
- 不应直接把 Linux 驱动编入 openvela。必须抽取硬件公共层并重新实现 NuttX
  OS、内存和设备接口。

## 推荐阶段

1. **官方基线审计**：确定 A733 NPU IP/VIP 代际、寄存器基址、IRQ、时钟、
   reset、供电、IOMMU/地址宽度以及 Linux 实际启用的 VIP1/VIP2 分支。
2. **无任务硬件检查点**：只开启供电/时钟、释放 reset、读取芯片与核心 ID，
   建立 `/dev/a733-npu` 状态节点；任何失败都不得阻塞 NSH 启动。
3. **IRQ、reset 与超时恢复**：验证软复位、空闲中断、错误中断、看门狗式
   超时恢复，确保错误命令不会锁死系统。
4. **NPU 内存层**：实现低地址、连续、cache 一致的命令/张量缓冲区；确认
   A733 NPU 是否直连物理地址或必须启用内部 MMU/IOMMU，并加入越界 guard。
5. **命令提交与最小自测**：移植 VIP2 公共硬件层，在内核内提交一个官方
   最小网络或确定性算子，检查完成中断、输出 hash 与 guard。
6. **openvela 设备/API 层**：形成稳定 ioctl 或轻量 runtime API，支持上下文、
   buffer、任务提交、同步、取消和错误恢复；避免照搬 Linux ioctl/ION。
7. **模型工具链与端到端推理**：确认官方模型转换器和运行时二进制格式，固化
   一个小型 INT8 模型与输入，CPU 参考结果和 NPU 结果逐元素比较。
8. **稳定性与性能**：循环推理、并发、冷/热启动、内存泄漏、异常模型、温度、
   DVFS/功耗和吞吐测试；通过后再冻结 NPU 版本并进入 GPU。

其中第 1～2 阶段可以合并开发，第 3～4 阶段可以并行审计但应顺序上板，第
5～7 阶段可在硬件自测稳定后合并实现。最终仍必须逐层保留检查点，不能把
所有风险压到一次模型运行中。

## 与现有成果的隔离原则

- 不改动 FCU760K 冻结文件，除非明确修复无线回归；
- NPU 使用独立 `a733_npu*.c/.h`、独立 Kconfig 和独立设备节点；
- 不占用 BL31 保留区 `0x48000000..0x48ffffff`；
- 继续遵守当前 4 GiB split-RAM 映射和 32-bit DMA 可达性限制；
- 第一版禁止动态调压和激进 DVFS，只采用官方安全频点；
- 每一步都先验证 `free`、Wi-Fi、SD/FAT 和 NSH，防止 NPU 初始化破坏既有功能。

## 最终产品目标：离线多模态 AI 助手

最终验收目标不只是 NPU 算子自测，而是在 4 GiB Cubie A7Z 上形成可实际使用的
离线 AI 系统：

1. 麦克风采集、VAD、降噪和流式 ASR；
2. 约 1.5B 参数的 INT4 对话模型，支持多轮但限制上下文长度；
3. 轻量离线 TTS，边生成边播放；
4. YOLOv8n 级别的 INT8 图片/摄像头目标检测；
5. 人脸检测、对齐、特征提取和本地特征库比对；
6. CPU、NPU 混合调度，Wi-Fi、SD/FAT、NSH 等既有功能不得回归。

这里的“NPU 加速”允许混合执行：卷积、全连接和可支持的矩阵算子优先放到
NPU；音频编解码、tokenizer、RoPE/RMSNorm/Softmax、KV cache 管理、NMS、
人脸对齐以及 NPU 不支持的算子放到 CPU。只有在官方转换器和 NBG runtime
确认算子覆盖后，才承诺某个模型全量运行于 NPU。

### 4 GiB 内存约束

- 1.5B LLM 以 INT4 为主目标，权重约 0.75 GiB；FP16 不作为目标，INT8 仅作
  工具链验证；
- LLM 初始设定为 batch=1、512～2048 token 上下文，并优先选择 GQA 模型；
- ASR、TTS、YOLO 和人脸模型采用小型量化版本；大权重按需加载，禁止默认将
  所有模型同时常驻；
- 使用统一模型内存池、可复用 NPU 张量缓冲区和显式卸载策略，至少预留
  512 MiB 给 openvela、网络、音频、图像帧和异常恢复；
- 摄像头检测和完整 LLM 解码默认分时调度；需要并发时再依据实测内存与温度
  逐步开放。

### 应用验收阶段

1. **算子基线**：INT8 Conv2D、MatMul、激活、Resize、Softmax 等逐算子与 CPU
   参考结果对比，并记录不能下沉到 NPU 的算子。
2. **视觉最小闭环**：静态图片运行 YOLOv8n INT8，完成预处理、NPU 推理、CPU
   解码/NMS；再接入摄像头并验证持续帧率、温度和内存。
3. **人脸闭环**：人脸检测只是第一步，还必须加入对齐、embedding 模型和本地
   向量比对；验证注册、识别、陌生人阈值以及误识别率。
4. **语音闭环**：先完成音频采集/VAD，再接小型 ASR；随后接轻量 TTS，验证
   流式输入输出、回声处理和首包延迟。
5. **LLM 闭环**：先运行小于 1B 的 INT4 模型验证 tokenizer、KV cache 和混合
   算子调度，再升级到 1.5B INT4；记录首 token 延迟、tokens/s、上下文上限和
   峰值内存。
6. **整机对话**：`VAD -> ASR -> LLM -> TTS` 全离线串联，支持打断、超时、
   会话清理和模型故障恢复。
7. **多模态融合**：将 YOLO/人脸结果作为结构化上下文送入 LLM；连续运行、
   冷热启动、掉电恢复、Wi-Fi 回归和热稳定测试全部通过后才算最终完成。

上述目标在硬件资源层面可行，但属于完整产品级移植，不能仅靠注册 NPU 设备
节点完成。决定成败的门槛是官方 VIPLite/NBG 工具链、Transformer 算子覆盖、
NPU 可访问内存，以及音频/摄像头硬件驱动；任何暂不支持的算子必须有 CPU
fallback，不能以死循环或静默错误代替。

## 当前执行进度：v37 官方 EVENT+END/IRQ 自检

v36 的低 32 位一致性 arena 已在板端通过。v37 使用官方 VIP2 源码中的最小
命令流 `08010e01 00000044 10000000 00000000`，并使用官方提交寄存器
`0x654/0x3a4`。测试不会在启动时自动执行，只能通过：

```text
echo submit > /dev/a733-npu
```

显式触发。实现包含 100 ms IRQ 上限、event bit 4、END 后全模块 idle、MMU
disabled、低地址缓冲区 guard 检查；失败时关闭中断源并运行已验证的有界软
复位恢复，不能阻塞 NSH。板端成功后阶段 3 完成，同时证明阶段 4 内存基础
可被真实硬件取指，随后才启用 VIP2 内部 MMU。

## 当前执行进度：v38 VIP2 Page-Descriptor MMU 翻译自检

v37 已在真实 Cubie A7Z 上通过：官方 EVENT+END 命令产生 IRQ 97，ACK 为
`0x00000010`，最终 idle 为 `0x7fffffff`，guard 与恢复状态均为 0。因此 v38
进入内部 MMU 的最小可证伪测试，而不是直接跨越到模型 runtime。

官方 feature database 将当前 `CID/PID=0x1000003b` 标记为
`mmu_pd_mode=1`。v38 据此建立 4 KiB 对齐 MTLB、16 KiB 对齐 4K-STLB 和独立
命令页，将 VIP 虚拟地址 `0x01000000` 映射到 low32 arena 中的命令物理页，
在 `VIP+0x3b4` 写入 page-directory entry，并在 `VIP+0x388` 写入 `0x21`
开启 Page-Descriptor MMU。随后通过这个与物理地址不同的虚拟地址执行同一条
EVENT+END 流。

测试仍然只允许手动触发：

```text
cat /dev/a733-npu
echo submit > /dev/a733-npu
echo mmutest > /dev/a733-npu
cat /dev/a733-npu
dmesg
free
dd if=/dev/mmcsd0 of=/dev/null bs=65536 count=16
```

成功条件是 `mmutest: checkpoint=0`、`control=00000021`、报告中的物理地址
与 `virtual=01000000` 不同、IRQ count 增加、`value=00000010`、
`idle=7fffffff`、`guards=0` 且 `recovery=0`。无论成功或失败，测试结束都会执行
有界软复位，把设备恢复到 v37 的 MMU-disabled 空闲状态；启动路径不会自动
提交命令。硬件通过后，下一阶段才是 `/dev/npu0` 的 buffer/context/submit/
sync/reset API 与最小 runtime。

### v38.1 对齐修复

v38 首次上板在提交前返回 `-EFAULT`。日志中的 MTLB `0x4028c000` 对齐正确，
但 STLB `0x4028f000` 并非 16 KiB 对齐，因此 `pd/control` 保持 0、IRQ 没有
变化。这不是 MMU 硬件故障，而是 low32 allocator 只对 arena 内偏移对齐；
arena 本身只有 4 KiB 对齐，偏移对齐不能保证最终物理地址满足 16 KiB。

v38.1 改为按实际 CPU/DMA 地址计算对齐，并补充 arena 尾部边界保护。旧 v38
归档已明确标记为 `failed-stlb-alignment-do-not-use`，后续上板只使用 v38.1。

v38.1 已在真实 Cubie A7Z 上通过：`mtlb=0x4028c000`、
`stlb=0x40290000`、命令物理地址 `0x40295000`、VIP VA `0x01000000`、
PD entry `0x004028c1`、MMU control `0x21`。翻译提交产生 event IRQ
`0x10`，IRQ count 从 1 增至 2，最终 idle `0x7fffffff`，guards 与 recovery
均为 0。阶段 4 的内部 MMU、页表可见性和虚拟取指基础至此完成。

## 当前执行进度：v39 `/dev/npu0` runtime ABI

v39 合并完成第一阶段正式 runtime 的可上板骨架：

- `/dev/npu0` 每次 open 分配独立 context，最多 4 个；close 自动回收其 buffer；
- 预建一个 1 MiB、4K 页粒度的 low32 连续共享池，并通过一套 MTLB/STLB
  映射到 VIP VA `0x02000000`；第 0 页只供确定性自检；
- 最多 16 个 generation handle，禁止跨 context 使用和用户自报物理地址；
- 公共 ABI 位于 `nuttx/include/nuttx/npu/a733_vip2.h`，包含 info、alloc、free、
  cache clean/invalidate、submit、sync、cancel、reset 和 selftest ioctl；
- 当前硬件只有一个 VIP2 核，因此使用单硬件队列和同步 submit；task id 与最后
  一次结果可由 sync 查询；超时上限 5 秒，cancel/timeout/fault 均走有界复位；
- 第一阶段只接受包含已验证 EVENT 4 且以 END 结束的驱动 buffer 命令，拒绝
  任意物理地址和无法产生完成 IRQ 的命令流；
- `selftest` 验证预留映射页；`apitest` 额外贯穿 context-owned buffer 分配、
  handle、CPU/物理/VIP 地址、cache、translated submit、结果记录和释放。

本阶段仍不宣称支持 NBG 或神经网络算子；板端 API 自检通过后，下一阶段才是
官方 NBG 容器解析、确定性算子命令和 CPU/NPU 数值对比。

### v39 实板验收完成

v39 已在真实 Cubie A7Z 上完整通过。`/dev/npu0` 报告 MTLB
`0x4028e000`、16 KiB 对齐 STLB `0x40290000`、共享池 `0x40295000`、
VIP VA `0x02000000` 和 PD `0x004028e1`。`selftest` 与 `apitest` 均为 0；
API 测试完成 1 次提交和 1 次完成，IRQ 为 `0x10`、idle 为
`0x7fffffff`、guards 为 0、无超时/取消/残留 inflight，测试退出后 buffer
与 page 计数均回到 0。SDMMC 1 MiB 回归读取和 4 GiB heap 同时正常。

因此 runtime transport、context/buffer 生命周期、cache、VIP2 PD-MMU 提交、
IRQ 完成与失败恢复基础正式封版。下一阶段为模型容器/图解析和确定性真实算子，
并以 CPU 参考结果逐元素对比；在数值一致前不宣称 YOLO 或通用模型可运行。

## 当前执行进度：v40 A733 v3 NBG 流式探测入口

官方 A733 部署资料要求目标 `VIP9000NANODI_PLUS_PID0X1000003B` 和 v3 NBG。
公开 Cubie AI SDK 的最小 v3 Lenet 样例进一步确认文件头为：magic `VPMN`、
格式字段 `0x00010020`、CID `0x1000003b`，随后是 32 字节模型名。公开 SDK
中的 VIPLite 头文件明确标为 Vivante confidential/proprietary，且模型准备、
命令修补逻辑位于依赖 glibc/Linux 的闭源 `libNBGlinker.so`，因此不能把该库
直接链接到 NuttX，也不能把简单文件头识别冒充为网络推理。

v40 将 `/dev/npu0` ABI 升至 v2，并新增 metadata-only model probe：

- `A733_NPUIOC_MODEL_PROBE` 及固定大小的 `a733_npu_model_probe_s`；
- 文本控制 `echo modelcheck=/data/MODEL.nb > /dev/npu0`；
- 只允许 `/data/` 下的绝对路径，拒绝 `..`、反斜杠、超长路径；
- 以 512 字节窗口流式读取，不把整个 NBG 常驻 RAM；
- 校验 64 字节最小长度、`VPMN`、v3 字段、A733 CID 和可打印模型名；
- 对完整文件计算 CRC32，并在 `cat /dev/npu0` 中报告状态、版本、CID、大小、
  CRC、模型名和路径；
- v39 的 context/buffer/cache/submit/selftest/apitest 行为保持不变。

本地参考 Lenet v3 文件的预期探测结果为：size `444152`、NuttX 原始 CRC32
`0x40bb2d73`、name `lenet_uint8_NCHW`。常见 ZIP/zlib 口径（初值和末尾取反）
为 `0x77b1738e`，两者算法约定不同，并非文件损坏。该第三方公开参考仓库没有许可证，
所以模型和闭源库不会自动嵌入发布镜像。需要板测时，可由使用者明确指定自己
有权使用的 v3 NBG：

```text
sudo ./install-a733-nbg-model.sh /path/to/model.nb
```

然后烧写镜像并执行：

```text
cat /dev/npu0
echo modelcheck=/data/model.nb > /dev/npu0
cat /dev/npu0
dmesg
echo selftest > /dev/npu0
echo apitest > /dev/npu0
free
dd if=/dev/mmcsd0 of=/dev/null bs=65536 count=16
```

通过条件是 `model: status=0 version=00010020 cid=1000003b`，文件大小、CRC、
模型名正确，同时 v39 两项硬件测试仍为 0。此阶段只证明容器与目标兼容性，
下一阶段必须实现有授权的 prepare/link 兼容层或离线转换器，再做真实 Lenet
推理和 CPU golden 逐元素对比。

### v40 实板验收完成与 v41 staging

v40 已在真实 Cubie A7Z 上通过。`lenet.nb` 报告版本 `00010020`、CID
`1000003b`、大小 `444152`、NuttX 原始 CRC32 `40bb2d73` 和名称
`lenet_uint8_NCHW`。`selftest`、`apitest`、IRQ、MMU、guard、4 GiB heap、
SDMMC 1 MiB 回归及 FCU760K 启动均正常。

v41 将 ABI 升至 v3，新增独立于任务池的 512 KiB low32 staging 区，并映射到
VIP VA `0x02100000`。`modelstage=` 会先完成 v40 的完整模型探测，再把模型流式
复制到 NPU 可见内存并二次核对大小/CRC；`inputstage=` 将输入样本按 64 字节
对齐放在模型之后。它们不允许通过现有 submit ioctl 当成命令执行，避免把尚未
经过官方 linker 修补的 NBG 误提交给硬件。

板端验证命令：

```text
ls /data
echo modelstage=/data/lenet.nb > /dev/npu0
echo inputstage=/data/lenet.dat > /dev/npu0
cat /dev/npu0
echo selftest > /dev/npu0
echo apitest > /dev/npu0
free
dd if=/dev/mmcsd0 of=/dev/null bs=65536 count=16
```

预期 model 为 `444152/40bb2d73`，input 为 `784/e892cab0`，两条 staging
状态均为 0、物理/VIP 地址非零且 input offset 位于模型末尾之后；既有自检和
回归继续为 0。此阶段完成后，剩余阻塞点只有官方 AArch64/glibc NBG linker
产生的 prepare/link 资源；它不能直接链接进 NuttX。

### v41 实板验收完成

v41 已在真实 Cubie A7Z 4 GiB 板卡完整通过。板端读回结果为：模型
`444152/40bb2d73`，物理地址 `0x40396000`、VIP 地址 `0x02100000`；输入
`784/e892cab0`，64 字节对齐偏移 `0x0006c700`、物理地址 `0x40402700`、VIP
地址 `0x0216c700`。`selftest=0`、`apitest=0`、guards 为 0，任务提交/完成为
`1/1`，IRQ `0x10`，idle `0x7fffffff`，缓冲区与页计数测试后均回到 0。
4 GiB heap、SDMMC 1 MiB 读取以及 FCU760K 启动同时无回归。

因此从 FAT 文件读取、NBG 目标校验到低 32 位 NPU 可见 staging、PD-MMU、IRQ
和同步提交的 openvela 基础链路已封版。下一关不是继续猜测 NBG 内部格式，而是
在官方 Debian/VIPLite 上对相同 Lenet 完成一次标准 prepare/run，导出张量元数据、
量化参数和黄金输出。为此新增 `a733-viplite-golden/`；其 Linux/闭源库只用于一次性
取证，绝不进入最终 openvela 运行依赖。

黄金工具已完成 AArch64 编译/链接检查，并打包为
`a733-viplite-golden-bundle.tar.gz`（SHA-256
`c093241e568b5e3dd8086ab2d984c7ae503f5f24240bca280c7506c15b9c12bc`）。
当前包还包含兼容 GLIBC 2.17 的 AArch64 `libviptrace.bullseye.so`、源文件
`viptrace.c` 与 `run-lenet-trace.sh`。跟踪器通过官方库实际使用的
`ioctl@GLIBC_2.17` 入口记录 VIP2 ioctl，并在 submit/wait 时对有效映射作有界
快照；它不改动官方内核驱动。板端执行 `./run-lenet-trace.sh` 后生成的
`viptrace-lenet-*.tar.gz` 是下一步 prepared-resource 导入器的输入。
`analyze-viptrace.py` 会将压缩包解码为确定性的 JSON manifest，包括任务属性、
VIP 地址、快照哈希以及 submit→wait 之间发生变化的内存。ABI 已按官方
`vpmdNBG_SIZE_OVER_4G=0` 校正为 32 位 `vip_size_t`。

首轮真实跟踪 `viptrace-lenet-20260828-120540.tar.gz` 已验证控制面：任务
`0x80040000`、1 个 subtask、命令 mem-id `352321547`、命令实际长度 1088、
priority 0、timeout 20000 ms、need-schedule 1；共分配 5 个 VIP buffer，地址为
`0x01005000`、`0x02010000`、`0x01008000`、`0x0100b000`、`0x0100e000`。
第一版快照因这些 DMA VMA 带 `VM_IO/PFNMAP`，被 `process_vm_readv()` 以
`EFAULT` 拒绝，文件为空。v2 跟踪器增加 `write(2)`/`copy_from_user` 回退，需再
采集一次取得实际命令、权重与张量内容。
官方 Bullseye 镜像使用 glibc 2.31 且自带 gcc；为避免新版 WSL 交叉产物的
glibc 2.34 版本依赖，镜像已预置 `/root/a733-viplite-golden`，板端通过
`./run-lenet.sh` 执行。首次板测发现镜像虽有 gcc 却缺少 `libc6-dev`/标准头文件；
因此已补充基于 Bullseye glibc 2.31 sysroot 构建、仅要求 GLIBC_2.17 的兼容程序
`a733-viplite-golden.bullseye`（SHA-256
`5e4dfe18ad3d00d0dd8a26b3a3527ac14496c9d5455f4cf416d23fe64c02ac8f`）。
更新后的脚本优先使用它，不再要求板端编译器或开发头文件。模型和输入 SHA-256
已从镜像内再次核验。

板端 native 首次运行暴露出 ELF 间接依赖搜索问题：主程序的
`$ORIGIN/lib` DT_RUNPATH 可以找到 `libNBGlinker.so`，却不会继续传递给它所需的
`libVIPhal.so`。新版 `run-lenet.sh` 已显式导出 bundle 的 `lib/` 到
`LD_LIBRARY_PATH`，并保留真实运行退出码；推理失败时不会再误执行输出校验。

### 官方 VIPLite Lenet 黄金推理通过

真实 4 GiB Cubie A7Z 已完成官方 Debian/VIPLite 黄金推理。VIPLite 版本
`00020003`，驱动 `2.0.3.2-AW-2024-08-30`，硬件 CID `1000003b`；网络为
`lenet_uint8_NCHW`、5 层、1 输入和 1 输出。输入是 28x28x1x1 UINT8、
TF-asymmetric（scale 1、zero 0），784 字节且 raw CRC32 为 `e892cab0`；输出为
10 个无量化 FP16，合计 20 字节。

复测运行 wall time 0.623 ms，硬件 profile 为 164 us / 64,285 cycles。输出
raw CRC32 为 `d50f5d79`，SHA-256 为
`4dccf1b724880df5d14916e9cfa1c5465332e5a76ddef6deb9a325c7526fd585`。
20 字节原始输出为
`00 3c 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00`；按小端
FP16 解码为 `[1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0]`，
argmax 类别为 0。它现在是 openvela 逐元素验收 fixture。至此官方闭源
prepare/run 黄金基准门槛关闭；随后提取已授权运行产生的 prepared resources，
实现导入/执行层，并经现有 v41 PD-MMU/IRQ 路径完成真实 Lenet 推理。
