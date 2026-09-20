# v89 两次启动失败记录

## 状态

v89 worker service 镜像未通过板端启动验证，不能作为稳定版本。恢复基线仍为 `a733-llm-cache-v88-basic-verified`，不得覆盖 v88 镜像或把 v89 标记为验证通过。

用户原始日志：`C:/Users/Lenovo/.codex/attachments/2d112b19-e373-472b-9a48-1ab219d23dbf/pasted-text.txt`。

## 已确认的证据

- 两次 U-Boot 均读取 1713600 字节 Image，随后进入 ARM64 内核；不是停在烧录软件或 U-Boot 找不到内核阶段。
- 两次均有多核启动输出，未进入可用的 NSH 控制台，出现 `F0`。
- `F0` 位于仓库 overlay 的 `nuttx/arch/arm64/src/common/arm64_fatal.c`，对应 `arm64_fatal_handler()` 入口。该标记只证明进入异常处理；本日志没有 ESR/ELR/FAR，不能据此区分数据访问、指令、对齐或其他异常，也不能断言所有八核已经稳定运行。
- `X0`/`X1` 分别在 `apps/system/nsh/nsh_main.c` 的 `nsh_initialize()` 前后。串口多核输出交错，不能仅凭交错文本确定故障 CPU 或精确执行顺序。
- v88 到 v89 的源码改动仅涉及 aipetllm_modelcheck.c 和平台 a733_boot.c。新增平台启动接口调用 kthread_create，但只由首次 LLM 并行矩阵调用触发，没有开机主动启动线程池的逻辑。因此当前不能归因于已运行的 LLM 线程池死锁。
- 原先的 MMC Device 2 not found、DTB 提示也出现在已经成功的版本中，不足以解释本次启动回归。

## 待执行的定位步骤

1. 在仓库 overlay 异常入口添加不依赖调度器、堆、正常 syslog 的低层串口诊断，输出 CPU MPIDR、CurrentEL、ESR、ELR、FAR；必须在 this_task() 等调用之前读取和打印。
2. 独立生成诊断镜像，保留 v88/v89 原文件和源码恢复点；以对应 ELF/System.map 映射故障 ELR。
3. 根据异常类型定位，再对新增平台接口与线程池静态状态做逐项差分验证。不要在未知原因时叠加 KV cache、生成或 NEON 优化。
4. 修复后重新验证八核 cpucheck、计时、Wi-Fi、首次加载和 cache-hit CRC、重复调用及 unload 内存释放。

当前没有定位到具体故障指令，没有修改运行代码，也没有宣称修复成功。
