# Markdown → DOCX 导出工具

技术报告需要以 `.pdf` / `.docx` 提交，而本仓库用 Markdown 维护（便于 diff 与审查）。
本目录提供把 `docs/SUBMISSION_TECHNICAL_REPORT.md` 导出为 DOCX 的最小工具。

## 用法

```bash
mkdir -p /tmp/md2docx && cd /tmp/md2docx
npm init -y
npm install markdown-docx

node /path/to/tools/docx/md2docx.mjs \
  docs/SUBMISSION_TECHNICAL_REPORT.md \
  docs/SUBMISSION_TECHNICAL_REPORT.docx
```

导出的 DOCX 已验证包含全部章节标题、25 个表格（177 行）与 827 个段落。

## 说明

- 只依赖 `markdown-docx`（MIT），不引入 Pandoc / LibreOffice。
- DOCX 是**派生产物**：修改内容请改 Markdown 后重新导出，不要直接编辑 DOCX。
- 提交前建议再导出一次 PDF（Word「另存为 PDF」或任意 Markdown→PDF 工具），
  因为模板接受 `.pdf` 或 `.docx`，PDF 在评委端排版更可控。
