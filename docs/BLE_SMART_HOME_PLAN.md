# A7Z ↔ openvela ESP32-S3 智能家居 BLE 链路

2026-10-15后续实机：助手通过SSH完成v145单板运行、官方BLE五轮启停、扫描中退出重进，
扫描期间电脑→板端47包ping零丢失。GATT正向和ESP32-S3双板仍无证据；
默认SSH会话报文损坏、异常退出回收需单独定位。见
`docs/evidence/BLE_V145_SSH_RUNTIME_ACCEPTANCE.md`。原镜像不覆盖，以下历史过程保留。

2026-10-15 继续开发：按用户“直接推进”新增 A7Z 官方 GATT 产品客户端 `home`，
Read/Notify、PING/GET_STATE 业务 ACK 与有限重连，保留原共享协议和 v144 底层。
Wi-Fi 共存暂列未测，不成为等待硬件时的源码开发阻塞；不称双板/GATT 实机通过。
见 `docs/BLE_GATT_CLIENT_V145.md`。下位机 BSP 与实际执行器仍未开发。

2026-10-15 更新：v144 官方 BLE 扫描与五轮正常启停/退出重进实机通过，证据见
`docs/evidence/BLE_V144_FIVE_CYCLE_HARDWARE_PASS.md`。
下一门先补 Wi-Fi 共存，再进入官方 GATT Client/ESP32-S3 Peripheral 双板链路。
五轮扫描通过不代表 GATT、配对、下位机或长时间稳定性已经通过；以下历史记录保留。

2026-10-14 追加：v143 两轮启停实测通过，随后重开无扫描结果、关闭状态 6 卡住。
下一门是 v144 官方 Host 生命周期压力测试，不开发 ESP 下位机，也不把失败移交到 GATT。
见 `docs/evidence/BLE_V144_REOPEN_CANDIDATE.md`；以下记录保留。

最新实测：v142 官方 Host 初始化与扫描通过，但 disable/退出重进失败。
v143 集中修复 BLE-only 生命周期，主机回归、完整构建、新镜像校验通过，待上板。
先通过重复开关与 Wi-Fi 共存门，再进入下位机/GATT，详见
`docs/evidence/BLE_V143_LIFECYCLE_FIX.md`。以下历史更新继续保留。

2026-10-14 更新：v141 HCI query 通过，但官方 Host 初始化 -5、FAT 数据库
路径 ioctl 缺失，BLE 扫描未通过。v142 是命令行/存储/初始化诊断修复候选，
不是 GATT 通信成功版本。详见 `docs/evidence/BLE_V142_HOST_FIX_CANDIDATE.md`。
当前优先验收 A7Z 的官方 BLE_ON 与 scan，再进入 ESP32-S3 下位机。

## 范围与状态（2026-10-11）

后续 v141 实测更新：启动/SMP/计时/Wi-Fi 扫描通过，H4 注册返回 0。v140 因启动异常不再推荐；现在在 v141 恢复官方 Host enable/scan 的分层测试。没有 ESP32-S3 双板/GATT 通过证据；记录见 `docs/evidence/BLE_V141_BOOT_HARDWARE_PASS.md`。

用户决定暂停 I2S 麦克风，转向蓝牙智能家居主控。保留所有 I2S 源码、过程文档和候选镜像；不把电流声测试记为录音通过。MAX98357A 音调输出曾实机通过；INMP441 v137 右声道无声、左声道电流声，仍待后续定位。

恢复点：`backup-before-ble-v138` → `87b6268`。

2026-10-12 实测更新：v138 启动通过，但首次 HCI Read Local Version 超时（-110，events=0），尚不可进入 GATT 阶段。源码审计发现 BTMODE 模板中的 HCI port=UART 未按 Linux 覆盖为 mailbox/USB；v139 已修复并增加分项诊断，编译、镜像校验通过，等待上板。证据见 `docs/evidence/BLE_V138_HARDWARE_FAILURE.md` 和 `docs/evidence/BLE_V139_BUILD_AND_TEST.md`。

2026-10-13 更新：v139 三次 HCI 查询实机通过，见 `docs/evidence/BLE_V139_HARDWARE_PASS.md`。v140 官方 Host 与服务已接入，编译、链接、镜像生成与校验通过，见 `docs/evidence/BLE_V140_BUILD_AND_TEST.md`。v139 镜像不覆盖；`backup-before-ble-v140` 保留接入前仓库状态。v140 实机启动、扫描、ACL、配对、GATT 和 ESP32-S3 双板通信均尚未通过；分层操作见 `docs/BLE_OFFICIAL_HOST_TEST.md`。

## 方案

ESP32-S3 支持 BLE，不支持经典蓝牙，不能使用 SPP/RFCOMM 串口方案。
依据：[Espressif ESP32-S3 蓝牙说明](https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32s3/api-guides/bluetooth.html)。N16R8 是模块存储规格，不改变上述无线能力。

目标结构：

```text
官方 openvela AI Agent / 手动 NSH 命令
          ↓ 受控、白名单的设备动作
A7Z 智能家居服务（设备表、超时、确认、状态）
          ↓ 官方 openvela GATT Client / GAP
官方 openvela 蓝牙服务 + Zblue BLE Host
          ↓ 官方 bt_driver_s / BTH4
A733 FCU760K USB1 HCI（event / ACL，与 Wi-Fi 共用 USB 控制器）
          ↕ BLE
ESP32-S3 openvela GAP Peripheral / GATT Server
          ↓ 下位机校验、去重、限位、超时停止
灯光、继电器、传感器；电机/舵机在安全验收后加入
```

无线命令不承担硬实时：电机 PWM、限位与失联停止由 ESP32-S3 本地执行。禁止将 LLM 输出直接作为 GPIO、寄存器、任意 shell 或不限幅的电机指令。

## 官方代码落点（相对完整 openvela 工作区）

- `frameworks/connectivity/bluetooth/README_zh-cn.md`：官方蓝牙能力、驱动注册说明。
- `frameworks/connectivity/bluetooth/framework/include/bt_gattc.h`：GATT 客户端。
- 同目录 `bt_gatts.h`：GATT 服务端。
- `frameworks/connectivity/bluetooth/tools/gatt_client.c`：连接、发现、写入、订阅、MTU 示例。
- 同目录 `gatt_server.c`：服务端示例。
- `external/zblue/`：官方接入的开源蓝牙 Host；不自写 ATT/L2CAP 栈。
- `nuttx/include/nuttx/wireless/bluetooth/bt_driver.h`：官方文档指定的驱动契约。使用该接口不等于绕开 openvela 上层服务。
- 官方蓝牙框架依赖 `LIBUV`、`LIBUV_EXTENSION` 等；待 HCI 实机验收后单独启用 BLE-only 配置，不默认开启 Classic/音频配置。

用户确认先开发 A7Z，没有现成可用的 ESP32-S3 工程。历史仓库 `https://github.com/hxy299/ESP32S3N16R8-Openvela` 用户称不可用，本轮未读取/修改。完整 openvela 源码已有 `ESP32S3_BLE` 配置及相关控制器支持；后续核验官方板型、N16R8 flash/PSRAM 和烧录方式，再提供下位机应用。当前没有可烧写的 ESP 固件。

## A7Z 硬件事实与实现

板载 FCU760K 已由现有 Wi-Fi 驱动加载 D80-U02 固件并枚举为 `a69c:8d81`：

- interface 0：HCI event IN `0x83`（interrupt，64 bytes，bInterval=4）；ACL IN `0x84` / OUT `0x04`（bulk）。
- interface 1：SCO/iso，智能家居 BLE 数据链路暂不需要。
- interface 2：WLAN data/message，绝不可当作蓝牙 ACL 使用。

Linux 行为参考（只读）：
`A733-A7Z-ALL-Files/fcu760k-official-reference/drivers/aic_btusb/aic_btusb.c`。
HCI 命令通过 EP0 `USB_TYPE_CLASS` 控制请求，event 经 interrupt IN，ACL 经 bulk；USB 数据不带 H4 类型前缀。
该参考针对 D80 的 probe 不走 DC/D80N 的独立 download_patch 分支，但仍须实测当前固件是否能响应。

新文件：
`board/a733-cubie-a7z/openvela-overlay/vendor/allwinnertech/chips/a733/a733_bt_checkpoint.inc`，由 USB1 所有者 `a733_wifi_usb.c` 引入。

诊断节点 `/dev/a733-bt` 只注册、不自动启动；显式 `probe` 依次发送 Read Local Version、Read BD_ADDR、LE Read Local Supported Features。无 HCI Reset、无 USB reset、无电源切换、无设备动作。

event 使用 EHCI periodic schedule，不能冒用 bulk/async。使用独立、静态、DMA32 可寻址、对齐的 QH/qTD/frame list；cache 维护；与 WLAN 同一 USB 锁。每次事件轮询窗口 10 ms，命令超时 2 s；命令之间校验完整 event 长度、opcode、status。停止 periodic 失败时禁止继续改写该 DMA 描述符，要求重启恢复。

此阶段不注册 `/dev/ttyHCI0`、不启用蓝牙 Host：先证实 HCI 再接官方 `bt_driver_s`，防止把枚举成功误判为蓝牙可用。

## 应用协议（编解码主机测试通过，尚未接入无线传输）

共享模块位于 `app/a733home/home_protocol.{h,c}`；`test_home_protocol.c` 为主机测试，不链接板端镜像。已通过 GCC 严格告警、AddressSanitizer / UndefinedBehaviorSanitizer，以及 100000 组畸形输入测试。连接管理、去重、业务 ACK 和安全认证尚待实现。

自定义 128-bit UUID，避免占用 Bluetooth SIG 标准分配 UUID：

- Service：`a7330001-6b42-4f65-8d37-9b184b771001`
- Command：`a7330002-6b42-4f65-8d37-9b184b771001`，Write with response。
- Status：`a7330003-6b42-4f65-8d37-9b184b771001`，Read + Notify。

第一版报文最多 20 bytes，可在默认 ATT MTU=23 下工作，不依赖 MTU 扩展。
布局：version(1)=1、type(1)、seq(2 little-endian)、opcode(1)、payload_length(1)、payload(0..12)、CRC16(2 little-endian，CCITT-FALSE，对前面的所有字节计算)。GATT 单次 value 就是一帧，不在第一版引入分片。

type：request=1、ack=2、event=3；ACK payload 首字节为 status。Write response 仅说明 ATT 写入成功，**业务 ACK 才表示设备动作完成或被接受**。允许命令：ping、get_state、set_output；后续动作应增加能力发现/边界检查。

SET_OUTPUT 请求 payload 固定为 `[output_id,value]`，PING/GET_STATE 请求无 payload；ACK 至少一字节 status。opcode 白名单及格式由 codec 校验；设备权限、输出编号和值的合法范围必须由下位机独立校验。

请求 seq 用于去重与 ACK 匹配；同一连接最多一个待确认动作。重试使用原 seq，下位机返回缓存结果而不重复执行；重连不得自动重放可能产生物理动作的请求。默认禁止未配对、未授权设备写入。

安全验收：LE Secure Connections / bonding、设备白名单、密钥不入 Git、失联回安全态、动作限幅和超时。CRC 仅做格式校验，不提供身份认证。没有 IO 的 Just Works 配对不具有中间人防护，最终必须明确其风险或采用带外/人工确认配对。

## 分阶段验收

1. **HCI 基础**：v138 手动 probe 三项成功；重复 10 次；同时 ping/SSH 不回归。冷启动重复。
2. **官方 Host**：bt_driver_s open/send/close/receive；H4 边界转换、ACL 分片与 credit；Zblue BLE-only，官方 bttool 能扫描。
3. **ESP32-S3**：核验官方板型/控制器驱动；GATT Server 广播服务 UUID，先用 ping/状态模拟验证，不接电机。
4. **双板通信**：扫描→连接→发现→订阅→命令→业务 ACK→notify；超时、断线、重连、重复 seq、坏帧、权限拒绝测试。
5. **Agent 联动**：官方工具注册，动作白名单、用户确认；屏幕呈现设备状态；人工/Agent 命令复用同一设备服务。
6. **压力**：Wi-Fi + 云对话 + BLE 连续状态通知 + LVGL；重连、供电与长时间稳定性。I2S 继续保持暂停。

## v138 上板测试命令

```text
cat /dev/a733-bt
echo probe > /dev/a733-bt
cat /dev/a733-bt
ifconfig
ping -c 4 <实际网关地址>
dmesg
```

成功最低条件：`last=0 stage=hci-query-passed hci-status=00 unsafe=0`，地址/version/LE features 有合理值。失败须回传完整报告及 dmesg，不能仅凭有节点宣布成功。当前没有 `bluetooth scan/connect` 应用命令。

## 构建与恢复

完整工作区 root 由 `OPENVELA_WORKSPACE` 指定；从比赛仓库运行：

```sh
A733_BUILD_DIR="$OPENVELA_WORKSPACE/cmake_out/cubie-a7z_nsh_v138_ble_hci" \
  bash tools/build-a733-wapi.sh
```

该脚本临时 stage 新 `.inc` 和 USB 驱动，EXIT trap 恢复官方文件。所有永久修改保留在本仓库，不删除历史文档。
旧 v137 镜像必须保留，不作为麦克风成功版本；其他功能回归依据见原验收文档。未完成硬件测试前不更换正式比赛交付默认镜像。
