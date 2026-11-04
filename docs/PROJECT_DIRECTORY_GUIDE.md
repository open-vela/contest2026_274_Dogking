# 项目目录与保留策略

整理：2026-11-04。最终提交基线为 v229，源码目录保持不变，不进行破坏性清理。先读 [最终提交说明](FINAL_V229_UPLOAD.md)。

| 目录 | 责任 | 保留策略 |
| --- | --- | --- |
| `board/a733-cubie-a7z/openvela-overlay/` | 当前 BSP、应用与公共接口 | 永久源码，不能用 SDK 中的临时文件替代 |
| `app/` | 家居和 UFS 应用 | 保留各子模块 README |
| `assets/` | 脱敏配置、人格规则 | 私人 Key、私人音频不进入公开材料 |
| `patches/` | 官方组件配套补丁 | 与构建版本一起维护 |
| `tools/` | 构建、检查、打包、恢复工具 | 保留历史版本入口，新增脚本使用清晰版本名 |
| `docs/` | 当前说明书与历史过程 | 不删除旧文档；从 README 进入当前总结 |
| `docs/a733/` | 各阶段实现、测试、发布记录 | 版本日志保留，不把旧待测结论直接复用 |
| `docs/evidence/` | 小型封存证据 | 原始日志和边界说明均保留 |
| `logs/`、`archives/` | 既有日志及归档 | 先查索引和恢复引用，再考虑清理 |
| `out/` | 忽略的本地构建／模型／镜像封存／恢复 | 不入公开 PR 不等于可删除；恢复包可能引用这些大文件 |
| `tmp/` | 临时调试材料 | 本轮未删除；后续按文件来源和引用关系核对 |

## 当前文档入口

- [v229 最终提交与上传边界](FINAL_V229_UPLOAD.md)
- [最终版本指针](FINAL_RELEASE.json)（不是稳定板测指针）
- [三张答辩亮点图及数据依据](presentations/HIGHLIGHT_IMAGES_20261104.md)

- [完整总结](PROJECT_FULL_SUMMARY.md)
- [详细项目说明书](PROJECT_MANUAL_V1_0.md)
- [初赛后变化](POST_PRELIMINARY_IMPROVEMENTS.md)
- [v218 多服务商云语音候选与测试](CLOUD_VOICE_PROVIDERS_V218.md)
- [v218 软件验证证据](evidence/cloud-voice-v218/README.md)
- [v1.0 审计记录](a733/PRODUCT_V1_0_AUDIT.md)
- [制品索引](ARTIFACTS.md)
- [首版决赛答辩记录](presentations/FINAL_DEFENSE_V1.md)

## v229 收尾归档

停止的 dslocal/v230 草稿放到仓库外 `A733-A7Z-ALL-Files/archives/dslocal-v230-stopped-20261104/`，含原文件、二进制差异补丁及恢复说明，不进入最终发布。
决赛PPT的历史制作脚本与说明书副本另存为工作区素材，不属于 v229 固件；最终上传工具从已提交 Git 快照打包，不把未跟踪的答辩草稿、私人配置、构建缓存或 Git 历史混入源码包。

## 本次整理没有做的操作

没有移动／重命名代码、SDK、模型、镜像和恢复包，没有删除历史文档，没有清空 UFS，没有改变固件配置，没有提交 PR 或推送远端。
决赛 PPT 作为工作区私有交付保存在 `Openvela/决赛答辩/`，与固件开发隔离。
