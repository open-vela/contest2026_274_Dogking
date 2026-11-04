# Kingston UFS128-CY14：器件确认与供电门禁（2026-10-21）

## 用户提供的实物信息

```text
Kingston
2401131-001.A00G-A
2341
C308Z5313.01
UFS128-CY14
M307W3716.00
TAIWAN
```

原样保留，未擅自修正字符。Kingston 官方产品表确有 **UFS128-CY14 / 128GB / UFS3.1**：
[Kingston 官方产品资料](https://media.kingston.com/pdfs/emmc/MKF_959-Design-in-UFS_en.pdf)。
这确认了系列资料可供研究，不验证实物真伪、焊接质量、容量或 ordering suffix。
`240...` 不应替代产品型号。用户尚未提供此芯片在 Linux 枚举的结果。

查到的详细参考是 Kingston 编写、Mouser 公开托管的
[CY14-02J01 v1.3 数据手册](https://www.mouser.com/datasheet/2/1389/Kingston_UFSnnn_CY14_02J01___v1_3-3314915.pdf)，
并非用户已确认的 `-02J01` 订货后缀。该 PDF 自带 CONFIDENTIAL 标识，本仓库仅记录
必要事实和来源，不分发完整 PDF、截图或厂商表格；当前资料不授予再分发权。

详细参考中的 VCC 范围为 **2.4–2.7V**，VCCQ 为 **1.14–1.26V**；
参考时钟支持 19.2/26/38.4MHz。它不能用于设定未核实实物的电压或承诺板端速度。
手册表中的电源上升时间是限制，不是可直接照抄的上电延时/完整时序。

## A7Z 原理图交叉检查：只读研究结论

使用本地 `radxa_cubie_a7z_schematic_v1.11.pdf`，共16页：

- SHA-256：`eae921f432cef107e9c3229c113b0267e62669c035c467ada982fbf968c14a4d`。
- 第4页 POWER：UFS3.x 的 VCC-UFS 标注 DLDO6/2.5V，经 RP293；另一条
  RP287 标注 UFS2.2/3.3V，不能在不知道实板贴装的情况下视为同一路已正确供电。
- DCDC8 经 RP290 到 SoC 的 VCC-UFS-IO，经 RP291 到 VCC12-UFS；
  RP292 为 NC/0R 的 VCC18-UFS 选件。DC8SET 表中 RP55=1K、RP57=NC
  对应默认1.2V；这只是图纸方案，不是本板电阻或电压实测。
- 第9页 UFS：A4/A5/B4/B5/C4/C5/E5/F5 所在 VCCQ 网络连 VCC12-UFS；
  旧 VCCQ2 网络另列出 A6/A7/B6/B7/C6/C7/K6/K7。
- 与上述 Kingston 子型号球图交叉核对：后面这组旧 VCCQ2 球为 NC，A8 也是 NC。
  不能凭 Linux 中存在 `vccq2-supply` 就断言 Kingston 需要第三路电源。
  图纸仍使用三星 UM2 器件符号；本板硬件版本和实际选件须另外核实。
- 第8页给出 26MHz 晶体及 AP-UFS-CLKOUT；这是连线依据，不是 refclk 波形验收。

**禁止按本文直接补焊/改压/短接。** 需要结合实际板号、贴装、实物型号和测量复核。
给 NC 球已有的外部连线应列为硬件核对项，不武断断言当前硬件已经损坏或必然兼容。
SoC PHY 的 VDD08-UFS、VCC-UFS-IO 与存储器 VCC/VCCQ 是不同角色，不能混为一谈。

Linux BSP 的 `vcc=dldo6`、`vccq/vccq2=dcdc8` 只说明软件绑定关系。
500000..3400000 的 DT 范围不是配置值；此前 v164 文档中的这个警告仍有效。

## 新增软件：沿实际供电引用取证

`tools/collect-a733-ufs-linux.py` 报告 schema 升为2，新增 `electrical_evidence`：

1. 只从 `/sys/bus/platform/devices/4520000.ufs/of_node` 定位 live DT，
   检查路径在 DT 内，不靠目录名字猜控制器。
2. 解析 big-endian 32位供电 phandle；缺失、长度错、0、重复目标均不升级为有效。
3. 通过 regulator 的 `of_node` 精确关联，采集 `microvolts/state/num_users`。
   不按 `dldo6/dcdc8` 名字猜电压，不把 DT min/max 当 readback。
4. `configured_range_match` 仅比较软件电压读回与**候选参考**范围；
   即使为 true，`physical_voltage_verified/cold_start_authorized/dma_mapping_verified`
   仍为 false。软件读回不能证明芯片端电压、纹波、上电时序或供电电流余量。
5. VCCQ2 单列旧绑定/NC 核对提示，不设第三路默认目标值。
   DT/调压器遍历分别限4096/256项；未知结果不触发任何初始化动作。

不读 `/dev/mem`，不写 PMIC、时钟、复位、sysfs，不发送新的 UFS 描述符查询，
不格式化/挂载/分区/迁移。不自动修改 v164 固件；该固件边界保持不变。
默认不读取原始块数据；可选原有 benchmark 的只读边界见 `UFS_STORAGE.md`。
报告文件会写入新目录，不能声称整台 Linux 系统完全没有文件写活动。

以后统一 Linux 板测可使用新版工具，仍只需：

```sh
sudo python3 collect-a733-ufs-linux.py
```

不要求现在换卡/运行。检测结果只是下一阶段 cold-start adapter 的输入，不能直接授权
编程 PMIC、接管 DMA、读写 LUN 或更改文件系统。

## 下一阶段与恢复

供电和实际型号核实后，仍需独立实现/验证 cold-start 的 DCXO/clock/reset、
M-PHY 校准/SRAM/UIC、device init、DMA mapping/lifecycle，然后才进行只读容量/数据验收。
写入、flush、分区挂载、高速优化和模型迁移继续按顺序进行。
当前 SDK 缺少可直接启用的 UFS host，不能把本文当作已完成适配。

新增前恢复点 `out/checkpoints/a733-pre-kingston-profile-20261021/`：
872个源码/文档文件，13996946B，4项外部引用；原 v164 source checkpoint、
capsule、Linux工具包及镜像均不改动。修改后的新工具/文档另行封存。
恢复仅写入新目录，不回滚或覆盖原仓库。保留所有旧日志及开发过程文档。

本次没有修改固件源码，不生成只改文档名字的新镜像。
**系列确认/主机工具验证不等于 cold-start、硬件枚举或高速读写通过。**
