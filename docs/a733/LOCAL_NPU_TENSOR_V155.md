# v155 多张量执行接口：软件门槛完成，真实 prepared 资产待采集

后续更新：v156已导出四模型A7TV，并通过Linux实际固定/动态/完整KWS链路。
见 [v156当前状态](LOCAL_NPU_PREPARED_V156.md)；下文为v155历史快照，不删除。
openvela真实语音模型执行仍待换卡测试。

[本次验证、恢复点及最终测试包哈希](../evidence/local-voice-v153/tensor-v155-software.md)。

后续r2回传及r3快照读取修复见
[更新证据](../evidence/local-voice-v153/layout-v155-r2-failure-r3-fix.md)。
不要继续使用r2包。后续已通过SSH完成r3四模型布局采集，见
[实机及本地复核证据](../evidence/local-voice-v153/layout-v155-r3-ssh-passed.md)。
真实A7TV导出及openvela执行仍待完成。

本版不替换通过固件，不把Linux通过写成openvela硬件通过。
默认关闭 `CONFIG_A733_NPU_TENSOR_IO`；旧A7PM/YOLO接口及8输出结构布局保留。
实验接口单独使用 `A733_TENSOR_ABI=1`，两条新增ioctl，不在启动阶段执行模型。

## 已完成

- 独立A7TV prepared容器（不是NBG），128字节头、32字节区域描述、64字节张量描述；
  64入/64出、128区域、区域内偏移切片、rank1..4、原生字节和显式数据格式。
  首版拒绝量化元数据非0和4-bit格式；已验证语音模型的native I/O都是无量化FP32。
- 使用原24MiB prepared池和16个MMU窗口，不改映射条目格式、DRAM/启动布局、时钟或IRQ。
  按页拒绝区域重叠、溢出、内部init页/命令尾页冲突及文件载荷越界；所有载荷CRC运行前校验。
- 同fd自有缓冲区句柄/权限/完整长度、禁止I/O物理重叠和输出互相覆盖。支持39路KWS状态。
- 运行时复制请求元数据，不重复信任可变长度/路径；无大数组占用NuttX任务栈。
- 输入/输出字节在RAM，不通过SD文件交换。全部硬件、guard、尾页和输出校验成功后才
  提交应用输出；失败时 `committed=0`，原输出字节保留。
- 硬件超时/异常或guard失败后隔离运行；不自动重试/复位。恢复失败时不撤销仍可能
  在途DMA的映射、不复用prepared池，要求冷重启。不是已认证的硬件恢复实现。
- 可选模式下序列化运行时的ioctl/write/close，避免free/close回收在用缓冲区；
  CANCEL仍可异步到达。独立 `/dev/a733-npu` 手动诊断不在此门内，禁止并行使用。
- 公共 `system/a733_tensor_client.h`，BSP实现位于vendor目录，产品应用不含私有NuttX头。
  明确检查预期native padded长度，部分分配失败回收fd，ioctl失败对象停止使用，
  不默默切换CPU/云服务。应用对象单所有者且调用期间禁止异步任务取消。
- 修正构建暂存清单：原脚本未显式暂存已有NPU运行时/头，过去依赖SDK已存在副本；
  现在连同新接口一起暂存/恢复，不让新功能依赖手工修改SDK。

## 测试与边界

```sh
bash tools/test-a733-tensor.sh
bash tools/test-vip-tensor-trace.sh
python3 tools/test-voice-layout.py
bash tools/check-openvela-first.sh
python3 tools/compile-a733-tensor.py EXISTING_NUTTX_BUILD NEW_OBJECT_OUTPUT
```

24组C测试（清单/状态8、实际驱动适配器mock10、公共客户端mock6）通过ASan/UBSan。
39路数据连续8轮、末路状态、所有权/权限、非法长度和重叠、损坏载荷、输出坏CRC、
分配失败、超时且恢复失败的隔离/保留映射、不提交失败输出均覆盖。
另按真实39路native长度验证303684字节输入、299524字节输出及不等长张量0，
不是只用等长小数组证明能回灌。该测试模拟字节变换，不是模型推理或板测。
CRC与实际NuttX `crc32part`一致：初值0/final xor0，已知答案
`123456789 -> 2dfd2d88`，不能误用默认zlib CRC。

真实NuttX GCC13.4编译库分别编译旧分支/新分支/解析器/客户端/KWS核心，
严格警告错误开启；输出到新目录，不修改SDK。编译对象不是链接镜像/实机验收。
LD_PRELOAD追踪器自进程内存/不可读地址/大小上限/独占文件测试通过；Python绑定
测试覆盖逐字节切片、歧义/损坏/截断、四次自动调度及错误停止不重试。

**尚无运行于openvela的真实语音A7TV资产**：Linux原始输出不含厂商prepare命令和
buffer地址，不能凭它猜测/构造执行流。因此保留已验证镜像，不生成名为全链路通过的镜像。
当前query只验清单/切片；RUN才检查所有文件载荷。用户须使用可信且外部哈希核验的包；
CRC不是数字签名，接口不是恶意NPU命令的安全沙箱。权重初版每次重载，尚非实时性能版。

## KWS状态回灌必须显式保留标量策略

应用以Linux核对的39入39出长度契约打开encoder。每块：

1. 输入0为29×80 FP32 fbank；步进16个特征帧，输出0为4×320 FP32。
2. 输出1..37原始字节回灌下一块输入1..37，绝不截短或重解释类型。
3. 输入38按已通过策略写**FP32特征偏移**0/16/32，而不是默认复制输出38的+8标量。
4. decoder上下文必须是该模型native FP32的两个token ID，joiner输出263音素。
5. 新的有界C搜索处理唤醒；不是ASR中文字表。ASR的36路/INT64状态必须另行验证。

公共传输层不私自更改任何标量、量化、采样率和文本/音素语义。

## 首次采布局，一次四模型

[Linux操作说明](../../tools/npu-voice/LINUX_VOICE_LAYOUT.md)。仍用当前Linux卡，
四个固定输入各运行一次，输出必须与旧golden完全相同，捕获实际命令/内存布局。
不重跑24场景/唤醒矩阵，不装工具链，不联网、不采麦、不自动重启。
回传后需进一步核对完整任务链、页布局、全部tensor绑定和动态输入，才能导出A7TV。

ASR NBG及转换工具、A7Z Matcha执行、实时麦克风→唤醒/ASR→官方Agent→语音闭环仍未完成。
全部厂商/模型/派生prepared文件保持本地私有，不加入Git，不删除开发过程记录。
