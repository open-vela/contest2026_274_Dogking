# v155-r3：SSH四模型布局实机采集通过

日期：2026-10-19。用户明确授权SSH连接并测试，实际执行于A7Z官方Linux，
不是WSL/CPU模拟，也不是openvela实机推理。

## 执行环境与范围

- SSH目标：192.168.1.235，板端 `Linux 5.15.147-21-a733 aarch64`，
  device-tree model输出 `sun60iw2`，存在字符设备 `/dev/vipcore`。
- 首次SSH使用本次工作区独立known_hosts、accept-new建立记录，后续均严格校验。
  凭据未写入项目代码、文档或测试脚本。
- 运行前磁盘剩余25GiB，温度约31.3–36.5°C；运行后约31.3–36.6°C。
- `fuser`未安装；用sudo只读遍历 `/proc/[0-9]*/fd/*`，确认NPU owners为空后执行。
- 独立新目录：`/home/radxa/voice-layout-v155-r3-ssh.Q9pkIEJy/`。
  原测试包/报告未覆盖，未安装依赖、改网络、刷写、复位或重启。
- 上传包哈希在板端再次核对通过：
  `11f7b994d648f02d5c258628071daefb7646134d502a0ea40c52ba7aa4dcd95a`。
- 执行 `sudo bash test-linux-voice-layout.sh --run --npu-idle-confirmed`，
  encoder、decoder、joiner、vocoder各一次；退出码0。

## 回传与独立核验

报告：`voice-report-FsNTsIdC.tar.gz`，21117877 bytes。
板端与下载到本地的SHA-256一致：
`84a5f7c48be7ef471e37fc5b6cab183fedbd9abc56edc0e1660469cdfd333bc3`。

本地私有归档位于仓库相邻
`Linux测试-Npu测试文件/voice-report-FsNTsIdC.tar.gz` 及解压同名目录。
真实命令、地址、厂商数据和快照不提交Git。

逐模型重新核对：native元数据、全部输入日志CRC、全部输出长度/SHA-256/CRC、
有限值/prefill变化计数、所有trace文件manifest大小和SHA-256、
两阶段快照及逐字节切片绑定，并与板端生成的bindings JSON再次比较，均通过。

| 模型 | 输入/输出数 | 输入/输出字节 | 每阶段快照区域 | trace文件数 | 回退读取记录数 |
| --- | --- | --- | --- | --- | --- |
| encoder | 39/39 | 303684/299524 | 84 | 757 | 168 |
| decoder | 1/1 | 8/1280 | 6 | 61 | 12 |
| joiner | 2/1 | 2560/1052 | 7 | 69 | 14 |
| vocoder | 1/1 | 64000/204800 | 7 | 67 | 14 |

合计42个完整原始输出哈希匹配旧golden、85处native输入/输出绑定、954个trace文件。
各模型均 `failed=0 snapshots=2`，没有read-error或空/截断快照。
208条 `write-copy-from-user` 回退记录证明本次实机确实走到r3修复路径，
不是只在普通堆内存上通过。r2空快照问题在此次运行中已修复。

板端结束状态：`NATIVE_GOLDENS_AND_LAYOUT_CAPTURED (exit=0)`。

## 性能与验收边界

本轮日志run wall时间encoder 80665µs、decoder 5335µs、joiner 3783µs、
vocoder 107459µs，包含LD_PRELOAD全量快照读出/写盘干扰，
不能替代此前无追踪暖态性能，更不是端到端语音延迟。

本轮仅四个固定参考输入，不新增动态输入/实时麦克风验收。
`prepared_package_exported=false`、`openvela_hardware_executed=false`、
`end_to_end_accepted=false` 保持不变。
下一门槛是完整任务链/内存引用审查及可信A7TV资产导出，再做openvela真实执行、
变化输入和多块状态回灌。ASR资产、A7Z Matcha及Agent语音闭环仍未完成。

## 文件权限处理

sudo创建的results目录为root私有，普通SFTP最初无法读取其内归档。
仅使用 `install -m 0600 -o radxa -g radxa` 将该报告复制到本次独立目录顶层后下载；
没有放宽目录/设备权限，没有递归chown，也没有删除板端或本地文件。
