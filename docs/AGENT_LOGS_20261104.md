# 10—11月 agent 日志归集与边界

2026-11-04，作者指定原件目录：工作区 `aiagent-log-10~11月/`。原始164个文件共1,222,519,607字节，58个JSONL、106个Zstandard压缩文件，均保留原处，不加入源码Git。

## 已完成的可核验导入

- 最终新增 **2个真实主开发线程的单一快照导出，1340条事件，937402字节JSONL**；manifest记录源文件SHA256、大小、事件数和部分采集说明。
- 最初97个格式合规导出经source字段核对，属于内部guardian审查，已全部从最终候选撤回并保留私有审计，不能作为主开发对话或有效工时。该中间材料分支未推送；正式PR用不包含该中间历史的独立官方基线。
- 官方归集工具来源：[`open-vela/.claude`](https://github.com/open-vela/.claude)，冻结提交 `ac36068f8571b2cced14c5f23f6cebb47026d1c6`。
- 使用其未修改的 `snapshot_core.py` 中 `expand_codex_rollout_line`、`redact_value`、`append_events`、`read_manifest/write_manifest`；解析器SHA256 `72b48d69b5fd52f1986030bf83f58c5c8f9beaa0784e7801786cc04f1811a87d`。
- 自定义包装器仅处理作者显式指定的目录、Zstandard解压/只读校验、来源隔离及批量调度；不伪装成默认hook，不伪造`.repo`闸门或修改系统CODEX_HOME，不在导出后编辑JSONL。
- 官方默认脱敏在生成时执行；敏感凭据额外启发式扫描未发现命中。不是完整人工隐私审计，发布前仍须审阅。
- 官方 `validate-log.py`：最终2文件/1340事件，`ALL OK`；原件未改变，导出副本SHA256一致。

## 没有冒充完整归集的部分

| 排除原因 | 原始文件数 | 处理 |
| --- | ---: | --- |
| 内部guardian审查 | 136 | 不纳入主开发日志或最终PR |
| 未选中的续接/分叉片段 | 14 | 原件保留，不拼接或造新ID，待官方确认完整归集方式 |
| 格式或内部session身份不一致 | 3 | 整文件隔离，未跳过坏行后声称通过 |
| 明确属于另一个项目 | 9 | 不导出、不提交 |

16份续接涉及 `019fa154-7ab3-7f31-b3cc-df6b6a829376`（13份）与 `01a12aec-87a2-7053-92e4-231be2265be5`（3份），有ordinal复用/分叉。每线程只选官方解析器支持文本事件最多的一个完整原始文件，其他14份不合并：主线程为10月16日开始的原始快照1318事件，另一线程为10月11日原始快照22事件。**不是10—11月全部开发历史完整归集**。

原官方主线程清单已有20539事件，新片段由官方writer从seq=20539继续。独立材料仓manifest只统计本批1318条；PR工作区合并清单后累计统计旧+新事件，旧JSONL字节不变。

官方Codex解析器只输出其支持的user/assistant文本，未输出的tool/reasoning/image负载不由包装器补写。manifest标为partial/degraded并记录原因，不能对外宣称完整工具链捕获。

## 复现与私有审计

仓库内工具：`tools/inspect-codex-log-sources.py`、`tools/import-selected-codex-logs.py`。依赖：Python3、zstandard；官方验证需要jsonschema，Skill格式校验需要PyYAML。使用前阅读工具说明，输出目标必须是新目录。

本机审计目录（不入Git）：工作区 `archives/v229-log-import-20261104/`，含全部原件清单/哈希、导出前副本、逐会话映射及排除理由。官方工具本机副本为 `archives/contest-log-tools-20261104/`，未安装全局hook。

正式提交规则参考：[日志归集手册](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/ai_coding_log_guide.md)。不手改事件，不把人工整理文档冒充日志；必要时整会话撤回。歧义/格式兼容性应由官方工具修复或组委会确认后重新导出。

## 官方PR集成

开发分支与官方 `6b80489ab3915ce706a58d21ddd856b9b0ea3d56` 分叉；直接PR会误删275个已有文件。因此独立PR准备工作区从官方分支开始，保留原Rockchip代码、既有Skills、初赛报告和107个日志会话，再叠加v229与本次材料。合并manifest只合并会话清单，不改历史JSONL。正式推送/PR未执行；日志完整性尚有上述待处理项。
