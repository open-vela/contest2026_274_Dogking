# v97：openvela LLM 应用入口

## 已验收基础

用户 v96 板端日志验证双 token 基线、中文 UTF-8 流式输出及两轮历史：第二轮回答 `Your name is Tom.`，历史保留 53/64 token。中文首预测约 7.95 秒，26 token 提示、24 token 输出合计约 17.50 秒（不含加载/分词）。未进行外部参考精度验收、长期压力测试、运行中 stop 验证。

此前 Ctrl+C 后缓存 busy 已由用户确认发生于首次前向被中断，属于未覆盖的中断清理路径；禁用 pthread cancellation 不等于 NSH 中断安全。此版命令封装不声称修复该缺陷，仍禁止强制中断 LLM。

## 正规用户命令

通过 openvela 官方工作区既有 Kconfig/CMake/builtin 应用机制注册 `llm`；其构建函数名 `nuttx_add_application` 是 openvela 沿用的应用基础设施，不代表另造一套系统。业务实现归本仓库 overlay，自研 Qwen2 模型执行使用 GGML 量化内核，不宣称官方原生 LLM 或完整 llama.cpp 移植。

```sh
llm --help
llm --version
llm check
llm run -p "你好，请简短介绍自己。" -n 24
llm reset
llm chat -p "My name is Tom." -n 8
llm chat -p "What is my name?" -n 8
llm history
llm status
llm unload
```

`run` 为独立 ChatML 单轮；`chat` 使用板端共享有限历史；`generate` 为不含 ChatML 的原始续写。支持 `-m/--model`、`-p/--prompt`、`-n/--n-predict`、`--cores 6,7`。模型默认配置路径、默认六核策略保持。默认输出 24，接受 1..64；未知/重复参数、缺失值会拒绝。

另一 SSH 会话可用 `llm stop` 请求协作停止生成；需要先确认 status active=1。诊断 forwardfast/forward2fast 尚未纳入 stop，不能声称任意命令可停。不要按 Ctrl+C；异常 busy 当前需重启恢复，不强行解锁。

## 实现与边界

- `llm_main.c` 只解析参数并调用导出的 `aipetllm_dispatch`，不重复编译模型后端，也不建立第二份缓存。
- `aipetllm` 全部原命令保留，用于工程诊断和兼容旧测试。
- `CONFIG_AIPETLLM_LLM_FRONTEND` 默认启用（内置模式）；暂不支持独立加载的 ELF/内核应用链接方式。
- 两命令共享 RAM 模型、线程池和单一全局历史。不能将多 SSH 用户视为隔离会话，隐私场景需要先 reset。
- 仍限 Qwen2、贪心采样、64 token 历史/提示；每轮重算 KV，无滑动窗口、temperature/top-p、持久会话服务。
- 下一阶段优先解决 NSH Ctrl+C 转协作停止及所有推理统一状态，再拆出稳定模型/session API、持久 KV 和交互聊天模式。不要为缩短命令名绕过资源所有权设计。

主机命令映射单元测试为 `tools/test-llm-frontend.c`；完整交叉编译和板端测试需分别验收。正式比赛 manifest 的临时共享仓库配置恢复要求不变。

## 构建与交付存档

- 源码提交 `862aed2`，构建前保存 `a733-v97-app-config-before-build.bundle`。初次增量构建沿用旧配置未注册 llm，未用于交付；显式设置板级 `CONFIG_AIPETLLM_LLM_FRONTEND=y` 后完整重新构建通过。
- `archives/llm-v97/test-frontend.log` 主机参数测试通过；`build-final.log` 为正式构建日志，`System.map` 包含 `aipetllm_dispatch`、`aipetllm_main`、`llm_main`，配置确认八核。日志、ELF 和内核均留在本地 archives，不整体加入 Git。
- 空闲栈 `0x423f6000` / `0x42406000` 页对齐，构建结束官方环境临时应用目录已恢复为不存在。
- 上层镜像 `openvela-a733-cubie-a7z-sd-llm-app-v97-candidate.img`，2147483648 字节，内核 1734128 字节。
- 内核 SHA256：`dc825352735129627ced7350bba47a4748ff8d6fac2c1bf2efc6c29ea7f118da`。
- 镜像 SHA256：`d9a143eba8d511cab26888ab980ccc66bd81823bbf3fa98a0df445ea4994ed82`。
- 打包、ext4/GPT 检查、内核回读通过；继承的分区 4 尾部非 2048 扇区对齐提示仍保留，GPT 报告 No problems found。
- 候选标签及上层恢复包：`a733-v97-llm-app-candidate` / `a733-v97-llm-app-candidate.bundle`。保留 `a733-v96-chat-history-basic-verified` 回退标签和 v96 镜像。
- v97 未通过板端入口/启动验收。整卡烧录前备份后来上传的模型、密钥及配置，新镜像不包含这些用户数据。
