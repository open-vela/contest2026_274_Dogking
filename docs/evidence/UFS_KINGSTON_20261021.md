# UFS Kingston 器件资料及软件门禁证据（2026-10-21）

## 本次输入与研究

用户补充实物系列为 Kingston UFS128-CY14，其余丝印原样保存在
`../a733/UFS_KINGSTON_CY14.md`。官方产品表确认128GB/UFS3.1系列，未提供 Linux 枚举结果。
研究使用 Kingston 官方 MKF-959.2EN、Mouser 公开托管的 Kingston CY14-02J01 v1.3
及本地 Radxa A7Z v1.11 原理图第4/8/9页；已查看供电、球图、时钟和标记相关原页图像。
没有把用户未提供的 `-02J01` 后缀当作已确认实物，未重分发完整厂商 PDF。
本地下载详细参考 SHA256：`3b32f4010b2638cea6b9417d348e8cbdd095a313556521bc6c4acf1db10affff`。
资料位置/链接及原理图 hash 见上面的器件核对文档。

## 实际修改

- `tools/collect-a733-ufs-linux.py`：schema2，控制器 linked live DT→供电 phandle→
  regulator `of_node` 精确绑定，readback/state/usage 取证，不按名字或范围猜实际供电。
- 子型号参考范围仅用于报告比对，不会编程供电；VCCQ2 明确旧绑定/NC核对提示。
- 缺失、非法/冲突/重复 phandle、重复 regulator、未关联节点、路径越界、遍历超限均
  保持未知；报告永不授权 cold-start、DMA、写入或电气验收。
- 不读 `/dev/mem`，不查询描述符，不改 PMIC/时钟/复位，不改镜像/固件源码。
- 默认仅系统元数据。原有显式64MiB只读 O_DIRECT benchmark 的范围、设备身份核验、
  短读停止及不写块设备边界不变。新建报告普通文件这一写活动如实记录。

## 已执行验证

WSL 主机运行（不作为 A7Z Linux/硬件证据）：

- 新 collector **9组** unittest 通过，最终日志 `out/test-ufs-kingston-20261021-r3.log`：
  精确绑定、软件电压范围上下界/越界、读取缺失、同名但不关联、重复/冲突 phandle、
  重复 regulator、外部 of_node、4096/256项上限、旧VCCQ2不升级、无控制器、
  report/archive SHA、非UFS/分区拒绝和只读 benchmark/短读停止。
  最初8组通过的日志仍保留 `out/test-ufs-kingston-20261021.log`；r2也保留。
- v164 真实生产驱动 **13场景** ASan/UBSan 主机模拟重新通过，
  `out/test-ufs-driver-20261021.log`，没有改动驱动字节。
- checkpoint工具 **3组** 主机测试通过，`out/test-checkpoint-20261021.log`。
- 本次未修改 ARM/C 源码，故不重复生成同源码固件；原 v164 构建/镜像证据保留。

## 恢复、工具包与未通过事项

前恢复点 `out/checkpoints/a733-pre-kingston-profile-20261021/`：
872文件/13996946B，含 Git 可达历史和 dirty patch，4项外部参考。恢复只写新目录。
修改后另存 `out/checkpoints/a733-kingston-profile-20261021/`。
新测试包 `out/a733-linux-ufs-kingston-20261021.tar.gz` 包含独立脚本、测试和说明，
带内部 manifest 及外部 SHA256；不是新镜像，不要求现在换卡。

没有 SSH、板上探测、供电改动、冷启动、传输、格式化、数据迁移或刷机。
**芯片/焊接/电源/时序/PHY/UIC/DMA/容量/文件系统/读写/速度/长稳均未因此验收。**
v164 capsule 的“当时型号未知”记录为真实历史，不追改其 manifest 或历史快照。
按嵌入式芯片适配技能保留恢复点、分层验证和未测边界；不把编译或源码当硬件通过。
