# 智能家居 BLE 应用与共享协议

v145 在既有 A7Z/ESP32-S3 共享编解码模块上增加 `home` 板端应用，通过官方
openvela GATT Client 实现连接、发现、Read/Notify、PING/GET_STATE 及有限重连。
构建通过 `CONFIG_SYSTEM_A733HOME` 接入；不自写蓝牙 Host，也不暴露 GPIO/电机动作。
主机模拟测试不能代替真实 GATT/双板验收。完整启动前提、外设 UUID、命令和限制见
[v145 客户端说明](../../docs/BLE_GATT_CLIENT_V145.md)。

```text
home info
home frame ping 1
home probe AA:BB:CC:DD:EE:FF random
home monitor AA:BB:CC:DD:EE:FF random 60
```

前两条离线运行；后两条必须先启动官方服务并启用 BLE，且外设实现约定的服务和业务 ACK。
`home` 不自动启动/关闭适配器、不自动接受配对、不发送 SET_OUTPUT。

完整路由、UUID、配对、去重和 ACK 约定见 [智能家居方案](../../docs/BLE_SMART_HOME_PLAN.md)。
SET_OUTPUT payload 为 `[output_id, value]` 两字节；具体设备能力、允许的 output_id/value 必须由下位机做白名单校验，不因格式通过就执行动作。

Linux/WSL 主机测试，从比赛仓库根目录执行：

```sh
bash tools/test-home-client.sh
```

此脚本同时检查协议和实际应用源码（官方 GATT ABI + mock 蓝牙/UV），覆盖异常与资源清理。
仅运行共享协议测试：

```sh
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  app/a733home/home_protocol.c app/a733home/test_home_protocol.c \
  -o /tmp/a733-home-protocol-test
/tmp/a733-home-protocol-test
```

测试覆盖 CRC16-CCITT-FALSE 标准向量、往返、容量、未知命令、ACK 状态要求、错误 CRC、失败不发布输出、长度越界及 100000 组固定种子畸形输入。不代替双板无线测试或安全认证。

USB HCI stream assembler 另有主机测试：

```sh
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -Iboard/a733-cubie-a7z/openvela-overlay/vendor/allwinnertech/chips/a733 \
  app/a733home/test_hci_framer.c -o /tmp/a733-hci-framer-test
/tmp/a733-hci-framer-test
```

覆盖所有示例分割点、合并事件、空 payload、最大 ACL、一字节分片、超长帧、上层拒收、无效参数和 100000 组畸形输入。它只为标准 HCI 组包，不包含私有 ATT 或 GATT 栈。
