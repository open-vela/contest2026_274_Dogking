# 外部制品与完整性校验

大型镜像、NPU 模型、厂商固件和专有工具不存放在 Git 历史中。这样既可
避免 GitHub 体积限制，也不会重新分发受许可证限制的材料。

## 最新可恢复候选镜像

```text
名称：openvela-a733-cubie-a7z-sd-st7735-lvgl-v120-candidate.img
大小：2147483648 bytes
SHA-256：6bececd77790eba146de0ef5762a2ec11b296a9665fb08f7f9fe24e67024941c
```

镜像内嵌内核：

```text
大小：2183008 bytes
SHA-256：f3871b2422ba000e14df016197fe2a09217a58f03f873e62b4e5084c73700eb7
```

对应源码提交为 `4df0e90`。完整交叉构建、ELF 符号、ext4、GPT 和内嵌内核
一致性校验已经通过。UART4 TW-TTS 已在 v118 实机通过；v120 新增的 ST7735、
`/dev/lcd0`、LVGL NuttX backend 和 `display` 应用仍待实机点亮，因此 v120
不是正式发布版。

镜像由已验证可启动的 A733 基础镜像复制后，仅替换第 3 分区中的 openvela 内核；
基础镜像不会被修改。可重复命令见 `docs/BUILD_AND_IMAGE.md`。

最终比赛发布版本必须重新构建、实机回归并验证镜像，将其作为 GitHub Release 资源，
同时在本文件中补充下载地址、大小和 SHA-256。

## 开发期间使用的 NPU 预处理包

以下信息可供拥有官方模型和 VIPLite 工具合法访问权限的评估者核对确切
输入：

| 制品 | 大小 | SHA-256 |
| --- | ---: | --- |
| `lenet.a7pm` | 441856 | `1e65fc966d5aabbda46f573e16c41c63e6ca3019d241feea12190b1cb38aa027` |
| `yolov5s-zero.a7pm` | 11239104 | `3feb08593d743bb5ca239110013954fe08ad8a864c81fd719b65b2630dbc4472` |
| `yolov8n-pcq-v3.a7pm` | 8017024 | `3c2891ef77ce9a44dd02cd32bdd7db5ec92d14185014e6de8db5803a6cbecce2` |
| dog RGB 输入 | 1228800 | `07a1654c992b1a2e38ccc595a571994889134df9594eef09b5a85d70f4820c60` |
| black RGB 输入 | 1228800 | `3630e065eb7b4540fbab11dbfd2619e8500f211b9c404380a1867fdc44b77c0c` |

A7PM 包含通过官方 Linux VIPLite prepare/run 流程获取并核对过的 VIP 虚拟
地址布局。它们不能替代或解码 Allwinner 专有的 `.nb` 编译器/链接器。

## 仅运行时使用的数据

以下内容应放在开发板数据分区中，不应进入源代码管理：

- 从官方 BSP 获取的 FCU760K D80-U02 固件；
- HTTPS CA 证书；
- 生成的 SSH 主机密钥；
- 本地 Wi-Fi 凭据和服务配置；
- 许可条款允许在本地使用的 A7PM/模型文件；
- 用户数据、采集帧和推理输出。

构建本仓库不需要密码、私钥或 API 令牌。
