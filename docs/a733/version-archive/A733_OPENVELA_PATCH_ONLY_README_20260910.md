# A733 openvela 仅修改/新增文件覆盖包

此包只包含在官方 `quickly-openvela` 环境上为 A733/Cubie A7Z 新建或修改的源码文件，不包含完整 openvela、不包含 `cmake_out`、预编译工具链、官方 BSP、镜像、模型或阶段文档。

压缩包内部从 `quickly-openvela/` 开始保留原始相对路径。将它放到新环境的项目父目录后解压，即可把这些文件恢复到相同位置。覆盖前必须备份目标树；新环境应来自与当前环境相同的 openvela 基线，否则不能盲目覆盖通用 NuttX/libssh/FTP 文件。

## 包含范围

- `vendor/allwinnertech/chips/a733/`：全部 A733 芯片、Wi-Fi、NPU、SDMMC、TRNG、USB camera 等新增驱动。
- `vendor/allwinnertech/boards/a733/cubie-a7z/`：全部 Cubie A7Z 板级文件与 defconfig。
- `apps/system/a733wifi/`：Wi-Fi 管理命令。
- `apps/system/a733services/`：自启动/服务管理。
- `apps/system/a733ftpd/`：FTP 服务包装器。
- libssh server/client/scp/init 的 NuttX 适配。
- curl 同步 DNS/TLS 配置。
- FTP 协议栈大文件、UTF-8 路径与错误恢复改动。
- NTP、iperf、NSH 网络命令改动。
- NSH 主入口和 NuttX ARM64 启动/异常/syscall/任务启动改动。
- FAT directory/path 兼容改动。
- A733 VIP2 公开 ABI、NuttX 顶层 CMake 和 `uname` 平台标识改动。

## 恢复方式

假设新环境父目录中已经有同基线的 `quickly-openvela`：

```powershell
tar -xzf A733_OPENVELA_PATCH_ONLY_20260910.tar.gz
```

这会覆盖同名文件并创建 A733 新目录。建议先解压到临时目录，逐项比较后再复制。如果接手 AI 继续使用本机现有工作区，则无需执行覆盖；这个包用于迁移、备份和校验。

包内 `PATCH_FILE_INDEX.csv` 为每个文件给出路径、大小、SHA-256 和类型；`MANIFEST_SHA256.txt` 用于完整性校验。

## 重要限制

当前 `.repo`/Git worktree 元数据在 Windows/WSL 挂载后已经损坏：各项目 `.git` 文件为空，bare repo HEAD/object 也不能正常解析，因此无法通过一次可靠的 `repo diff` 自动证明相对官方 commit 的精确差异。本包采用以下证据的并集生成：

1. v33.1 到 v65 阶段恢复归档里的 source snapshot；
2. 当前 A733 chip/board/custom-app 完整目录；
3. 2026-08-17 后被修改的官方树文件；
4. A733 符号和调用引用反向扫描；
5. v55 服务恢复点中的跨目录文件。

因此本包倾向于“宁可多包含一个相关修改文件，也不能漏掉一个构建所需文件”。它仍远小于完整 openvela，而且不含生成文件。

## 不在包内、但运行/构建仍需要

- 官方完整 openvela 同版本基线及其 `prebuilts`。
- `A733-A7Z-ALL-Files` 中的构建/打包脚本和技术交接文档。
- Wi-Fi `/data/aic` 固件、HTTPS CA、SSH/FTP 配置等镜像数据。
- NPU prepared models/traces。
- 可烧写 SD 镜像。

不要把 Wi-Fi、SSH、FTP 密码或 API key 放进本补丁包。本包已扫描用户此前日志中出现过的已知明文凭据。
