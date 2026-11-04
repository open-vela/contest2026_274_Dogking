# PR发布步骤（未执行）

先解决 `PR_BODY.md` 的隐私、日志完整性/组委会接受范围和CLA待办。本轮只是准备；以下命令供作者明确决定发布后使用，不表示已操作。

## 1. 核对工作区与官方基线

在独立PR工作区运行：

```bash
git branch --show-current
git status --short
git fetch --no-tags origin dev-ai-contest-2026
git log -1 origin/dev-ai-contest-2026
git diff --name-status --diff-filter=D origin/dev-ai-contest-2026 HEAD
```

分支应为 `pr/v229-final-20261104`。按作者最新策略，最后一条仅应列出 `UPLOAD_POLICY_R2.md` 中2份已停用的A733补丁；日志/Skills/文档及其他平台不得删除。若官方base已变化，先审阅并集成，不强推或盲目覆盖。

## 2. 确认报名账号的官方fork

按官方流程将 `open-vela/contest2026_274_Dogking` fork到报名账号，先确认真实fork URL。不要使用不属于此官方仓的个人share-hub替代。当前未创建fork、未验证写权限，也没有凭据写入仓库。

把实际fork配置为新remote后，仅推送这一分支：

```bash
git remote add submission-fork <已核对的官方fork的HTTPS地址>
git push -u submission-fork pr/v229-final-20261104
```

不要运行 `git push --all`、`git push --mirror` 或 `--force`，不要直接推官方保护分支。

## 3. 创建草稿PR

GitHub选择base `open-vela/contest2026_274_Dogking:dev-ai-contest-2026`，head选择真实fork的 `pr/v229-final-20261104`，正文使用 `PR_BODY.md`，先标Draft。若已有同一head的PR，更新它而不是重复创建。

若已安装且登录GitHub CLI，可在确认上述内容后使用：

```bash
gh pr create --repo open-vela/contest2026_274_Dogking \
  --base dev-ai-contest-2026 \
  --head <报名账号>:pr/v229-final-20261104 \
  --draft \
  --title "[决赛][Dogking] Vela桌宠 v229：A733端侧语音、视觉与系统适配" \
  --body-file docs/submission-v229/PR_BODY.md
```

CLA与决赛权限以官方实际检查为准。镜像单独上传Release或指定附件，再填写真实URL；不要把2GiB镜像或私有板上/data配置加入源码Git。未完成的日志/板测清单如实保留，不勾选为已完成。
