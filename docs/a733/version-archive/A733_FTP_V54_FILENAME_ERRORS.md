# A733 FTP v54：中文文件名上传失败与会话断开

## 实板证据与结论

2026-09-03：v53 实板下载成功，上传 PDF 时 WinSCP 报 `Can not open file !`。
文件名 `5.2 课程设计说明书正文格式.pdf` 是 41 个 UTF-8 字节。
旧有效配置 `CONFIG_NAME_MAX=32`；NuttX `_inode_checkpath()` 对每段路径按字节
计数，在文件系统 open 之前拒绝这个名字并返回 ENAMETOOLONG。
不要把这里的字节限制与 FAT 的 UCS2 字符数限制混淆。

`ftpd_stream()` 发送 550 后仍返回负 errno，worker 以此终止控制会话，造成
“一个文件打不开 → FTP 断开”的连锁现象。这不等同于整个服务器或 Wi-Fi 崩溃。
诊断时 Windows 到板子 21 端口曾成功建立 TCP 并收到 `220 NuttX FTP Server`。
用户报告的“先由板子 ping 电脑才可连接”仍是独立的 LAN/ARP 待查问题，v54
没有修改 Wi-Fi，不能宣称该问题已修复。

## 修复范围

- NAME_MAX=255，PATH_MAX=512，FAT_MAXFNAME=255，FTP 命令缓冲 1024。
  普通中文名称仍使用 UTF-8，FAT UCS2 的原有限制不变；不是任意长度 Unicode 支持。
- FEAT 宣告 UTF8（FAT UTF8 启用时）、SIZE 和 REST STREAM。
- 文件打开/寻址/读写/刷盘失败返回明确 550/451 和 errno，同时记录不含文件名、
  密码的错误日志。回复成功就保留控制连接，允许再次上传；控制 socket 真失败仍退出。
- 失败传输正确关闭数据连接并清理 REST 状态；保留已有断点续传策略。
- 不改动 SSH、NPU、用户凭据；不写物理 SD、不删除旧镜像。

## 宿主验证与边界

`test-a733-ftp-v54-path.py` 编译执行真实 VFS 校验函数，证明原名字被旧配置拒绝、
新配置接受，并保留超长路径拒绝行为。
`test-a733-ftp-v53.py` 是沿用的实际 FTP 库 POSIX 测试：UTF8/FEAT、中文原名、
中文长名 .filepart、空文件到 64 MiB SHA256 往返、错误路径之后同会话 NOOP/
再次上传下载、续传、并发、目录隔离、认证、退出。
这些测试不是实板 FAT/SD/USB/Wi-Fi 压力测试，必须再做板端验收。

## 板端验收

1. 更新 v54 内核/镜像后重新联网，执行 `ftpd start`；WinSCP 主机填板子 IP，
   FTP 端口 21，被动模式，UTF-8，传输类型“二进制”。FTP 不加密，仅可信局域网使用。
2. 上传原 PDF，下载回电脑，对两个文件运行 PowerShell `Get-FileHash -Algorithm SHA256`。
3. 再传短英文文件和长中文文件，确认列表显示完整，连续上传不掉线。
4. 如失败，保留完整 FTP 返回文字（含 errno）、`dmesg`、`ps`、`ftpd status`；
   WinSCP 会话日志必须去除密码。不要只截取“连接失败”。
5. 若下载/上传期间突然网络不可达，另做双向 ping 和邻居表检查；不要将文件名
   修复视为首次 LAN 可达问题已解决。

新镜像沿用 v53 的 2 GiB 布局和基线数据。重新整卡烧录会覆盖用户上传的文件和
板端密钥，应先备份；脚本只生成镜像，不执行烧录。也可按已有更新流程只替换
启动分区 `/boot/openvela/a733/Image`，保留数据分区。

构建：`build-a733-ftp-v54.sh`；打包：`package-a733-ftp-v54.sh`。
有效构建配置和最终哈希以构建目录、`v54-image-sha256.txt` 为准。

## 本次执行记录

WSL 完整 1898 步构建通过（9 分 34 秒）。三个宿主测试脚本均通过；包含
UTF-8 FEAT 声明、实际 VFS 拒绝/接受对照，以及失败 REST/STOR 后偏移清理。
内核 `cmake_out/cubie-a7z_nsh_v54_ftp_names/nuttx.bin`，1,564,504 字节，SHA256：
`f7e85afc6f56a6f9feccbfe651b47e5ea760707c465e559b92d4d094cde6bd23`。
实板上传复测待用户执行。

打包完成：`openvela-a733-cubie-a7z-sd-ftp-v54-candidate.img`，2,147,483,648 字节，
SHA256 `f0f5ba8dce357115c40e779cb7745e051c57fef00b68c8818885f7ef3be9fe8c`。
新内核与镜像内文件逐字节一致，ext4 检查通过；启动区及数据区与 v53 逐字节
一致，GPT 无错误（沿用原分区 4 尾部非 2048 扇区对齐提示，不涉及磁盘加密）。
回环挂载已清理。旧镜像未改动，物理 TF 卡未访问。
