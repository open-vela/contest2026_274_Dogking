# v229 官方PR准备记录

2026-11-04。本轮请求是准备官方PR，**未推送、未创建/合并PR**。本地v229固件、封存镜像和原恢复标签不变；新增优化Skill、真实导出日志和提交材料，不继续dslocal。

## 目标与基线

- 目标：`open-vela/contest2026_274_Dogking`，base `dev-ai-contest-2026`。
- 已只读fetch核对的官方HEAD：`6b80489ab3915ce706a58d21ddd856b9b0ea3d56`。
- v229封存源码/文档提交：`5d900a2a4fde4e6453e07f2fd76c15a103459884`；固件冻结：`ba0d722a965ed59a8aa8bef49dbf64fc1f8731e5`。
- 独立准备分支：`pr/v229-final-20261104`；工作区为本目录同级 `contest2026_274_Dogking-pr-v229-20261104/`。

官方分支含本开发线没有的275个文件。直接从原开发分支提PR将误删初赛Rockchip代码、Skill、报告与日志，不采用这种方式。准备工作区从官方HEAD建立，再叠加本次v229源码/文档/日志；官方 `.github/` 与许可证保持原样，已有路径不删除，正式manifest保留官方指向及旧平台映射。

## 本次新增材料

- `skills/embedded-ai-pipeline-optimization/`：ASR/TTS/LLM/YOLO优化，按需引用4份细节资料；Skill Creator格式验证通过。工作区根 `skills/` 也保留同字节副本。
- `logs/hxy299/`：2个真实主线程快照导出/1340事件，官方验证通过；guardian审查记录不入最终PR，边界见 [日志说明](AGENT_LOGS_20261104.md)。PR保留官方107个初赛会话，合并清单而不改事件。
- v229源码、功能状态、历史板测与最终制品指针一并保留；新真实云多轮与整机3–5秒首音不冒充已验收。

## 发布前尚需处理

1. 每个真实线程只选一个完整原始快照，仍有14个未合并续接/分叉原件等兼容性问题，需官方确认后重新导出；不能说“全部原始日志已合规入仓”。先本地准备或草稿，正式发布前确认接受范围。
2. 核对报名账号的fork/push权限与CLA；当前未登录GitHub执行PR写操作，也没有自动创建fork。
3. PR合并前manifest对外指向官方base分支是正式配置；未合并时如需验证新代码，使用PR分支检出，不误以为官方base已更新。
4. 2GiB镜像、GGUF/厂商模型不进入源码Git，镜像附件/下载链接尚需实际上传后填写，不伪造Release地址。

PR标题、正文、差异和验证结果放在独立准备工作区的 `docs/submission-v229/`。再次推送前重新fetch并检查base是否变化；不要强推、直接向保护分支push或使用旧个人share manifest。

规则依据：[官方代码提交指南](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/code_submission_guide.md)、[官方日志归集手册](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/ai_coding_log_guide.md)。指南中的初赛截止与决赛实际权限以组委会通知为准，不自行推定仍有写权限。
