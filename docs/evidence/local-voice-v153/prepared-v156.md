# v156：A7TV Linux真实执行与候选固件证据

日期：2026-10-19。用户明确授权 SSH 操作 A7Z Linux，保持 Linux 卡和原通过镜像不变。
此页区分 Linux 板测、主机模拟、openvela 构建与尚未执行的 RTOS 门槛。
操作入口及后续顺序见 [v156说明](../../a733/LOCAL_NPU_PREPARED_V156.md)。

## 环境、权限与资产

- A7Z 官方 Linux `5.15.147-21-a733 aarch64`，`/dev/vipcore`，CID `1000003b`。
- 本轮中网络变更后，SSH 从192.168.1.235切至10.195.194.22；沿用同一私有
  known_hosts 严格核对主机密钥，不关闭主机校验，不把登录/提权凭据写入文件。
- 独立工作目录 `/home/radxa/voice-layout-v155-r3-ssh.Q9pkIEJy/`，原包/失败报告保留。
- 不安装依赖、不改网络、不刷卡、不重启，不执行 reset ioctl、云请求或麦克风采集。
  正常厂商内核初始化可能自行设置NPU频率；本轮没有手动超频/修改时钟。
- 执行前遍历 `/proc/[0-9]*/fd/*` 确认无其他VIP占用者；每个硬件等待5秒，
  外层进程60秒/再5秒强杀，总调用/时间/75°C温度门禁，失败停止而非自动重试。
  进程被杀不保证内核/NPU已恢复，未进行硬件超时/故障注入认证。
- 模型、厂商库、内存快照、prepared、原始语音及完整报告保存在本地忽略目录，
  不加入Git，不公开再分发。以下哈希用于本地资产核对。

## 导出与命令审查

输入是先前通过的 `voice-report-FsNTsIdC`（四模型实际prepare快照）及锁定参考输入。
重新核验所有快照大小/SHA、原生I/O绑定、任务/提交/等待/内核返回后才导出。
每个模型只允许一个core0单子任务，不接受额外init/preload/patch任务依赖。

原内核 `vip_drv_task_get_cmd_info()` 取命令分配**基址**。分配长度多出的56字节不是
entry skip。新A7TV命令偏移固定0，LOAD_STATE/STALL/事件/END/零尾审查通过，无LINK。
此判断来自本地 `bsp-product-aiot-stable/drivers/npu/aw_nna_vip/vip2/` 的
`task/vip_drv_task_common.c`，不是根据单个输出猜测。旧YOLO流程不改动。

| 模型 | 区域 | prepared池字节 | native命令字节 | END偏移 | A7TV字节 |
| --- | ---: | ---: | ---: | ---: | ---: |
| encoder | 84 | 8077312 | 334816 | 334848 | 7917440 |
| decoder | 6 | 606208 | 96944 | 96976 | 588032 |
| joiner | 7 | 204800 | 2680 | 2712 | 182528 |
| vocoder | 7 | 8085504 | 36792 | 36824 | 8069952 |

全部预提交区域原样LOAD，输出区域也保留非输出切片的padding/私有内容。
共享实际NuttX plan解析器的主机ASan/UBSan校验通过，不改变24MiB池或16窗口上限。
这是针对已验证布局的精确地址包，不是任意NBG加载器或恶意命令安全沙箱。

私有目录 `out/voice-prepared-v156-r1`：

```text
encoder.a7tv 812c2c8669110178233261671fd8db5e6bd85893a07b6e701ea71f169640bd95
decoder.a7tv 1248493fd949379f97aeec8e00c90f1592ea57eaa466519d2662b8a64d234123
joiner.a7tv  60075ca51c0b16867170adf48f9f9bf85c7e3b81a27275f9574a0df363eed144
vocoder.a7tv 0593a9d0f45ae4f637e74696b8acdda2d842b9531ade00e1bfdca58c45b9dad7
```

导出时封存的 `export-index.json` 保留当时 `hardware_replay_passed=false`。
下述后来成功记录不回写/篡改旧封存清单。

## Linux分配失败及r3修复（报告不删除）

初版GFP分配导致encoder第一个大区的VIP映射被拒绝；r2一轮分配预检通过，
后续encoder第35区仍偶发失败。两次均在该encoder硬件提交前失败，不是在不确定硬件
完成后盲重跑。根因是厂商MMU按物理分段形态自动选择页型，同一MTLB混用4K/64K被拒绝。

比对内核 `memory/vip_drv_mmu.c` 及 `inc/vip_drv_share.h` 后，r3限定本次观察布局：
窗口0/3的大区用1MiB分段，窗口1小区用连续物理4KiB映射，窗口2中区用连续物理64KiB映射。
不满足长度/对齐/窗口的布局直接拒绝，不尝试修改共享SRAM或硬件页型。
只改变Linux诊断分配参数，不把这些Linux页选择常量照搬进openvela旧映射器。

保留报告：r1 `prepared-replay-k57kihhc`、r2 `prepared-replay-2rdoqax6`
（分配通过）、r2 `prepared-replay-9_ihi93y`（后续分配失败）。
r3 `prepared-replay-w_rhfa11` 全部分配通过；`prepared-replay-qt815vnl` 四模型执行通过。

## 固定与动态输入实测

四模型固定输入全部42个输出完整SHA匹配原厂商基线，无截取预览代替全量比对。
单次内核submit/wait日志：joiner1106µs、decoder646µs、encoder26832µs、vocoder57251µs。
这是每模型一个样本，不含全部加载/I/O，不能称实时性能或暖态基准。

`a733-prepared-dynamic-v156-r1` 的14组实际不同输入全部通过：encoder连续三块、
decoder三种上下文、joiner两种非零/零/重复、vocoder两段语音mel/底值/重复。
128个输出完整SHA匹配；encoder输出1..37实际字节回灌，74处连接再次独立逐字节核对。
输入38明确覆盖为FP32偏移0/16/32；所有模型均出现不同输入/不同输出，重复输入一致。
不使用冻结模型输出代替推理。程序/文件式测试不是常驻RAM实时流。

固定/动态及失败报告总包 `out/v156-linux-reports-20261019.tar.gz`：
`4580447c465870413e67b564dcb27d557f5dfa8d5d07f8945521771b9737ab87`。
本地 `out/v156-checkpoint-audit.json` 显示170输出/74连接通过。

## 完整prepared关键词链路

`a733-prepared-kws-v156-r1` 使用六个已锁定的真实语音特征样本；包内只有参考哈希，
没有冻结encoder/logits原始输出、NBG加载器或厂商用户态库。运行全部神经网络，
decoder上下文和joiner输入由同一应用C搜索即时提出，实际输出进入后续计算。

| 实际NPU调用 | 数量 | 完整输出数 |
| --- | ---: | ---: |
| encoder | 86 | 3354 |
| decoder | 11 | 11 |
| joiner | 1631 | 1631 |
| 合计 | 1728 | 4996 |

三个正例命中：你好小瑞（frame17，mean≈0.2645）、小瑞（frame32，≈0.2520）、
小瑞小瑞（frame35，≈0.1539）；其他命令/噪声/静音三个负例无命中。
第三个分数接近配置门槛0.15，不能据六例宣称高准确率或抗噪能力。

报告 `out/prepared-kws-doyeyu7c.tar.gz`：
`77932aa7b6934326e1d284082236755e1d83bfef14d6b02a20f21ab5adcbc859`。
板端状态 `A7TV_LINUX_KWS_SIX_FIXTURES_PASSED`；结束VIP owners为空，温度约31.5–37.4°C。
尚余25GiB，未重启；测试结束退出提权及SSH会话。

下载后用 `audit-prepared-results.py kws` 独立核验4996输出及所有输入完整SHA，
2960处连续encoder状态回灌，并在主机编译同一 `voice_kws.c` 重算六例命中。
命中短语、帧号和概率与板端逐项一致；`out/v156-kws-audit.json` 为结果。
这仍是预计算fbank→NPU KWS→C搜索，不是实时麦克风、中文ASR、TTS或Agent闭环。

## 软件回归

`out/regression-v156-final.log`：导出器5项、Linux回放生命周期mock10项、应用npuref
11项、A7TV plan8组/实际适配器mock10组/公共客户端mock6组、C关键词核心8组，
以及官方公共API边界检查通过；C/C++测试使用ASan/UBSan及严格编译警告。
覆盖坏文件、非有限输入、完整长度、错误后不公布结果、关闭失败、取消及所有权等。
这不是硬件复位/恢复或长稳认证。

额外新增 `audit-openvela-npuref.py` 自动核对未来实机串口/SSH日志；其4项合成日志测试
通过，覆盖CRC/长度/模式/契约错误、缺失/重复/截断及执行失败。没有把合成日志计作板测。
KWS原始内核日志 `out/v156-kws-kernel.log` SHA：
`ae6afcdb605d0470371db85465b08b940b89ccb6ed94674151f88aefb0a3aa9a`。
独立KWS复核JSON SHA：`ae958bd01f93716c55a3147726fbeb8a59e5952590b5dff38130c6447ee30c15`。

补跑既有 `test-aipet-audio.sh` / `test-aipet-routes.sh` 通过，日志
`out/v156-audio-regression.log`。更新音频测试中的NPU分发stub以匹配新增入口；
证明音频配置损坏、Agent未ready时诊断仍可分发，且不会录音、上传或发声。
真实npuref helper另有独立11项mock测试，不用这个stub冒充其执行通过。

## 私有测试资产与恢复点

Linux replay r3包SHA：`90f5ea7fffac3e00d8fc36289db5b18d1cd7e7aaafe07cd66cc8fd129ce5cabc`。
动态包SHA：`6b9efceefe6d309204702ca2ee8e032473a8b5419102ebb59d7c5e2b368327e2`。
完整KWS包SHA：`6f96501df0017f6d8cbe29f4076ceaf3f0d45cb7be92b2885cda199ff64e3d5c`。

openvela单独资产包 `out/a733-openvela-voice-assets-v156-r1.tar.gz`，11434896字节：
`89c345c29707ec299da57e6b0de089450800edb9e21c9bd256b631ac02466607`。
包含4模型/18固定与动态输入/逐路预期raw CRC/操作表，不含声学验收或RTOS成功声明。

通过的v153内核恢复点 `out/recovery-before-tensor-v155/nuttx-v153.bin`：
`99e6ec93023ae92f3c917eb49a632d76ee78d9c68d3b94afaf457aa359fe94da`。
新构建使用独立 `cubie-a7z_nsh_v156_tensor_candidate`，不覆盖原构建目录。

## openvela候选构建（与硬件通过严格分开）

全量2972目标已链接、Image布局门禁通过，SDK暂存148路径逐字节恢复。
随后针对最终关闭错误处理补丁完成增量链接；新入口、公共客户端、plan/输出检查与提交
均可在最终System.map中找到。GCC13.4.0，CMake/Ninja沿用官方SDK，不更换工具链。
SDK148路径再次恢复通过；旧v153内核独立重新计算仍为上列99e6…哈希。

最终产物保存于 `out/v156-candidate-20261019/`：

| 产物 | 字节 | SHA-256 |
| --- | ---: | --- |
| Image | 2651968 | 3bd60dadeed3b7a5e829ade2b91592ac57e1f38647502d68c7286cffc9c6c24e |
| nuttx.elf | 3690056 | ab4d7c76374008b6c1e7ebd0b9bf9382bda08308463b3158a777cf86ad6b1919 |
| build.config | 103002 | 9569a7a14a523603a91e2c1e9d28872524a3718e989a18c3c9116060f794deec |
| System.map | 432501 | d5f5e7919e66905e71b12c0687301c8efbf1cfff3b3be0b4d1ced7f55ed5c877 |

ARM64 Image魔数/页对齐/段/栈/布局检查通过，文件2651968字节，运行时预留36941824字节，
不越过BL31保留区。候选中TENSOR_IO=y，默认源码配置仍关闭；8核、I2S、LVGL、BLE保留。
全部编译/链接日志 `out/build-v156-tensor-candidate.log` 和
`out/build-v156-tensor-candidate-incremental.log` 保留，含既有工具链/SDK警告，未隐藏。

整卡候选位于仓库相邻 `openvela-a733-cubie-a7z-sd-v156-npu-tensor-candidate.img`，
2147483648字节，SHA：
`d4d4717c7f107bb81384e02249a0cbb8401d95136dc338d1178b13f3dac056e3`。
同名 `.img.sha256` 已生成，Windows独立Get-FileHash与WSL结果一致。

仅复制既有v151底图并替换分区3 `/boot/openvela/a733/Image`；本轮不新增模型或凭据，
模型仍须单独拷入。
修改前/后及最终抽取ext4通过，内嵌Image逐字节/哈希匹配；GPT报告No problems found。
沿用旧底图分区4不结束在2048-sector边界的警告，不重排分区。
范围 `[0,348127232)` 和 `[469762048,2147483648)` 与底图完整逐字节比较一致，
证明分区3之外未改动。底图重新核对仍为
`436ad2fce1e88901b2c2d34f6a5f5d17da4ae4ef9317ab1463c4c39cd2669942`。
布局、打包、镜像检查、SDK恢复日志均在候选目录中。

当前HEAD为 `36077a09e3d9ce4cc2aadd08d28b238ff578bfa9`，工作树包含前期和本轮未提交
修改；不把HEAD冒充当前源码版本。私有代码快照
`out/a733-v156-source-20261019.tar.gz`（818375字节）SHA：
`efad9d3b35065a643949ddc3f03640073a6a6e4b34abf2c9adfe9231e98366c7`。
包含overlay、home、补丁和工具，不含完整官方SDK或私有模型。
候选目录的 `source-sha256.txt` SHA：
`944536d9cb62e807e5158e7477c7e33be8c29d4c7cfb95399ebd0ac174580bef`，
源码完整重新校验通过。没有commit/push，也没有删除任何旧开发文档/失败记录。

**当前仍未把候选刷入板端，没有openvela新语音模型执行记录。**
下一步使用另一张卡，并单独拷入私有资产；按 `README.txt` / v156说明逐路核对。
Linux通过不解除RTOS硬件门槛，ASR、实时流和全助手验收仍未完成。
