# UFS v170：一次完成有界只读验收

v169 的私有缓冲区 NOP 已有一次用户实机通过。v170 在同次启动 NOP 成功后，
用一个手动批次读取身份/容量、头中尾样本及 GPT CRC/基本范围，减少分轮烧录。
本版本是软件验证后的候选，不是已经实机可用的高速模型盘。

## 不变的安全边界

继续 SD 启动，原 `/data` 不变；不写 UFS 扇区、不清空旧 Linux、不格式化、不挂载、
不注册新块设备、不自动扫描 LUN，不调用 PMIC/PHY/host reset 或超时重试。
旧 `probe`/`attach-ro` 的供电门禁仍未开放，不能绕过本阶段。
用户已授权将来清空测试 UFS，但现在没有实施该动作。

新只读核心使用第二块 77,824B、4KiB 对齐的静态 BSS arena；不复用或覆盖 v169 NOP
arena。19 页逐页执行真实 CPU 读写地址翻译，要求 VA=PA 且处于已映射低地址窗；
cache/屏障、头尾保护区及请求/PRDT/非预期数据修改都检查。保护区不是硬件 DMA 防火墙。
即使成功，也不释放这两块 arena。失败写后隔离，不强行复位或回收未知 DMA。

每次只允许 INQUIRY、READ CAPACITY(16)、READ(16)，没有任意 CDB/裸盘写入口。
新的适配器可对严格前置检查下自己产生的 completion bit0 做 W1C；不清其他中断位，
不改 IE、UTMR、HCE 或 doorbell-clear。v169 NOP 核心/写权限保持原样。
正常完成停止自有 UTRL 后复核，命令等待与时钟冻结均有独立上限。

只允许在首个数据 READ(16) 前，消费一次完整合法的初始 UNIT ATTENTION
（reset/power-on 或 medium-change 的受限 sense），然后重发原只读命令。
第二次 UA、数据读取期间 UA、其他 CHECK CONDITION 或传输失败均停止；不是无限重试。
响应中 device-exception 标记仅报告，不清除、不等同器件健康通过。

## 批次内容

1. 默认 LUN0，读取 36B INQUIRY，验证直接访问设备，报告 vendor/product/revision。
2. READ CAPACITY(16) 获取真实块长及容量，仅接受有界 512B/4096B 几何并检查溢出。
   不从 U-Boot 旧日志补值。
3. 头部、中部、尾部各读取 16KiB，并各读第二遍逐字节比较，报告三组 CRC。
4. 头部若发现 GPT，检查 protective MBR、primary header CRC 和基本范围；
   分区项表不超过 16KiB 时额外读取一次，检查表 CRC 和非空项范围，报告前四个范围。

通常 8 条 SCSI 命令；有受支持 GPT 表时 9 条；允许的一次初始 UA 最多增加 1 条。
三对样本加身份/容量为 98,372B；16KiB GPT 表时为 114,756B，不含 UA 重发。
这不是连续吞吐基准，也不检查所有分区重叠、backup GPT、全部规范条件或文件系统。
GPT 缺失/损坏/表过大单独报告 `gpt_error`，不自动修复、不伪装成已挂载，
也不把可靠的原始读取直接判作 DMA 失败。旧 Linux 文件系统仍不能直接假定可挂载。

## 上板测试（一次启动一批）

只将新镜像烧到已备份的 SD 卡，不能把 SD 镜像写到 UFS。首先确认版本：

```text
cat /dev/a733-ufs
```

必须显示 `A733 UFS v170 bounded readcheck`。否则停止，是旧镜像，不要继续。

```text
echo board > /dev/a733-ufs
echo handoff > /dev/a733-ufs
time "echo quiesce > /dev/a733-ufs"
cat /dev/a733-ufs
```

仅在本次 quiesce `completed=1 quarantine=0 last=0` 时继续：

```text
time "echo dma-nop > /dev/a733-ufs"
cat /dev/a733-ufs
```

仅在 NOP `completed=1 stopped=1 guards=1 quarantine=0 last=0` 时继续：

```text
time "echo readcheck > /dev/a733-ufs"
cat /dev/a733-ufs
```

成功应有只读批次 `completed=1 identity=1 samples=3 stopped=1 guards=1 quarantine=0`、
真实介质身份/块长/容量、三对相等 CRC。完整报告以 `readcheck-*` 段为准；
顶层旧 `media` 字段是尚未开放的旧传输路径，仍可能为空，`registered=0` 是预期。
GPT 状态另看 `gpt_error`/CRC 标记，不把 `completed=1` 理解成文件系统验收。

只在上述成功后补采：

```text
echo handoff > /dev/a733-ufs
cat /dev/a733-ufs
free
dmesg
```

请保留完整启动及全部命令输出。默认不必手选 LUN；只有明确知道目标时才使用
`echo readcheck=0 > /dev/a733-ufs`（范围 0..7）。每次启动只尝试一次；
在 NOP 前误执行 readcheck 也会消耗该次尝试。失败、隔离、超时或保护区异常时立即停止，
不要同次启动重发、切 LUN、attach、reset、格式化或迁移模型。

## 恢复与下一阶段

v169 及更旧镜像/源码封存不覆盖；新增独立 v170 镜像、源码归档、证据和可恢复 checkpoint。
候选未通过时回到已保存版本，不需要修改 UFS 介质。
只读实机通过后，再实施受限块设备/文件访问；写命令、flush、scratch 写回读、
断电一致性和最终模型盘布局仍需独立开发与验收。独立冷启动和性能优化也尚未完成。

## 构建入口（Linux/WSL，不访问开发板）

在本仓库根目录运行，SDK 路径按实际环境设置；所有开关默认关闭，因此必须显式启用。
使用新的 build/output 名称，不覆盖封存候选：

```sh
export OPENVELA_WORKSPACE=/absolute/path/to/quickly-openvela
A733_TENSOR_EXPERIMENTAL=1 \
A733_UFS_EXPERIMENTAL=1 \
A733_UFS_HANDOFF_EXPERIMENTAL=1 \
A733_UFS_QUIESCE_EXPERIMENTAL=1 \
A733_UFS_DMA_NOP_EXPERIMENTAL=1 \
A733_UFS_READCHECK_EXPERIMENTAL=1 \
A733_BUILD_DIR="$OPENVELA_WORKSPACE/cmake_out/cubie-a7z_nsh_v170_rebuild" \
JOBS=8 bash tools/build-a733-wapi.sh
```

构建器持久备份临时覆盖的 SDK 文件并在退出时恢复；若恢复失败会非零退出并保留备份，
先检查该备份和 `tools/audit-a733-staging.py` 的哈希，不带着未知 SDK 状态继续构建。
不要强杀整个进程组。完整编译成功不等于实机启动。

封装只替换已有完整 SD 镜像 p3 内的 Image，基底需要事先具备正常 Boot0/U-Boot/DTB：

```sh
candidate_kernel="$OPENVELA_WORKSPACE/cmake_out/cubie-a7z_nsh_v170_rebuild/nuttx.bin"
bash tools/package-a733-kernel-image.sh \
  ../openvela-a733-cubie-a7z-sd-v169-ufs-nop-candidate.img \
  ../openvela-a733-cubie-a7z-sd-v170-ufs-readcheck-rebuild.img \
  "$candidate_kernel"
candidate_kernel_sha=$(sha256sum "$candidate_kernel" | cut -d ' ' -f1)
bash tools/verify-a733-image.sh \
  ../openvela-a733-cubie-a7z-sd-v170-ufs-readcheck-rebuild.img "$candidate_kernel_sha"
```

这些工具只处理普通镜像文件，不烧物理 SD/UFS。重新构建得到的哈希不应冒充已封存镜像哈希；
保留实际源码、六项配置、staged record、ELF 布局、SDK 恢复及提取验证，重新建立证据链。
