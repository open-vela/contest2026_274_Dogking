# v193 中间构建记录

- r1：agent 为补正 Kconfig 对显式 ID 命令的说明，主动向本次 Ninja 发送 SIGINT。
  CMake 生成被中断，wrapper-status=1，不是完成的构建或镜像。
- r2：沿用缓存时缺少 build.ninja，Ninja 退出，wrapper-status=1。
- 两次 285 个 SDK 路径均逐字节恢复；日志和恢复 JSON 原样保留。
- 随后增加显式、限定源目录/生成器的 cache 再生成，不删除对象、不改通过版镜像。
  后续最终结果另记，不把这两个尝试改写成成功。

host-r1..r4 是工具演进中的历史检查，后续修改工具使其摘要不再代表最终工作树；
最终源码对应 host-r5，保留先前记录供溯源。
