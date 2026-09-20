# v90：v89 启动回归诊断镜像

此版本仅用于定位异常，不是已修复版本，也没有增加 LLM 生成功能。

## 保存点

- 故障基线标签：`a733-v89-boot-failed-before-diagnostic`。
- 诊断源码提交：`4eb2951`。
- 上层目录备份：`a733-v89-before-diagnostic.bundle`、`a733-v90-diagnostic-source.bundle`。
- 可运行恢复版本仍为 v88。

## 改动

在仓库 overlay 的 `nuttx/arch/arm64/src/common/arm64_fatal.c` 中，进入异常时立即读取 MPIDR、CurrentEL、ESR_EL1、ELR_EL1、FAR_EL1。直接使用低层串口打印，放在 `this_task()` 和正常异常处理之前；不使用 malloc、格式化库或 UART 锁。

每行带故障 MPIDR；启动时多个 CPU 同时打印仍可能使行交错，不应为获得整齐日志在异常入口引入可能死锁的锁。诊断针对目前 TF-A 进入 EL1 的启动路径，字段明确标为 EL1。

`tools/build-a733-wapi.sh` 新增此文件的备份、临时覆盖与退出恢复，使修改不永久留在官方工作区。

## 板端测试

使用独立的 `openvela-a733-cubie-a7z-sd-llm-bootdiag-v90-candidate.img`。烧录整卡前备份卡上的模型、SSH 密钥、网络配置及个人数据；镜像不会自动保留当前实体 TF 卡内容。

从上电开始保存完整串口日志。若再次失败，重点提供 `F0` 和随后所有 `FAULT cpu=...` 行；若没有 FAULT 行，也提供完整日志，不要认定问题消失。若成功进入 NSH，先执行 `aipetllm cpucheck`、`time "sleep 5"` 并测试 Wi-Fi，不能仅凭一次启动认定 v89 回归已修复。

用本次归档的 ELF/System.map 查故障 ELR，禁止使用以后重新构建的 ELF 映射旧地址。

## 未完成

故障类型和具体指令仍需板端寄存器输出确认；持久线程池跨调用、unload 内存释放、异常取消均需后续验证。多 token KV cache、prefill、生成和 NEON 优化继续暂停。

## 构建和镜像校验结果

WSL 构建退出 0，应用 openvela 边界检查通过。构建退出后确认官方 arm64_fatal.c 不再包含新增诊断函数。

- 镜像：2147483648 字节，SHA256 `269e06b1253edb0e4463b367e39eb1ecd6ba403f5d669c99f6d0fa73f46ae214`。
- 内核：1713600 字节，SHA256 `2e3e77fa152b3ac4d3dacdac93865819bb3ad93f91e1ede4b4d9dbbe205fec84`。
- 镜像内核回读一致，ext4 只读检查通过，GPT 检查未发现问题；分区 4 尾部非 2048 扇区对齐提示继承自基线镜像。
- 本次对应 ELF、Image、System.map 和 build/package/verify 日志保存在仓库 `archives/llm-v90/`，未上传远端。

以上是离线构建和镜像完整性校验，不是板端启动验证。
