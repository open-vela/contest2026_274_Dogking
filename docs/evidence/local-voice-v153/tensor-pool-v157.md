# v157实验I/O池修复证据

日期：2026-10-19。根因/验收步骤见 [v157说明](../../a733/NPU_TENSOR_POOL_V157.md)。
用户v156串口记录原样关键输出见 [原始记录](openvela-v156-user-fixed.txt)。
v156三模型固定CRC已实机匹配，encoder为分配失败；本轮不把主机修复通过当成新encoder板测。

## 根因重现与修复范围

原实际保留布局：总256页，自测页1、LeNet固定常量107，空闲148页/606208字节。
公共客户端将39路64字节对齐，再分配输入303744和输出299584，页取整75+74=149页。
实际分配器返回ENOMEM，且没有进入TENSOR_RUN。只有133页连续空间，因此简单合并句柄也不够。

修复只在TENSOR_IO实验模式选择2MiB池；非实验模式仍为1MiB。
32MiB DMA arena、24MiB prepared池、16句柄、旧VIP固定常量和协议ABI均不改变。
stage映射由原有宏随池尾移动；同MTLB窗口/不重叠/STLB容量及arena总预算回归通过。

新增 `test-a733-runtime-pool.py` 从实际runtime提取当前常量、数据结构、find/free/close/
alloc函数和启动保留语句，而非复制改写分配算法；包含真实公共客户端和应用39路native长度。
两个配置均编译执行，ASan/UBSan、-Wall/-Wextra/-Werror：

```text
Actual runtime allocator + real client: pool=1048576 free=606208 encoder-rounds=16 hw-mock=0 PASS
Actual runtime allocator + real client: pool=2097152 free=1654784 encoder-rounds=16 hw-mock=16 PASS
```

包括16轮成功/失败后的页和句柄回收、失败不提交、所有权、常量区逐字节不变、16句柄耗尽、
stage映射边界和原arena预算。旧模式返回ENOMEM是期望的失败重现，不是其encoder通过。
新模式的输出是mock字节，不是神经网络硬件输出。
两种池配置测试的QUERY/RUN均由桩提供，以隔离验证真实分配器；不表示关闭实验开关后
真实驱动也支持A7TV。真实关闭配置仍应拒绝扩展ioctl，原客户端ENOTTY测试保留。

`out/v157-tensor-regression-final.log`：实际分配器双配置，原plan8组/适配器mock10组/
客户端6组、npuref应用11项、导出器5项、日志检查4项及官方API边界均通过。
旧测试只用无限制calloc模拟分配，没有带上永久保留页，是v156漏检原因；现在纳入日常
`test-a733-tensor.sh`，避免再次遗漏真实池限制。
初次新主机harness漏ioctl桩头路径的编译失败日志也保留，补齐后r2与最终版通过。

最终回归日志SHA：`228c85c30fca6d8ce7ba3c18ae37974b39727edc5e061f2036bccb600fb437c8`。
实际NuttX/GCC13.4目标对象：baseline-disabled、tensors-enabled、plan、client、KWS核心
五项通过，输出在 `out/tensor-v157-target-compile/`，不是链接/板测证明。
该编译摘要SHA：`7e3ed275c3d5cf7f51ad0ed4c96b317fd9c6649bf660f2ee99d14b5912153d6d`。
用户v156记录SHA：`79fad3b298136aacc6380edea3e23572dac93ad6b8affd2de1e459e89267c3e3`。

与v156源码清单比较，除 `a733_npu_runtime.c` 外的全部板级/应用文件哈希保持一致，
核对日志 `out/v157-unchanged-board-app-check.log`。新测试和文档另存，不改已封存报告。
私有代码恢复快照 `out/a733-v157-source-20261019.tar.gz`（820656字节）SHA：
`87c43cc2e722d78b10c8982c8fad70c60f968e40c1ee7904f9b14eaac3978b72`。
源码清单 `out/v157-candidate-20261019/source-sha256.txt` SHA：
`67348defbe6f6a88fe63b136802917ee9c35482078411bc846863b65dd0090d9`。

## 构建与恢复

保留v156通过三模型的整卡、Image、ELF、config、源码快照和所有报告，不覆盖。
新目录 `quickly-openvela/cmake_out/cubie-a7z_nsh_v157_tensor_pool_candidate`：

```sh
python3 tools/audit-a733-staging.py record ../../quickly-openvela out/sdk-before-v157.json
A733_TENSOR_EXPERIMENTAL=1 \
A733_BUILD_DIR=/absolute/official/workspace/cmake_out/cubie-a7z_nsh_v157_tensor_pool_candidate \
JOBS=8 bash tools/build-a733-wapi.sh
python3 tools/audit-a733-staging.py check ../../quickly-openvela out/sdk-before-v157.json
```

完整2972目标构建/链接通过，日志 `out/build-v157-tensor-pool-candidate.log`。
148个SDK暂存路径逐字节恢复，源树恢复检查在打包前再次通过。
新候选保存在 `out/v157-candidate-20261019/`：

| 产物 | 字节 | SHA-256 |
| --- | ---: | --- |
| Image | 2651968 | c687018f0d6010d5cec578149b4e327c4ea6da82823de87aff738327408a0017 |
| nuttx.elf | 3690056 | 2f722dcd60051087da984e87647833e01ff225b9a87ab6996f7e0ce98b4d8ecc |
| build.config | 103002 | 9569a7a14a523603a91e2c1e9d28872524a3718e989a18c3c9116060f794deec |
| System.map | 432501 | 8ae4308ed1057a030b73576afff314d3abce06e3769b47585d3323936cf743da |

实验flag=y，8核/I2S/LVGL/BLE配置保留；配置哈希与v156一致，修改在条件代码内部。
ARM64魔数、页/段/栈及BL31 carveout检查通过，运行时预留36941824字节，与v156一致。
最终ELF实际符号核对：`g_rt_pages` 从0x100变为0x200字节；`g_npu_arena` 两版均为
0x2000000字节且地址同为0x404ac000，证明没有扩大整个低地址DMA arena。
符号/布局/SDK恢复及源码完整重新校验日志均保存在候选目录。

整卡：仓库相邻 `openvela-a733-cubie-a7z-sd-v157-npu-pool-candidate.img`，
2147483648字节，SHA：
`be08b22602208546a2248a7c362b73f59f98377bdaaa5e95fac1c68f53f45ec7`。
同名 `.img.sha256` 已生成，Windows独立Get-FileHash一致。
以v156镜像副本为底图，仅替换分区3的Image，不新增模型或凭据；原底图仍为
`d4d4717c7f107bb81384e02249a0cbb8401d95136dc338d1178b13f3dac056e3`。
修改前/后及最终抽取ext4通过，内嵌Image字节/哈希匹配；GPT No problems found。
沿用底图分区4不结束在2048-sector边界的警告，不重排分区。
`[0,348127232)` 和 `[469762048,2147483648)` 完整逐字节比较一致，未改非内核分区。

没有对开发板执行SSH、烧录、复位、自动重试或网络修改，没有commit/push。
当前HEAD仍为36077a09e3d9ce4cc2aadd08d28b238ff578bfa9，工作树含旧及本轮未提交修改，
因此以恢复快照及源码SHA清单标识本次实际代码，不把HEAD冒充构建版本。

**目前没有v157实机执行记录。** 修复只验证了资源分配原因/软件回归/候选构建；
encoder计算正确性、多块状态、动态输入与完整本地语音链路仍须分级验收。
