# logs/ — AI Coding 日志目录

2026-11-04补充：新增2个真实主线程的原始快照导出/1340条事件，调用官方归集器原始解析、脱敏和写出函数生成，官方验证通过。内部guardian记录不进入最终PR，不拼接分叉或修改事件。方法及缺失范围见 [导入说明](../docs/AGENT_LOGS_20261104.md)。PR工作区保留官方107个初赛会话，已有主线程seq从原清单20539后续接，不重写历史事件。

存放你在开发中与 AI 工具的对话日志，和作品代码一并提交。

本仓库不保留模板示例日志。提交前必须用组委会归集工具导出开发过程中真实的
Codex 会话，不能手写、拼接或修改 JSONL。

## 目录结构

```text
logs/
└── <github_login>/              # 你的 GitHub 用户名，一人一目录
    ├── manifest.json            # 会话清单
    └── <date>/                  # 日期 YYYY-MM-DD
        └── <tool>__<sid>.jsonl  # 一个会话一个文件（工具名与 session id 用 __ 连接）
```

- `<tool>`：`claude-code` / `opencode` / `codex` / `kiro`
- 每个 `.jsonl` 每行一个事件，由组委会提供的日志归集工具导出，**只提交 JSONL 本身**。

安装（把占位符替换为真实 GitHub login）：

```bash
bash ../.claude/skills/contest-log-collector/onboarding/install.sh \
  --team-id contest2026_274_Dogking \
  --github-login <github-login>

bash ../.claude/skills/contest-log-collector/onboarding/verify-setup.sh
```

提交前验证：

```bash
python3 ../.claude/skills/contest-log-collector/tools/validate-log.py logs/
```

导出与提交的完整步骤、字段定义见[《AI Coding 日志归集与提交手册》](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/ai_coding_log_guide.md)。
