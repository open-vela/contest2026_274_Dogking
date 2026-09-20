# A733 SMP / Cortex-A76 性能适配计划

## 冻结基线

- 恢复提交：4f41737；
- 可烧写镜像：openvela-a733-cubie-a7z-sd-aipet-forward1-v81-candidate.img；
- 镜像 SHA-256：00ddafa52f0e039fa3be60a6f0363325d30bb89c11f7978d386cc6ecf4c6517c；
- 内核 SHA-256：037177bc975c1b636e855811e9baf7c2c5d3559a6f3499ad6d21d4e7de893181；
- forward1 时间：473.6016 秒；
- 单 token argmax：6233，解码为 beautiful。

## 分阶段原则

1. 只启用 SMP/PSCI/GICR，验证 8 个 idle task、IPI、调度和长时间稳定性；
2. 增加 CPU MIDR/MPIDR/频率诊断，确认 6 个 A55 与 2 个 A76 的逻辑映射；
3. 给 aipetllm 增加 A76 affinity，但先保持单线程，量化大核单核收益；
4. 将矩阵输出行分块为工作任务，先使用两个 A76，再扩展到异构 8 核；
5. 引入模型页/权重缓存或高速存储，消除每 token 重读约 1 GB SD 数据；
6. 建立逐层持久 KV cache；
7. 启用并校验 ARM NEON 量化 dot-product；
8. 每一阶段都回归逐层 CRC、logits CRC、argmax 和系统外设稳定性。

SMP 首镜像不得覆盖 v81；任何启动失败均可直接烧回冻结镜像恢复。
