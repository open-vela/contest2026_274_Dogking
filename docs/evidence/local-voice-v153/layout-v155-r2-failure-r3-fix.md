# v155-r2 Linux回传：encoder成功、快照失败；r3修复待板测

日期：2026-10-19。用户回传：`voice-report-NjChd1DF`。原目录不修改。

## 独立核验

- `encoder.log`：进程退出0，`reference-status=0`；提交/等待ioctl均成功。
- 元数据匹配原encoder契约；39路输入的长度及日志CRC全部匹配固定输入。
- 39路输出共299524 bytes，长度、SHA-256、CRC、有限值与输出prefill变化计数
  全部匹配已通过的 `voice-report-fXyxidmo` 固定encoder参考。
- trace包含84次allocation记录、295次return记录，最终
  `failed=1 snapshots=2 dumped_bytes=13824`；13824为ioctl载荷，不是模型快照。
- pre-submit/post-wait在首个VIP分配的快照均为0 bytes；没有可用snapshot绑定。
- decoder/joiner/vocoder未运行。布局验收仍然失败，不称为四模型采集通过。

## 原因与修复

r2追踪器只使用 `process_vm_readv` 复制CPU地址。该接口无法pin某些VIP
VM_IO/PFNMAP映射，而参考程序自身仍能正常访问和推理。旧版通用VIP追踪器
已针对此类映射使用 `write` 的 `copy_from_user` 路径，本次新追踪器遗漏了该处理。
r2没有记录读取errno，因此不能声称已从该回传直接读到EFAULT数值；
两个空文件、追踪器源码路径及既有VIP映射处理支持上述定位。

r3：

1. 优先保留 `process_vm_readv`，失败后使用有界 `write(fd, mapped_source, n)`。
   由内核安全复制，不在用户态直接解引用未知指针，不安装signal handler。
2. 支持partial write/EINTR；计入真实写出字节，大小/总量上限不放宽。
3. 记录 `read-fallback` 及失败时的 `read-error`（具体文件、地址、errno）。
4. 先检查trace final/read-error，再核验文件大小；空文件诊断包含文件名及实际字节数。
5. 任意不可读、缺失、截断、歧义、输出错误仍停止，不重试、不伪造绑定。

## 离线验证

- `bash tools/test-vip-tensor-trace.sh`：ASan/UBSan通过；原快照/独占文件/大小上限，
  另强制 `process_vm_readv` 返回EFAULT，对有效RAM测试真实kernel write回退，
  单块64 bytes及跨块131072 bytes逐字节/尾部核验；PROT_NONE和地址1拒绝。
  这是映射失败模拟，不是声称主机具有A733的真实VM_IO映射。
- `python3 tools/test-voice-layout.py`：4个测试方法通过，增加read-error及空快照
  在第一模型立即停止、不重试、不生成成功摘要的子场景。
- r3 runner/追踪库AArch64交叉编译，`-Wall -Wextra -Werror`通过；追踪库
  GLIBC依赖仅2.17。新压缩包重新解压后资产/输入完整性校验通过，未调用NPU。
- 没有更改openvela运行时、原通过镜像、系统库/驱动、时钟、网络、固件或原回传。

## r3交付

私有包：`out/a733-linux-voice-layout-v155-r3.tar.gz`，7913615 bytes。
SHA-256：`11f7b994d648f02d5c258628071daefb7646134d502a0ea40c52ba7aa4dcd95a`。
r2包及所有历史记录保留。r3仍待Linux实机布局采集，不等于A7TV导出或openvela验收。

```sh
KIT=a733-linux-voice-layout-v155-r3
tar -xzf "$KIT.tar.gz"
cd "$KIT"
sudo bash test-linux-voice-layout.sh --run --npu-idle-confirmed
```

退出其他NPU程序后再运行，保持供电/散热和至少2GiB空闲。无需重刷系统。
回传 `REPORT:` 对应压缩包；任一失败保留日志，不自动复位/循环重试。
