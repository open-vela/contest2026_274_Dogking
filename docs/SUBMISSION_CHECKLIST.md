> 2026-11-04正式PR准备：以下9月20日内容为历史；当前官方manifest已恢复，使用 docs/submission-v229/ 下的新提交说明与检查结果。尚未推送、未创建PR。

# 官方提交规则核对清单

核对日期：2026-09-20。依据 openvela 官方 `dev-ai-contest-2026` 分支的《参赛
代码提交指南》和《AI Coding 日志归集与提交手册》。本文件区分“个人协作远端”
与“组委会正式提交”，避免把临时共享配置误提交为最终 manifest。

## 当前个人协作阶段

- 当前分支：`a733-cubie-a7z-share`。
- 当前推送目标：个人公开仓库 `hxy299/hxy299-a733-share-hub` 的 `share` 远端。
- manifest 和 README 的 `repo init` 命令暂时指向个人仓库，便于队友同步开发。
- 不向组委会 `origin` 推送；不创建正式比赛 PR。

## 正式提交前必须恢复

1. 将 manifest 项目恢复为组委会仓库 `open-vela/contest2026_274_Dogking`，分支
   `dev-ai-contest-2026`，并同步修改 README 的 `repo init` 示例。
2. 从个人分支创建面向组委会专属仓库的 PR；遵守保护分支，不强推覆盖历史。
3. 使用报名账号签署 CLA；若 `cla/signature` 未刷新，在 PR 评论 `/check-cla`。
4. 公共 `nuttx/apps/packages/vendor` 修改按功能拆分到对应公共仓 fork，并向
   `dev-ai-contest-2026` 提交 PR；比赛仓保留 overlay/patch 是当前可复现开发形态，
   不能替代获奖后的上游合入。

## 仓库内容检查

- [x] README 包含作品简介、赛道、目录、构建/运行、AI Coding 和限制。
- [x] `contest2026_274_Dogking.xml` 可映射板级和应用源码。
- [x] 源码只在比赛仓维护；构建脚本临时 patch 后会恢复公共工作区。
- [x] 镜像生成、校验和烧录边界有可重复文档。
- [x] 未完成的 USB、I2S、动作、视觉链路明确标记，不以编译代替实机通过。
- [x] `.img/.bin/.elf/.a7pm/.nb/.gguf`、本地归档和恢复 bundle 不进入 Git。
- [x] Wi-Fi、SSH/FTP、API Key、设备密钥和私人配置不进入 Git。
- [ ] 正式候选镜像完成最终实机回归并上传 GitHub Release。
- [ ] Release 地址、大小、内核与镜像 SHA-256 更新到 `docs/ARTIFACTS.md`。

## AI Coding 日志

官方要求每位成员安装归集器，日志按本人 GitHub login 放入
`logs/<github_login>/`，提交真实导出的 `manifest.json` 与 JSONL，不得手工拼接或
修改事件。目前仓库只有 `logs/README.md`，所以个人代码备份可以继续，但在正式
比赛提交前此项仍未完成。

```bash
bash ../.claude/skills/contest-log-collector/onboarding/install.sh \
  --team-id contest2026_274_Dogking \
  --github-login hxy299

bash ../.claude/skills/contest-log-collector/onboarding/verify-setup.sh
python3 ../.claude/skills/contest-log-collector/tools/validate-log.py logs/
```

多人协作时，每位成员使用自己的 GitHub login 目录，并在合并前同步最新日志。

## 推送前机械检查

```bash
git status --short
git diff --check
bash -n tools/*.sh
git ls-files | grep -E '\.(img|gguf|a7pm|nb|key|pem)$' && exit 1 || true
git grep -nEi 'api[_-]?key[[:space:]]*[:=]|BEGIN (RSA|OPENSSH|PRIVATE)'
git log --oneline --decorate -10
```

个人远端推送：

```bash
git push share a733-cubie-a7z-share
```

该命令只用于协作备份，不代表已向组委会提交作品。
