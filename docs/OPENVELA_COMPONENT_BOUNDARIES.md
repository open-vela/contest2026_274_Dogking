# A733 openvela 组件边界与官方接口迁移记录

## 1. 结论

openvela 的实时内核就是 NuttX。因此，“把底层从 NuttX 改成 openvela”不能理解为
移除 NuttX：CPU 启动、中断、串口、USB、网络设备、块设备和传感器驱动都必须通过
NuttX 内核接口工作。正确的迁移目标是：

1. 芯片和板卡特有代码只负责硬件、固件传输及 NuttX 下半层驱动；
2. 上层应用使用 openvela 工作区已有的标准组件和公共 ABI；
3. 私有 `/dev/*` 控制协议只保留诊断或尚无公共框架替代的功能；
4. 不以改名、包装命令或伪造设备节点宣称完成迁移。

## 2. Wi-Fi 的 v71 分层

### 必须保留的 A733/FCU760K 板级代码

`vendor/allwinnertech/chips/a733/a733_wifi_usb.c` 负责 Cubie A7Z 板载
FCU760K/AIC8800D80 的供电、USB Host 枚举、BootROM 协议、固件装载、LMAC 消息、
RF 校准、收发队列、WPA2 四次握手所需的芯片协议以及 `wlan0` 网络设备。这部分没有
可直接替换的 openvela 通用 FCU760K 驱动，属于必须新增的 NuttX 板级驱动。

### 已迁移到 openvela 公共接口的部分

- `wlan0` 暴露标准 Wireless Extensions（WEXT）IOCTL；
- 启用 `apps/wireless/wapi` 库和 `wapi` 命令；
- 扫描、扫描结果、工作模式、认证方式、密码、频率、BSSID、ESSID、国家码均可经
  WEXT/WAPI 操作；
- `apps/system/a733wifi` 的 `wifi scan/list/connect/disconnect` 已改为直接调用 WAPI，
  不再用 `echo scan/wpa2/disconnect > /dev/a733-wifi` 完成网络控制；
- `wifi connect` 继续在终端隐藏式读取密码，避免密码进入命令行历史；
- 扫描缓存由 16 扩大到 64，避免 2.4 GHz 结果占满缓存后丢失 5 GHz 高信道 AP。

`/dev/a733-wifi` 暂时保留，用于固件、USB、RF、WPA/EAPOL、DHCP 和收发统计诊断，
以及旧镜像兼容。它不再是新 `wifi` 命令的扫描、认证或断开控制面。

v71 在此基础上增加 runtime message endpoint 后台排空和同步命令串行化，已经通过
5 GHz 联网、180 秒空闲、20 次连续网关 ping 和空闲后再次双频扫描回归。证据与恢复
点见 `docs/a733/WIFI_V71_STABLE_BASELINE_20260915.md`。

### v71 板端验收

```sh
wapi scan wlan0
wifi scan
wifi scan 2
wifi scan 5
wifi connect 706E 5
ifconfig
ping -c 4 192.168.31.1
wifi disconnect
```

`wapi psk` 会把密码作为命令行参数，日常使用优先采用交互式 `wifi connect`。

## 3. 当前组件分类

| 功能 | 当前实现 | 分类与处理 |
| --- | --- | --- |
| ARM64 启动、GIC、中断、UART | A733 arch/board 代码接入 NuttX | openvela 内核必需，不可移除 |
| SDMMC、GPT、FAT、procfs | A733 SDMMC 下半层 + NuttX 块设备/文件系统 | 已使用官方内核框架 |
| I2C、SPI、PWM、GPIO、watchdog | A733 下半层 + NuttX 标准设备接口 | 已使用官方内核框架和官方工具 |
| 温度传感器 | A733 THS 下半层 + openvela uORB sensor | 已使用官方传感器框架 |
| Wi-Fi 管理 | FCU760K 下半层 + WEXT + openvela WAPI | v67 已迁移公共控制面 |
| IPv4、DHCP、DNS、ping、iperf、NTP | NuttX 网络栈 + openvela apps/netutils | 已使用官方组件 |
| curl/wget | openvela apps 中的 curl/webclient | 已使用官方组件 |
| SSH/SCP/SSHD | openvela apps/external/libssh | 已使用官方组件；A733 service 仅保存启动策略 |
| FTP | openvela apps/netutils/ftpd | 已使用官方服务器；`a733ftpd` 仅管理根目录、状态和自启动 |
| NSH、reboot、poweroff | NuttX NSH/system 接口 | openvela 默认 shell 基础设施 |
| VIP2 NPU | A733 专用驱动、MMU、A7PM 执行器 | 官方树无 A733 VIP2 替代实现，必须保留板级代码 |
| USB 摄像头 | A733 xHCI/UVC bring-up | 尚未达到标准 video/UVC 完整数据链，继续迁移中 |
| 本地 LLM | openvela 应用 + 移植中的 GGML/ARM64 后端 | 应用层移植，不是内核驱动 |

## 4. 不应错误替换的部分

- NPU、FCU760K 固件协议、A733 SDMMC、USB Host 和时钟/复位没有现成的通用驱动，
  不能删除后宣称“使用 openvela 驱动”。
- openvela 的 WAPI 是控制 API，不会替代芯片驱动、固件或数据面。
- SSH、FTP、curl、DHCP 等已经来自 openvela 工作区；A733 代码应只提供板级配置、
  凭据保护和自启动编排，不应复制实现另一套协议栈。

## 5. 后续收敛顺序

1. 将 `/dev/a733-wifi` 写控制接口标记为兼容模式，默认只读诊断；
2. 为连接状态、RSSI、统计补齐标准 WEXT 查询，减少 `wifi` 对私有诊断文本的依赖；
3. USB 摄像头完成后注册标准 `/dev/video0` 并使用 openvela video API；
4. AI 桌宠按 `AI_PET_OPENVELA_FIRST_ARCHITECTURE.md` 使用 Media、uORB、KVDB、UIkit
   等官方能力，私有设备仅留在 vendor BSP；
5. 将可复用的 NuttX/openvela 公共层修改按 `UPSTREAM_PLAN.md` 拆分上游补丁，板级文件
   留在本比赛仓库 overlay。

## 6. 构建隔离

`tools/build-a733-wapi.sh` 会临时把本仓库中的 Wi-Fi 驱动、板级配置和 `wifi` 应用
复制进 `quickly-openvela`，构建结束（成功或失败）时由 trap 恢复官方树原文件。最终
修改只保存在比赛仓库，官方环境不作为提交来源。
