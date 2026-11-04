# v158 RAM 常驻 / 连续 KWS 软件与候选交付证据

2026-10-20。使用 embedded-npu-porting skill 保留恢复点和分层验收。
用户授权开始下一阶段；本轮没有 SSH、录音、音频上传、云请求、重启或烧录开发板。
v157 实机通过单独归档：[原始日志与核验](openvela-v157-runtime-passed.md)。

## 软件/目标编译证据

- 实际 manifest/39张量/状态错误回归 8 组，实际驱动 adapter 16 组、公共 client 8 组
  ASan/UBSan 通过；覆盖双 fd 独立快照切换、暖态不读文件、工作区污染后重建、
  快照内容破坏、预算/首次及载荷分配失败、短读、关闭失败和隔离路径。
- 实际 reserved-page allocator 接实际 client：旧1MiB模式16轮重现 encoder ENOMEM，
  新2MiB模式16轮分配/模拟执行/释放，旧 LeNet 字节和空闲数保持不变。
- 实际 `voice_npu_check.cxx` + `voice_npu_stream.inc` + `voice_kws.c`，
  使用 Linux 已采集原生张量的**主机文件后端**，六场景、1771 次调用、三正例/三负例
  命中符合预期。另测 prepare/输出损坏/关闭/超时、短特征、NaN、未知参考输入七种失败。
  搜索核心没有替换；这不是本轮运行 NPU，更不是新 openvela 硬件验收。
- 旧 npuref 应用11项、音频配置/重采样/ASR路由/完整 TTS/输出选择、搜索8组、
  prepared格式5组和日志校验器回归通过。云及音频在测试中均为 mock。
- ARM64 真编译器：runtime 默认关闭/实验启用、plan/client/搜索五个对象通过；
  实际 C++ KWS 应用对象通过，12个静态栈帧最大1024字节，无动态栈。
  首次 C++ 检查工具输出目录未转绝对路径导致失败，修正工具后通过，失败日志保留。
- 构建时15个关键源/头文件与仓库逐字节一致，记录在 staged-sources JSON；
  所有新增大缓冲均为堆分配。源码未由 Git HEAD 单独代表，交付另存脏工作区源码快照。

本地原始输出保留在 `out/`：

```text
v158-tensor-regression-final.log
v158-kws-host-regression-final.log
v158-npuref-regression.log
v158-audio-regression.log
v158-search-regression.log
v158-prepared-regression.log
tensor-v158-target-compile/compile-summary.json
kws-v158-target-compile/compile.log               # 工具首次失败
kws-v158-target-compile-r2/summary.json            # 修正后真实目标通过
v158-staged-sources-match.json
build-v158-resident-kws.log
sdk-before-v158.json
```

## 构建与镜像

全新构建目录：`quickly-openvela/cmake_out/cubie-a7z_nsh_v158_resident_kws_candidate`。
临时启用 `A733_TENSOR_EXPERIMENTAL=1`，默认产品 defconfig 不改。
完整2972目标、最终链接、布局检查通过；file=2660160、runtime=36950016字节，
相较v157运行占用增加8192字节，固定32MiB DMA arena不扩大。
SDK 148 个暂存路径退出后已逐字节恢复；v157构建/镜像/封存目录未覆写。

候选镜像在仓库上级：
`openvela-a733-cubie-a7z-sd-v158-resident-kws-candidate.img`（2147483648字节）。

- 镜像 SHA256：`44a1d2bce28ef6776f25d3a9f219b364595809026c06d4ff5bafbcbf911b9bf8`
- 内核 SHA256：`3ceef0a67dc8be81862ac6b4aa0dad0f2e733a90158d76300c814ab1a2f2b0b8`
- 私有测试包：`out/a733-openvela-kws-v158-r2.tar.gz`（6175926字节）
- 测试包 SHA256：`231485021a389881d50e1921a07340668a3ef73a3d3f923df8a0f12159239467`

测试包只包含三模型、六组特征、关键字和 CRC 对照及步骤；不含 Linux 输出张量或
host-native-map.json。包内 assets-sha256.json 可核对每个资产。
模型与v156/v157相同，不是重新转换；vocoder不在这次KWS包中。

| A7TV | 字节 | SHA256 |
| --- | ---: | --- |
| encoder | 7917440 | 812c2c8669110178233261671fd8db5e6bd85893a07b6e701ea71f169640bd95 |
| decoder | 588032 | 1248493fd949379f97aeec8e00c90f1592ea57eaa466519d2662b8a64d234123 |
| joiner | 182528 | 60075ca51c0b16867170adf48f9f9bf85c7e3b81a27275f9574a0df363eed144 |

仍是 Zoo1.1.0 对照链路导出的、CID1000003b 专用原生 FP32 I/O prepared snapshot；
转换器/布局/原生参考来源见 [v156记录](prepared-v156.md)。不是通用 NBG 加载器。

从 v157 只读底板复制，仅替换 partition3 的 `/boot/openvela/a733/Image`。
ext4/GPT/内嵌内核校验和分区外字节比较见封存日志；原GPT的partition4末端未2048扇区
对齐提示保留，不做无关重分区。**烧录整卡依然覆盖 data**，必须自行备份整份配置/凭据。

上述校验均已完成：分区3之外逐字节一致；原v157镜像SHA保持
`be08b22602208546a2248a7c362b73f59f98377bdaaa5e95fac1c68f53f45ec7`。
Windows独立Get-FileHash也得到相同v158整卡SHA。构建保留既有第三方签名比较、
UNUSED宏、zblue poll及build-id警告，不能把“编译通过”写作“零警告”。

最终源码/镜像/资产清单在 `out/v158-candidate-20261020/manifest.json`，
源码包为 `out/a733-v158-source-20261020.tar.gz`；新候选尚未开机实测。
源码包SHA256：`1b6c2bd034abc7fc41c9313b6a5c242264603e67e8c3fcc9e8e24d317544a7f2`，
包含395个源码/工具文件，不含私有模型、报告原生张量或完整SDK。

## 未通过的项目，不扩大声明

v158 openvela 六场景、模型切换/释放、暖态时延、温度与长稳仍待实机；
只使用固定 fbank，不含实时 ICS43434/fbank 联动、中文 ASR、完整 TTS 声学前端或 Agent闭环。
按 [详细上板流程](../../a733/LOCAL_NPU_KWS_V158.md) 保存日志后，逐场景核对 calls/hits/chain
及资源恢复，才可提升新阶段验收等级。
