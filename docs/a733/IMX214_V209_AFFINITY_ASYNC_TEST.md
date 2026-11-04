# v209：有界核心迁移确认与三阶段并行候选

2026-10-29。保留 v204、v206、v207、v208-r2 镜像、源码标签和恢复包。
本版尚未板测，不保证 30FPS；不能把主机/QEMU 耗时称作 A733 性能。

## 已有实测与问题定位

[v208-r2 原始记录](../evidence/camera-v208-board-20261029/README.md)：
双槽管线 231帧/231推理，稳态7.733FPS，错误/清理/fallback均0，
但请求CPU6/7后实际仍CPU1/A55，affinity-error=19、CRC=portable。
故上版没有证明 A76 或硬件 CRC 生效。

当前 SDK 的 sched_setaffinity 修改 TCB 掩码后，以同优先级切换请求
调用 nxsched_switch_running；后者扫描可运行队列寻找合格替代任务，
没有替代任务时，不能据 syscall 返回断言当前指令已迁移。
上版立即读寄存器、失配后恢复掩码，尚未阻塞让出执行。
本版在应用线程内 usleep(1000)，使唤醒调度按新掩码选择目标CPU，
不改内核调度策略、全局优先级、PLL/DVFS、CPU编号或中断路由。
这是一条有源码依据的修复；具体板上迁移与收益仍须下面实测。

确认协议：读取旧掩码→设置单核→读回掩码→MPIDR/MIDR/MPIDR一致快照；
阻塞让出后，连续两次目标CPU/正确part的快照才通过。
最多251次、250ms软等待预算（调度抢占不保证硬实时墙钟上限）；
计时器不前进也有次数上限。停止/API失败/错核/错part
均不冒认成功，恢复并读回原掩码；恢复失败按故障退出。
只有已确认A76才读B域时钟，只读估算不是物理频率测量。
CRC仍须单核掩码、实际MPIDR、ISA能力和640项逐字节等价自检，
不支持则保持软件路径。kernel/apps两份CRC头保持字节一致。

## 三阶段所有权

保持 live-stream（串行预处理/NPU/显示）和 live-pipe（两阶段）作对照。
新增独立 live-async / live-async-verify，仍只有两个RGB槽：

FREE → WRITING（CPU7采集/预处理）→ READY → READING（CPU6 NPU）
→ DRAW_READY → DRAWING（CPU5唯一LVGL/SPI线程）→ FREE。

RAW DMA仍是原有单个4MiB永久守卫缓冲；应用复制RAW后再预取下一帧。
CPU5显示线程自己 begin/present/end，其他线程不调用LVGL。
每槽快照包含同帧RGB、seq/inputCRC、检测框、六路输出CRC和耗时，
不保存会被下一次ioctl覆盖的 run 指针。
全RGB CRC、旧框清除、六路输出及每次硬件后的权重检查均保留。
显示完成、统计/日志结束才释放槽；不覆盖、不运行中跳帧、不靠降采样掩盖FPS。
正常到时排空已提交显示结果；停止/错误可丢弃未完成槽并单独报告，
不得把提交数当已显示帧数。join后才释放资源；不确定退出则隔离到重启。

async 的 warm-fps 和 frames/inference 均按实际成功SPI刷新统计。
npu-stage/display-overlap-us 是包含CPU准备/校验的NPU调用阶段与显示调用
的交叠，不是纯NPU硬件与SPI总线交叠。两槽仍有回压，无法保证三阶段始终忙。
successful-frames-wall 含排队延迟，重叠工作不可相加当单会话墙钟时间。

## 上板顺序

烧写本版候选SD镜像，不改UFS；正常引导脚本沿用SWOUT1受控开启及读回。
启动时记录内核CRC/镜像SHA，确认不是UFS里的旧U-Boot。
若有摄像头/DMA失败或cleanup非0，保存日志后重启，不继续重复试错。

1. 先不启动摄像头，执行核心/CRC检查：

    camera live-cpu

CPU6/7应分别 actual=6/7、MIDR part=d0b、confirmations=2、
readback=00000040/00000080、affinity-error=0、restore-error=0，
crc=arm-crc32-selftest-passed。两行 crc-exec 应 reference-equal=1、
result=0、hardware-dispatch-qualified=1；最后cpu-probe result=0 restore=0。
software-us/dispatch-us只是该CRC测试，不是整体FPS。
如果失败，停止性能验收，发完整日志；不要屏蔽检查或强制CRC指令。

2. 屏幕、同输入冷热六路及检测一致性：

    display raw8
    camera live-verify 5 1
    camera live-status

等待 active=0、error=0、cleanup=0，再运行下一条。
verify必须cold/warm equal-legacy=1、六路CRC相同，不能只看有框。

3. 先测保留的两阶段对照，再测独立显示模式：

    camera live-pipe 30 1
    camera live-status
    camera live-async-verify 5 1
    camera live-status
    camera live-async 30 1
    camera live-status

每个后台任务等待完成再启动下一项；每帧YOLO使用detect-every=1。
CPU角色应 infer-display=6、prepare=7、display=5（A55）；显示固定A55
是为给另两条CPU密集路径保留A76，不改变其他系统任务。
normal结束submitted=rendered=frames、error=cleanup=0，
warm-fps提升才可说本板提速。观察边框与画面同帧、零检测清框、
静态/移动画面无花屏。只看相机采集FPS或提交数不算推理FPS。

4. 短测通过再长测及合作停止：

    camera live-async 300 1
    camera live-status
    camera live-stop
    camera live-status
    cat /dev/a733-camera-control
    free

另一个SSH/串口执行stop；等待active=0/cleanup=0，再发起第二次30秒会话。
stop允许discarded-at-stop>0、run-ioctls多于已显示推理；必须如实保留。
保存完整串口输出和画面，不能只截取最快一帧。

## 复现与恢复

构建入口 tools/build-a733-camera-v209.sh，传NEW报告目录和
out/ort-static-v185-r2，可显式 --incremental 复用生成缓存；不删除旧封存。
SDK临时覆盖由构建器快照/恢复，新增迁移协议头也在清单。
tools/test-a733-perf-v209.py 包含22组回归：原20组、迁移协议、
真实pthread三阶段模拟（含慢显示/交叠、故障、停止、两次会话、六路快照）。
tools/check-a733-perf-v209.py 再检查ARM对象、完整ELF/守卫布局、旧ABI和
NPU执行核/映射代码不变。最终 tools/audit-a733-isa-v208.py
检查真实ELF的CRC32X/B及局部O2，不能只看CMake声明。
封装器只生成NEW普通镜像，回读、e2fsck、GPT与非root跨度校验；
不会访问实盘、清UFS或迁移模型。

## 最终候选封存

22组回归、8个原生ARM对象/栈、完整链接/24构造器、旧NPU ABI、
执行/映射实现不变、实际ELF的CRC32X/B/局部O2/内联阻塞让出与
MPIDR夹读、297项SDK恢复、镜像回读与Windows独立SHA均通过。
[102项封存证据](../evidence/camera-v209-final/README.md)不冒认实机通过。

- 镜像：仓库上一层 openvela-a733-cubie-a7z-sd-v209-camera-affinity-async-candidate.img
- 大小：2147483648字节
- SHA256：758208e54be76ff4fa985d94530dc50389790f88c9885200707b8332597057b4
- 内核：7156328字节，CRC32 d67240da
- 源码：efe3f2efdc0fdac557098292ee4fc60810baa03e
- 源码标签：a733-v209-affinity-async-source-20261029
- 封存标签：a733-v209-affinity-async-sealed-20261029
- 最终恢复标签/包：a733-v209-affinity-async-recovery-20261029
- 二进制/配置/模式/脚本：out/v209-release-20261029（10项）
- 完整报告：out/camera-check-v209-r2；构建r1、封装r1均为成功记录。
- RAW池：0x45912000/4MiB；runtime-end 0x45de5000，无重叠。

额外ISA审计首轮误要求保留cpu_wait函数名，被编译器内联后无法匹配；
已核对真实live_cpu_bind里的usleep和cpu_identity里的三次MRS，修正工具，
完整r2重跑通过，生产源码/内核不变。保留r1诊断，不用其提前生成的summary验收。

恢复包仅归档源码/完整Git历史；大镜像、封存二进制、QEMU依赖包、
ORT静态库是外部哈希引用，必须保留原文件或另外备份。官方SDK/BSP和工具链
仍按构建文档准备，不会被恢复脚本覆盖。实际新目录恢复结果将另行记录。
