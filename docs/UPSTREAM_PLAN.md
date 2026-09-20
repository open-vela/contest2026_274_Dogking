# 公共仓库上游计划

比赛指南要求将对公共 openvela 仓库（例如 `nuttx` 和 `nuttx-apps`）的修改
提交到它们的 `dev-ai-contest-2026` 分支供组织方审核。比赛清单通过显式的
`linkfile` 保持当前测试集成可复现，并公开每个公共代码树替换项，而不是隐藏
在一个巨型补丁中。

## 比赛项目维护的开发板代码系列

以下新增目录构成 A733/Cubie A7Z 开发板提交内容，最终应移动到相应的全志厂商
仓库：

- `vendor/allwinnertech/chips/a733`
- `vendor/allwinnertech/boards/a733/cubie-a7z`
- `apps/system/a733wifi`
- `apps/system/a733services`
- `apps/system/a733ftpd`
- `apps/include/system/a733_services.h`

## nuttx-apps PR 系列

按子系统拆分，并保证每个提交都可独立审阅：

1. A733 Wi-Fi/网络集成以及 NSH 命令修改；
2. FTP 大文件、UTF-8 路径、被动模式和会话恢复修改；
3. NTP/DNS 计时与重试修改；
4. libssh 的 NuttX 初始化、服务器 PTY/NSH、清理和客户端/SCP 修复；
5. 目标平台所需的 curl NuttX 配置。

发送前，应将每个文件重新基于当前官方分支，并删除上游已不再需要的临时
兼容代码。

## NuttX PR 系列

1. ARM64 早期启动/致命错误/syscall/task-start 修改，并附架构测试；
2. FAT 长文件名/路径修复，并附文件系统测试；
3. 公共 A733 VIP2 设备 ABI 头文件；
4. 初始化/构建/uname 修改，尽量缩小到通用行为。

每个公共 PR 都必须包含真实的 A733 失败证据、聚焦的技术说明、适用时在其他
受支持目标上的测试，以及项目要求的 CLA。完成合并后，再将比赛集成更新为
对应的上游提交。

当前工作仍是本地半成品，尚未向组委会或公共仓库提交。
