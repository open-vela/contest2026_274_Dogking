# v165 UFS M-PHY/UIC 核心（源码阶段，不是可烧录冷启动版本）

## 本阶段实际完成

新增生产模块 `a733_ufs_link.c/.h`，位于 A733 chip overlay，采用独立构造的
状态机、回调和错误处理；并入 `CONFIG_A733_UFS_READONLY` 的 CMake 源列表与构建暂存清单。
没有复制 Linux 驱动的结构体、表格、生命周期或许可头；寄存器/属性事实来自厂商 BSP
和 UFSHCI 定义，来源及 hash 记录在本轮证据中。

这不是模拟器专用实现：测试直接编译同一个生产 `.c`，不移除函数体、不替换内部状态机。
但是**当前没有板级 caller，也没有任何 `cold-init` 的 NSH 入口**。没有把主机通过
或内部 API 存在当作 A7Z 可以启动 UFS，更没有借此把 `attach-ro` 改成复位/初始化接口。

流程仅到链路/CPort：

```text
板级前置条件 → 检查HCE/队列/中断/版本/专用配置 → HCE
  → RMMI → 释放PHY复位 → 等待SRAM → CR桥/SRAM/校准
  → PHY启用 → DME_LINK_STARTUP → device-present/VS_POWERSTATE
  → CPort配置和回读 → link_up（不代表存储可用）
```

默认校准路径50个UIC命令；完整AFE＋PLL A/B路径各92个。所有寄存器写范围限制在
HCE、UIC参数/命令、已拥有的UIC完成位和PHY配置字段；不分配DMA、不写UTRL或SCSI介质。
不改HS Gear/lanes，不发送NOP/Query/device-init，不枚举LUN，不注册新块节点。

## 不能绕过的板级接口

`a733_ufs_link_io.qualify()` 必须返回已由板级驱动确认的九项条件，缺任何一项都在
HCI访问前拒绝：rails、power-domain、clocks、exclusive owner、SoC、refclk、
PHY held in reset、calibration provenance、device-reset prepared。

这不是用户可输入的mask、DT min/max或“我确认”的字符串。当前项目没有这个真实适配器：

- S-TWI0/PMIC由系统固件使用，现有扩展I2C驱动明确不接管它；不能直接抢总线写电源。
- Linux调压器的设置范围不足以确认本板供电，Kingston具体后缀/板号/NC连接仍需核实。
- core只接受已准备好的内部26MHz refclk/19.2MHz config profile，检查实际HCI配置，
  但自己不编程共享PLL、PMIC、RTC、CCU或电源域。
- `release_phy` 是板级独占、受控操作；它不能作为任意寄存器写接口。
- core不处理Ctrl+C/任务死亡；未来caller须由稳定worker持有整段生命周期和互斥，
  不允许在途取消、释放锁或并发调用。不能把主机无锁单线程测试当NuttX并发验收。

未满足这些条件不允许连接到现有NSH/启动流程。此边界来自芯片适配技能的电源、
时钟、复位、MMIO门禁要求，不是通过增加CLI强制开关即可消除的问题。
没有调用未知SMC或凭借“其他芯片能读PMIC”推断本板接口可用。

## 超时、错误与可诊断性

- UIC ready 50ms、command completion 100ms、SRAM 10ms；均有轮询次数与单次墙钟边界。
- 整段另有10s单调时间/累计延时上限，处理32位毫秒计数回绕。
- UIC结果非零、ISR错误、MMIO/释放PHY失败、错误回读等立即停止，不执行后续写操作。
- 第一条可能有副作用的写之前设置 `attempted`；出错后 quarantine，拒绝再次尝试。
  无自动reset/retry/清隔离接口，成功也不能在原状态上重跑。
- 上电前置条件不满足时没有MMIO访问；已活跃的HCE/队列/自动休眠/中断状态拒绝接管。
- 记录phase、errno、last offset/value/attribute/UIC result、命令/轮询数和时间。
- 只有UIC完成位可W1C，错误位保留给诊断。软件超时不能救回电源关闭后卡死的总线读，
  因此必须先完成板级供电/电源域的资格检查。

## 软件验证与构建方法

```sh
python3 tools/test-a733-ufs-link.py
python3 tools/test-a733-ufs.py
python3 tools/test-ufs-linux-collector.py
python3 tools/test-project-checkpoint.py

python3 tools/compile-a733-ufs.py \
  ../../quickly-openvela/cmake_out/cubie-a7z_nsh_v164_ufs_readonly_candidate \
  out/new-ufs-link-object-check
```

主机以 `-Wall -Wextra -Werror`、ASan/UBSan 编译生产模块，三种校准分支遍历
3357个错误位置，另测577个非法正返回值，合计3934个注入点；正常顺序、缺失门禁、
HCE/队列忙、错误配置、SRAM/HCE/UIC超时、时钟冻结/回绕、错误link/CPort等另测。
31项critical-source校验在临时SDK副本中通过，并验证新源/头任意改一字节必须拒绝。
这不是在真正SDK中暂存/完整CMake构建。

ARM64以v164 compile_commands的真实参数独立编译core/旧UFS/board对象通过，
最大静态帧分别112/224/64B；保留命令JSON及项目头hash。没有修改SDK。
本轮**未做完整链接、未生成新镜像、未做启动/PHY/存储板测**。
以后有可用板级adapter再按 `UFS_STORAGE.md` 的全新目录构建流程生成候选，
不得把本源码checkpoint当作一个可测试冷启动镜像。

## 后续依赖顺序与恢复

1. 实现安全的板级供电状态读取/系统固件协作、clock/reset/refclk/校准来源，
   核实实际电气状态，接入上述core，验证冷启动和失败回收。
2. 真正NOP、Query/device-init及描述符/LUN只读枚举，再验证DMA映射和数据。
3. 只读分区/文件系统、大文件hash，然后才开放授权的scratch写入/flush/断电验证。
4. 最后吞吐/长稳、模型迁移；SD原数据保留。

本轮前checkpoint：`out/checkpoints/a733-pre-ufs-link-v165-20261021/`，
874文件/14019362B，3项外部引用。原v162/v163/v164镜像、历史开发文档全部保留。
新源码另存 `out/checkpoints/a733-ufs-link-v165-source-20261021/`，恢复仅写新目录。
证据见 `../evidence/UFS_LINK_V165_20261021.md`。后续仍按用户要求统一实机测试。
