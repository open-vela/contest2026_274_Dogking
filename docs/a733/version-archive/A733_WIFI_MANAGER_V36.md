# A733 / Cubie A7Z Wi-Fi 管理器 v36 候选版

## 目标与结论

本版本修复 FCU760K（AIC8800D80-U02）运行态已经正常、但连续扫描返回零结果的
问题，并为 openvela/NuttX 提供原生 `wifi` 命令。它不移植 Linux `nmcli`：
`nmcli` 依赖 NetworkManager、D-Bus 与 systemd，在 NuttX 上引入这些组件既不合适
也没有必要。新命令直接使用现有 `/dev/a733-wifi` 控制 ABI，可完成：

- 一次操作扫描并合并 2.4 GHz 与 5 GHz 非 DFS 网络；
- 单独扫描 2.4 GHz、5 GHz 或 5 GHz DFS 信道；
- 按 SSID 自动连接，或明确指定 2.4 GHz/5 GHz；
- 交互式读取 WPA2 密码，关闭终端回显，密码不进入命令历史或项目文件；
- 等待 WPA2 四次握手和 DHCP 完成，并提供状态、列表及断开操作。

## 故障定位与修复

故障日志中的 USB 枚举、固件上传、运行态重枚举、LMAC、RF 校准、CN 信道表和
station VIF 全部成功，因此不是供电、天线、固件或 USB 初始化故障。失败只发生在
扫描请求：固件确认请求成功，但没有返回 BSS indication。

D80-U02 对一次请求携带较多信道的扫描不稳定。v36 不再把双频全部信道塞进单个
请求，而是依次执行以下较小分组，并把结果保留在同一个缓存中：

1. 2.4 GHz；
2. 5 GHz 低频非 DFS（36～48）；
3. 5 GHz 高频非 DFS（149～165）；
4. 需要时单独扫描 DFS 信道。

每个分组在“请求成功但零 indication”时最多自动重试三次，重试间隔 100 ms。
`wifi scan`/`scanall` 是一次用户操作，但单射频硬件必然按信道顺序扫描，并非在
物理意义上同时接收两个频段。

## 实板验收

烧录候选镜像后执行：

```text
wifi scan
wifi list
wifi scan 2
wifi scan 5
```

连接 5 GHz（同名双频 SSID 时推荐明确指定频段）：

```text
wifi connect Xiaomi_47C7_5G 5
```

随后程序显示 `WPA2 passphrase:`，直接输入密码并回车；输入内容不会显示。连接
2.4 GHz 可使用：

```text
wifi connect Xiaomi_47C7 2
```

不指定频段时，程序扫描两个频段并连接第一个同名匹配项：

```text
wifi connect SSID名称
```

网络验收：

```text
ifconfig
ping -c 4 192.168.31.1
ping -c 4 223.5.5.5
ping -c 4 baidu.com
wifi status
wifi disconnect
```

通过标准是：扫描至少得到环境中已知的 2.4G 和 5G AP；连接后 `wlan0` 获得非零
IPv4 地址；网关、外网 IP 和域名均可连通。建议分别冷启动测试 2.4G、5G 各三次。

当前连接路径支持 WPA2-PSK/CCMP。WPA3-SAE、企业认证及隐藏 SSID不属于本候选
版本的验收范围。

## 构建和镜像

- 构建脚本：`build-a733-netssh-wifi-v36.sh`
- 构建目录：`quickly-openvela/cmake_out/cubie-a7z_nsh_v36_wifi_mgr`
- 内核：`openvela-a733-stage1/Image-wifi-v36-candidate`
- 候选镜像：`openvela-a733-cubie-a7z-sd-wifi-v36-candidate.img`
- 内核大小：1,033,056 字节
- 内核 SHA-256：`af98d4d1da6749ad3eefd3477d9127abe8882f264549dd7ac7299778f009f4b9`
- 镜像 SHA-256：`3bb1b6c6334a04800f1b6ad635ab6b48b786f279bceb0c3508b3d2961f736aa1`

该镜像已经通过 GPT 分区表、ext4、FAT 和嵌入内核哈希校验。它仍是候选版；在
上述实板验收通过前，v50 NPU 镜像继续作为正式稳定基线且未被覆盖。

