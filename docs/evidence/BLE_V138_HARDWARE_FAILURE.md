# v138 蓝牙首次实机失败与 v139 修复依据

日期：2026-10-12。用户原始附件：`96d1577c-7ced-41e6-b348-0dfde10c19f5/已粘贴的文本.txt`。

## 实测事实

v138 可以启动进入 NSH；USB1 BootROM `a69c:8d80` 上传全部 D80-U02 固件，重新枚举为 `a69c:8d81`。Wi-Fi LMAC/RF/ME/法规域/VIF/MAC 基础检查通过，`wlan0` 注册成功。没有 Wi-Fi 关联、DHCP、ping/SSH 测试证据，不能由此宣布无线联网回归通过。

```text
last=-110 stage=read-local-version probes=1 events=0 hci-status=ff unsafe=0
runtime=a69c:8d81 event=03 ACL=04/04
```

第一项 HCI 查询超时；后续 BD_ADDR 和 LE features 没执行。`unsafe=0` 表示未触发 periodic-stop 失败保护，不代表蓝牙可用。v138 报告未区分 EP0 命令超时与 event 超时，不能凭 `events=0` 证明命令提交成功。

## 明确找到的源代码缺陷

原始 `fw/aic8800D80/fw_patch_table_8800d80_u02.bin` 的 type=3 BTMODE 数据（19 pairs）前九项为：

| index | address | 文件内 value | Linux 加载器实际覆盖值（D80 默认） |
|---|---|---|---|
| 0 | 0x00201fd4 | 0 | 1（hwinfo 无效标记） |
| 1 | 0x00201fd8 | 0 | 0xffffffff（hwinfo=-1） |
| 2 | 0x00201fd0 | 0xff | 0（cpmode WORK） |
| 3 | 0x00201fc8 | 5 | 5（BT_ONLY_COANT，保持 Linux 默认） |
| 4 | 0x00201fc4 | 2 | 1（BTPORT_MB，即 USB 路径的 mailbox） |
| 5 | 0x00201fe8 | 921600 | 1500000（UART 参数，USB 路径无须使用） |
| 6 | 0x00201fe4 | 0 | 1（UART flow control） |
| 7 | 0x00201fdc | 0 | 0（LPM disable） |
| 8 | 0x00201fe0 | 0x5f2f7f2f | 0x00006f2f（D80 power default） |

v138 只覆盖 index 0/1，剩余磁盘模板值被直接写入。这是与 Linux 官方加载契约的明确差异；HCI-port=2 会选择 UART，不能当作 USB HCI 配置。

只读本地依据：

- `fcu760k-official-reference/drivers/aic8800/aic_load_fw/aicbluetooth.c`：`aicbt_btport_type` 的 NULL/MB/UART 为 0/1/2；D80 默认信息结构；`aicbt_patch_table_load()` 覆盖所有九项。
- 同文件 `AICBT_BTPORT_DEFAULT=AICBT_BTPORT_MB`、`AICBSP_CPMODE_WORK=0`、D80 默认 mode/power。
- `fcu760k-official-reference/drivers/aic_btusb/aic_btusb.c`：USB Class EP0 命令 + interrupt event。
- 原始补丁表直接按 little-endian address/value 解码核对，没有修改固件文件。

这是 **很可能导致无 HCI 回复的配置错误**，不是已被实机验证的唯一根因。EHCI periodic 接收仍必须独立测试。

## v139 修复与新增证据

- type=3 至少九项才继续，完整覆盖 Linux D80 默认配置；其他 pairs 保留。
- 对 HCI-port 写入后经 BootROM read32 反向核对；不是只记录预计值。
- 启动日志输出 `firmware mode=5 HCI-port=1 readback=1`。
- `/dev/a733-bt` 报告命令 return/actual、event return/actual、polls、phase。
- 记录周期调度 USBCMD/USBSTS/qTD token 和 FRINDEX 起止值，便于区分“无 event”和“调度没运行”。
- 不更改端点类型、不增加 HCI reset、不重置正在工作的 Wi-Fi，不启用 GATT Host。

恢复点：`backup-before-ble-v139` → `85b2a15`。v138 镜像保持原文件不覆盖。
v139 构建/产物/测试状态见后续本轮验收记录。只有实机三项查询通过，才能进入官方 Host/GATT 阶段。
