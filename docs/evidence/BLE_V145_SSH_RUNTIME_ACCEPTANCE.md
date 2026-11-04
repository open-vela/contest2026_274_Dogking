# v145：SSH 实机回归与限定通过范围

日期：2026-10-15，Asia/Shanghai。由助手在用户授权后通过 SSH 实际操作 A7Z。
测试前源码/镜像归档为 `53376f3` / `a733-ble-v145-gatt-client-candidate`；
本轮不修改或重刷固件、不操作执行器、不连接未知 BLE 外设、不改防火墙和凭据。
旧 v144 镜像、标签和全部历史过程文档继续保留。

## 设备与连接

- A7Z WLAN MAC：`f4:ab:5c:e1:dc:5e`；BT地址：`f4:ab:5c:e1:c8:62`。
- 用户把网络切换为706E，板端 `10.195.194.22/24`，网关 `10.195.194.239`，
  Windows电脑 `10.195.194.107/24`，ARP中板端地址与已知MAC匹配。
- 先前 `192.168.1.235` 不再可达，不继续向旧地址认证。
- 本次新生成的 ED25519 主机公钥指纹：
  `SHA256:Jn0SN3nttv4jXWZJhA7acOGNEPSTzv9eBLakv2HfK+Y`。
  首次连接采用 TOFU 记入用户 known_hosts；后续严格核对，不关闭校验、不删旧密钥。
- 运行标识：`NuttX 0.0.0 Oct 15 2026 13:19:35 arm64 cubie-a7z`；
  `home info` 明确显示 v145。没有再次回读板端 Image 哈希；本轮不是重新冷启动录像。

## 实测通过

1. **v145 应用运行**：info显示三个约定UUID、needs-recovery=0；五种frame命令均打印报文。
   只验证编解码入口运行，不表示发出任何 GATT 数据。
2. **负面路径**：invalid地址、monitor时长0/301被拒绝。
   启动官方服务但适配器仍OFF时，probe以本机控制器地址作为输入，明确返回
   `adapter state=0`、`result=-100 cleanup=0 ACKs=0`，后续info仍needs-recovery=0。
   此路径在尝试连接前即拒绝，不向陌生设备发起连接。
3. **SMP**：`aipetllm cpucheck`八核全部actual=logical、checksum=81e62f98、checkpoint=0，
   CPU0..5为A55、CPU6..7为A76；测试结束恢复原亲和性。
4. **计时**：`time "sleep 5"`返回5.0009秒。
5. **HCI**：在Host未打开时仅执行一次只读probe，last=0、hci-query-passed、3个事件，
   manufacturer=0b3b、unsafe=0。之后不抢占官方Host进行probe。
6. **官方服务**：确认原先没有重复服务后，启动kvdbd PID243和bluetoothd PID244。
7. **正常生命周期五轮**：每轮BLE_ON(2)、scan status=0、真实广播、停止回调、关闭6→0、
   state=0、quit回NSH、再次进入。命令编号及广播记录如下。

| 轮次 | bttool命令编号 | ScanResult记录数 | 扫描期间电脑→板端ping |
| --- | --- | --- | --- |
| 1 | 1..9 | 285 | 15/15，7..55ms，平均16ms |
| 2 | 10..18 | 227 | 8/8，6..22ms，平均12ms |
| 3 | 19..27 | 414 | 8/8，7..48ms，平均17ms |
| 4 | 28..36 | 261 | 8/8，6..24ms，平均12ms |
| 5 | 37..45 | 314 | 8/8，5..20ms，平均10ms |

五轮共1501条ScanResult，包含停止附近已排队回调，不代表唯一设备数。
电脑→板端合计47/47、零丢包；这些是1Hz、32字节ICMP的**短时轻负载**共存测试，
同时维持AES-GCM SSH，不是吞吐/长稳压力或双向网关并发测试。

8. **扫描中直接退出**：第46..50条命令中，扫描尚活跃时直接quit，收到停止回调并回NSH；
   重进后state=2、scan dump=idle，再start收到新广播，stop→disable→OFF→quit正常。
   这两个额外扫描窗口分别163/63条记录。全记录共有7次成功start回调、7次stop回调、
   6次OFF回调，合计1727条ScanResult。
9. **Host健康**：结束时worker仍PID270，open=0、error=0、needs-reboot=0、failures=0，
   tx-command=148、rx-event=1877、tx-acl=rx-acl=0。不把HCI命令流当作ACL/GATT通过。
10. **短循环内存**：第一轮退出used=296272；第五轮紧接退出used=313520，
    sleep2后恢复296272、nused=551；直接退出专项及诊断后used=296400。
    短时异步回收恢复不代表长期无泄漏。首轮前基础used=171552，与服务/Host未启动状态不同，
    不能直接把两者差值判为泄漏。
11. **网关**：BLE循环前后各5/5返回，前一组8..27ms，后一组9..109ms。
    Windows ICMP Echo入站系统规则均未启用，当前网络类型Public；板端→电脑0/3，
    不能据此判定板端所有网络不可用。没有修改Windows防火墙。

结束时蓝牙适配器OFF；kvdbd、bluetoothd和原sshd保留运行。SSH会话以exit正常结束，
本机helper退出码0，没有通过kill结束服务或重启设备。

## SSH发现的问题，不标记为已修复

- 用户的`sshd --setup openvela`配置成功，正确服务启动为`sshd &`。
  `sshd start/setup`不是本版的服务子命令，会进入显式参数路径而不加载保存的密码。
- 服务端`ssh_server.c`认证/打开channel循环是100次×100ms，约10秒，且还有迭代次数限制。
  工具分步等待/首次主机密钥确认可能超过窗口。两次Windows交互尝试在密码发送附近
  遭服务端Bye Bye；不能凭此认定密码错误。
- 使用`tools/ssh-board-terminal.py`，在本机隐藏输入密码后才创建OpenSSH连接，
  自动响应一次密码提示，成功进入NSH；不保存密码、不通过argv/env传密码，
  不读取或复制板端密钥文件，known_hosts严格验证。
- **首次WSL默认协商会话发生真实错误**：已输出info/frame/free/ps，随后发送下一批测试命令时
  客户端报`Bad packet length 1058820993`及`Connection corrupted`，会话中断。
  没有足够证据把根因直接归到某一种cipher、Wi-Fi、libssh或SMP。
- 改为客户端显式`aes128-gcm@openssh.com`后，第二个会话完成本报告全部剩余操作并正常exit。
  这是实测可用的临时客户端选择，**不是对默认SSH损坏的根因修复，也不是全算法稳定认证**。
- AES会话的PS中观察到一个更早的PID219 `ping -c 5 10.195.194.239`长期Waiting Semaphore，
  在BLE启用前已存在，可能与前一SSH异常退出有关；本轮未强杀或排他归因，需另测子进程回收。
- SSH终端/异步bttool输出存在回显交错和部分末尾输出截断；保存原始日志，不把显示排版当
  成链路数据正确性证明。

可手工尝试的客户端命令（密码交互要及时完成）：

```text
ssh -c aes128-gcm@openssh.com openvela@10.195.194.22
```

WSL辅助终端（从比赛仓库根目录执行；地址按实际修改）：

```sh
python3 -u tools/ssh-board-terminal.py 10.195.194.22 \
  --known-hosts /mnt/c/Users/Lenovo/.ssh/known_hosts \
  --cipher aes128-gcm@openssh.com
```

## 证据与完整性

- `20261015-v145-live-ssh-tests-01.log`：保留默认协商会话损坏，SHA-256
  `a77c67b94288f8766e119e6abc66f283b3f9aa0dc23e04c8e9c48e42b86bcc31`。
- `20261015-v145-live-ssh-tests-02-aesgcm.log`：完整回归和dmesg，SHA-256
  `fa18799ad70ad43ba96ff069e3b6f33a8065bfaedc38a5938514971011f79c30`。
- `20261015-v145-pc-ping-during-ble-01..05.log`：电脑侧ICMP结果。
- raw日志保留CR/ANSI和异步交错，不为消除空白告警而重写，且不包含输入密码。

## 明确未测

配套ESP32-S3固件仍不存在：没有对合法外设执行连接/服务发现/特征查询/Read/Subscribe/
Write/Notify/业务ACK，没有真实GATT正向和断连重连验证，也没有配对、授权或动作控制。
没有执行冷启动/多次重启、Wi-Fi断线恢复、持续吞吐、热压力、长时间SMP/内存压力。
没有再测试I2S录音、摄像头、LLM生成、NPU或LVGL；这些历史结论不能由本报告扩展。

按embedded-soc-porting技能保留原恢复镜像、成功与失败日志，将本次标记为
**v145 runtime / SMP checkpoint / BLE five-cycle lifecycle / scan-exit-reentry /
light-load scan+Wi-Fi coexistence passed**，而不是GATT全链路或整机stress-passed。
下一步优先定位默认SSH会话损坏与异常退出回收，再准备约定GATT Peripheral验证正向通信。
