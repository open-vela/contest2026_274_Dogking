# 初次 WSL 检查：基线检查拒绝，未开始测试

2026-10-29。首次收集器使用 Git 文本 diff 判断 `board/` 是否变化，报：

```text
ValueError: Firmware sources differ from preserved v200 baseline
```

检查发现仅原有 `apps/system/aipet/third_party/onnxruntime/provenance.json` 的 CRLF 被
WSL Git 与 Windows 原生 Git 不同解释。没有修改该文件；实际 SHA256
`65a41930e873052b3ca1f1ddd08bb3919b44ef924fe73f75cca2b8c7c6f17b66`
与 v200 恢复清单相同。

后续收集器不放宽字节标准：校验固定 SHA 的 v200 恢复清单、文件集合与全部287个文件大小/SHA。
r2 在该更严格字节验证下通过；最终重新验证结果参见主文档及后续 final 证据。
本记录是诊断整理，不是原始串口日志。没有连接开发板。
