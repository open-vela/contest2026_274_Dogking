# 官方PR本地准备入口

> 当前同步和发布指示以 [UPLOAD_POLICY_R2.md](UPLOAD_POLICY_R2.md) 为准：A733全量替换，其他平台保留，日志/Skill/文档增量；作者已要求实际通过PR合入。下方“未发布”等是此前准备阶段记录。

2026-11-04。

工作区：`contest2026_274_Dogking-pr-v229-20261104`；分支：`pr/v229-final-20261104`。
目标：`open-vela/contest2026_274_Dogking` 的 `dev-ai-contest-2026`；已核对base为 `6b80489ab3915ce706a58d21ddd856b9b0ea3d56`。

- [PR标题/正文及合入前清单](PR_BODY.md)
- [发布操作边界与命令](PUBLISH_STEPS.md)
- [本次验证记录](CHECKS.md)与[机器可读结果](VALIDATION.json)
- [日志导入来源和缺失范围](../AGENT_LOGS_20261104.md)
- [优化Skill](../../skills/embedded-ai-pipeline-optimization/SKILL.md)

此前保留274个原开发线没有的官方文件；按新策略移除其中2份v229已不用的A733补丁，其余初赛资料及其他平台代码仍保留。日志manifest增量合并，官方 `.github/` 和LICENSE不变；A733内容和文件模式与v229一致，manifest正式指向官方仓。

官方日志验证109文件/25273事件通过；新Skill格式验证通过。内部guardian审查已排除，完整主开发历史尚有14份未拼接快照及3份兼容问题，不冒充全量。

不要推送 `submission/v229-materials-20261104` 或 `--all`：它是本机材料整理过程分支，包含已撤回的中间审查日志历史。正式准备分支从官方基线单独生成，只包含最终选定内容。本轮不创建PR、不推送、不重编固件。原 v229 release 分支、标签、镜像及原始日志保留。
