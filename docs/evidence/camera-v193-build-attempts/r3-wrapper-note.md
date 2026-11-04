# r3 外层包装器的额外退出说明

SDK 构建子包装器报告 `wrapper-status.txt=0`，完整链接与布局检查通过，
`sdk-restored.json` 记录 285 路径逐字节恢复；生成的内核 SHA256 为
`707b84dbd46943e5c0bc0a0692f5ee2e4293ebb71b2f104d9d2b8b76af31ef4f`。

本轮 agent 在外层 v193 shell 尚运行时补充了它的“无 cache 时走 fresh”分支，
旧 shell 在子包装器结束后继续读取已改写脚本，报
`line 15: build-a733-camera-v192.sh: command not found`，进程外层退出码为 1。
**不能把外层退出码记为 0**。这不是编译/链接失败，未覆盖成功子包装器的原始状态。

最终脚本语法与 fresh/cache/regeneration 路由另用实际包装器＋模拟子命令回归，
最终内核仍须通过对象/源码/ELF/配置/镜像独立校验。今后禁止在 shell 正执行时编辑
该脚本；修改应在子包装器完成并退出后进行。失败说明保留，不改写原始日志。
