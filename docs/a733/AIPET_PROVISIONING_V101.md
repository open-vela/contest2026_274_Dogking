# v101：在线桌宠首次初始化与中文输入保护

## 本轮错误记录

v100 实机已验证 Wi-Fi 和 Agent 自启动。第一条中文请求成功，第二条请求返回 HTTP 400：`messages[1].content: invalid unicode code point`。TLS 握手和 HTTP 收包均成功，因此不是自启动失败，也不是本次连接池复用失败。`reply failed -5` 是桥接层对云端错误的统一映射。

能确定请求中含无效 Unicode，但现有日志没有记录原始输入字节，不能确定产生位置。已检查 NSH readline：中文退格原先只删除一个字节，可留下不完整 UTF-8。本轮补丁改为删除完整 UTF-8 字节序列，并在 pet 消息发送前严格校验 UTF-8（拒绝截断、过长编码、代理区、超出 Unicode 范围）。不会静默替换用户文字；无效输入在板端返回 EILSEQ，不再发送该请求。若仍出现编码错误，需要继续检查终端编码和输入编辑过程，不能仅以此宣称所有中文输入问题已根治。

## 首次使用（串口或已登录的 SSH）

确认 Wi-Fi 已连接。无需再通过 FTP 上传含 API-key 的 config.json。

```sh
aipet init
```

按提示输入：

1. 提供商：回车或 `1`/`deepseek`；`2`/`mimo`。
2. 模型名称：回车选默认，也可直接输入自定义名称。DeepSeek 默认 `deepseek-flash`；MiMo 默认沿用当前本地官方源码的 `MiMo-v2-Flash`。服务商以后更名时可重新初始化，不把该默认视为永久有效的云端承诺。
3. API-key：隐藏输入，不进入命令参数和 NSH 命令历史。不需要再次把 key 发给开发者。

模型名最长 63 字节、key 最长 127 字节，均要求非空可打印 ASCII，不允许空格/换行。保存失败会报告错误码，不打印 key。

初始化保存后开启 Agent 自启动，并启动后台就绪等待器。等待网络、配置、CA 和合理系统时间；这些条件满足后启动官方 `packages/ai_agent`。初始化不校验账户余额、模型可用性或真实 API-key 正确性，首次请求才会得到服务端结果。

```sh
aipet status
aipet ask "请只回答：启动测试成功"
aipet ask "给我讲个短故事"
aipet ask "请总结刚才的故事"
```

`ready` 表示本地引擎已就绪，不代表已经建立云端 TLS。不会发送收费的预热对话。短时间内再次请求通常能复用连接；服务器关闭的旧连接会在发送之前重新建立。

## 后续开机 / 更换模型

配置留在数据分区，每次正常重启不用重新初始化。Wi-Fi 的已保存连接仍由 Wi-Fi 服务负责。整卡重新烧录会覆盖数据分区，不等同于普通重启，应先备份。

若当前 Agent 已启动，`aipet init` 拒绝在线重配，避免配置和活跃引擎不同步。安全重配步骤：

```sh
service agent off
reboot
```

重新连接串口或 SSH，再执行 `aipet init`。初始化成功会重新开启自启动。`aipet setup` 是同一入口的别名。原有明文 JSON 仍可读取以兼容旧测试卡，但它不会自动变成密文；需按重配流程迁移。

## 凭证保存与边界

使用官方工作区中的 mbedTLS AES-256-GCM，随机 32 字节板端密钥、每次随机 12 字节 nonce、16 字节认证 tag，并绑定版本化 AAD。配置中的 `api_key` 保存 `a7g1:` 密文记录；运行时仅在内存解密后交给官方 LLM proxy。官方 config-store 对 api_key 的后续写入也经过加密 hook。

文件位置：

- `/data/ai_agent/config/config.json`：提供商 host/path/port、模型名称、加密 key；保留其他既有配置字段。初始化使用临时文件、fsync、rename 保存。
- `/data/ai_agent/device.key`：本板随机加密密钥，不在镜像中预置。丢失或损坏不会静默重建并假装原密文可用。
- `/data/ai_agent/ca.pem`：TLS 信任根证书；镜像包含公共 CA，不包含私人 API-key。
- `/data/system/agent.auto`：服务自启动设置（以实际 service 配置实现路径为准）。

重要：FAT 上的 0600 不是可靠的 Linux 权限隔离；密钥与密文都在 TF 卡，因此这不是抵抗完整 TF 卡读取、root/SSH 特权用户的硬件安全存储。它防止单独读取/导出 config.json 时直接看到明文，**不能承诺防物理盗取**。恢复已初始化配置应同时保存 device.key，不要把二者提交 Git 或公开发送。

默认 FTP 根目录仍为 `/data/models`，凭证不放入该目录。使用串口配置时串口链路本身不加密，应确保设备物理可信；SSH 配置受到现有 SSH 连接加密保护。

## 源码与验证

所有新增代码保存在团队仓库，不永久改写官方源码：

- `board/a733-cubie-a7z/openvela-overlay/apps/system/aipet/agent_setup.c`：交互初始化、隐藏输入、取消/恢复终端、原子保存。
- `agent_secret.c/.h`：随机密钥、GCM 加解密和显式内存清除。
- `agent_startup.c`：识别加密配置，后台启动条件。
- `agent_bridge.c`：消息 UTF-8 校验。
- `patches/ai-agent-provisioning.patch`：官方 config-store 的最小加密 hook 与构建集成。
- `patches/readline-utf8-backspace.patch`：中文退格保护。
- `tools/build-a733-wapi.sh`：临时应用、备份并在退出时恢复上述官方文件。

主机验证已通过：Agent 桥接 ASAN/UBSAN；中文合法输入和非法 UTF-8 拒绝；mbedTLS 加密往返、随机 nonce、防篡改、密钥丢失、旧配置兼容；PTY 中 DeepSeek/MiMo 默认/自定义模型、key 不回显、加密 JSON；Ctrl+C 字节/信号取消后终端恢复；HTTP chunk framing 回归。

首次构建暴露补丁上下文过弱，wipe 行被插入函数外，编译失败。已加严上下文，保留失败日志 `../a733-online-agent-v101-build.log`；修正后主体构建通过。最终包含取消处理的镜像需以最终构建日志和实机测试为准。

实机验收：分别测试两种账户；连续三次中文请求；编辑/退格中文后发送；初次配置中 Ctrl+C 后重新初始化；重启后无需配置自动 ready；旧卡重配到加密配置；错误 key、错误模型和掉网时错误可诊断。主机测试不代替这些实机结果。

## 最终构建与镜像

最终构建 exit=0：`../a733-online-agent-v101-final-build.log`。串口超长输入排空的小修正在该源文件编译前同步至临时 staging，已由最终构建编译；主机 PTY 超长 key 拒绝回归通过。

- 候选镜像：`../openvela-a733-cubie-a7z-sd-online-agent-v101-candidate.img`，2147483648 字节。
- 镜像 SHA256：`7de5735b30594384b29c2cef946b941f890367e2eed53d73616681b1394402b6`。
- 内核/ELF/System.map：`../a733-online-agent-v101-kernel/`。
- 内核 SHA256：`cf83a8709cffd6cedde66a40521d55d8f20d57f06d29c69429ef8c32e2124343`。
- 打包日志：`../a733-online-agent-v101-image.log`；GPT、ext4、嵌入内核和 CA 读回校验通过，Windows 独立哈希一致。
- 源码恢复包：`../a733-online-agent-v101-final.bundle`，不含模型/镜像/私人凭证。

构建退出后临时 aipet 目录移除；与 v100 恢复目录比对的 41 个既有官方基线文件无哈希差异。新增 patch 对应的 config_store/readline 已恢复无本轮 hook，构建脚本将它们加入备份恢复列表。官方仓库的 .git 元数据在本机无法被 Git 正常解析，未修复或改动这些元数据；此次恢复检查使用文件内容与基线哈希，不冒称 git diff 验证。

烧录会覆盖 TF 卡数据，请先备份模型、Wi-Fi/SSH/FTP 配置及必要的 Agent 私人配置。候选镜像不预置你的 API-key，烧录后先联网，再运行 `aipet init`。仍未宣称通过 v101 实机验收。
