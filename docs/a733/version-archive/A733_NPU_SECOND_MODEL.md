# A733 NPU 第二模型与 YOLO 路线

## 当前可信基线

- openvela `/dev/npu0` ABI v3 的分配、缓存维护、提交、同步、取消和复位接口已通过。
- 官方 Debian VIPLite 生成的 LeNet trace 已在 openvela 裸机环境执行成功。
- LeNet 原始输出 CRC32 为 `d50f5d79`，20 字节输出与 Linux 黄金结果完全一致。
- A7PM prepared-resource 导入路径已经证明可脱离 Linux、glibc 和闭源运行库执行真实 NPU 命令。

这证明 VIP2 的时钟、复位、MMU、缓存、命令队列、中断及同步主链路成立。下一项必须是不同网络，而不是继续重复 LeNet。

## 选定的第二模型

使用 Radxa 官方 Allwinner Model Zoo 中为 A733 编译的：

`examples/yolov5/model/yolov5s_rt_uint8_a733.nb`

官方模型目标 CID 为 `0x1000003b`，输入是 `1 x 3 x 640 x 640` uint8，原始输入恰好 `1228800` 字节。模型有三个 FP32 检测输出，因此它同时验证：

1. 非 LeNet 网络能否执行；
2. 大模型资源和大输入的映射；
3. 多输出张量的保存与校验；
4. YOLOv5 后处理前的 NPU 原始输出是否稳定。

官方模型包地址：

`https://dl.radxa.com/cubie/allwinner-model-zoo.tar.gz`

将下载完成的文件保存为：

`A733-A7Z-ALL-Files/allwinner-model-zoo.tar.gz`

然后在 PowerShell 执行：

```powershell
./A733-A7Z-ALL-Files/prepare-a733-yolov5-second-model.ps1
```

脚本会校验 tar/gzip、只提取 A733 YOLOv5 模型、生成不含用户数据的确定性零输入，并把二者放进 Debian 黄金工具 bundle。

## Debian 一次性黄金采集

把更新后的 `a733-viplite-golden/bundle` 复制到官方 Debian，运行：

```sh
chmod +x run-model.sh run-model-trace.sh
VIPTRACE_MAX_BUFFER=67108864 VIPTRACE_MAX_TOTAL=268435456 \
  ./run-model-trace.sh yolov5s \
  ./yolov5s_rt_uint8_a733.nb ./yolov5-zero.dat ./yolov5-golden
```

必须保留：

- 完整终端日志；
- `yolov5-golden.output*.bin`；
- `yolov5-golden.sha256`；
- `viptrace-yolov5s-*.tar.gz`。

压缩包和所有输出复制回 `A733-A7Z-ALL-Files`。密码、Wi-Fi 配置和其他个人数据不应放入 bundle 或 trace。

## openvela 导入前必须处理的结构差异

当前 LeNet A7PM v1 只描述一个输出，而 YOLOv5 有三个输出；不能假装只验第一个输出。导入器的下一版需要：

- 描述多个输出 region、每个输出的有效长度和黄金 CRC32；
- 对每个输出分别填充、失效缓存、校验 changed bytes 和 CRC32；
- 根据实际 trace 的 VIP 地址创建所需的 16 MiB MTLB 窗口；
- 把当前 1 MiB runtime pool 和 2 MiB low-32 arena 扩到 trace 实测需求；
- 保持所有物理地址低于 4 GiB，并继续执行 guard 检查；
- 失败时恢复 NPU 状态，不能破坏 LeNet 回归测试。

在拿到 YOLO trace 之前不应盲目硬编码内存大小或 VIP 地址。trace 清单给出真实 allocation、VIP 地址和生命周期后，再按证据扩容。

## 验收标准

第二模型阶段只有同时满足以下条件才算完成：

1. 官方 Debian 对零输入推理成功，并产出三个输出；
2. openvela 执行同一 prepared package 成功；
3. 三个输出的大小与 CRC32 全部和 Debian 一致；
4. NPU 中断完成、无超时、MMU 无错误、guard 完整；
5. 重复执行至少 20 次输出一致；
6. LeNet、NPU selftest 和 apitest 仍全部通过。

完成后再加入 uint8/FP32 YOLO 解码、置信度筛选、NMS、摄像头/图片输入和性能统计。模型执行成功与检测精度验收是两个阶段，不能混为一谈。
