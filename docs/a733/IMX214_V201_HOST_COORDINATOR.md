# v201：摄像头单帧 → 同输入 YOLO → LVGL 主机联调工具

日期：2026-10-29。**这是主机工具版本，不是新固件镜像。v200 自动供电和 RAW 仍待板测。**

[最终主机证据与恢复命令](../evidence/camera-vision-v201-final/README.md)。
源码恢复标签：`a733-v201-host-vision-final-20261029`；旧版本及前置 candidate 标签不删除。

## 本轮推进与边界

按“基础门禁未板测，不放开后续硬件”的原则，保持摄像头、NPU、SPI、内存布局和启动脚本不变，
复用 [v200 测试镜像/启动说明](SWOUT1_V200_RAW_TEST.md)。不重新复制 2 GiB 镜像。
新增一键联调工具，把原先两个 SSH 窗口的手动顺序变成有证据的依赖链：

1. 只读核对 YOLO 准备包的路径、普通文件类型及 8,017,024 字节大小。
2. 仅调用一次 `camera capture NEWDIR`；要求 RAW 已发布、验证/清理错误为零且无残留所有权。
3. 要求 COMPLETE 元数据、四个数据文件及尺寸一致。不删除或覆盖旧目录；失败不推理、不显示缺失文件。
4. 第二路 SSH 启动同一 `model-rgb.bin` 的 `display vision-image`。
5. **等待显示端真实就绪和底图 CRC，而不是固定 sleep。** CRC 必须匹配 COMPLETE。
6. 第一路只调用一次 `npu yolo-ui`；要求状态/hw 为零、1,228,800 字节输入 CRC 相同、六输出 CRC 完整。
7. 要求生产端数量/入队数一致，显示端序号/数量/耗时和全部框的索引、类别、分数一致，且 `input-bound=1`。
8. 等待 UI 自然结束，再退出。**零检测是有效结果，会清除旧框；不是自动判错。**最多展示七框，NPU最多返回32框。

成功结果只叫 `SINGLE_FRAME_PROTOCOL_ACCEPTED_NOT_VISUAL_ACCEPTANCE`：表示命令链和消息协议自洽，
**不是屏幕肉眼出图、摄像头色彩、检测准确率、实时视频或 FPS 通过。**
显示端整批日志发生在 LVGL 提交流程内，不是面板电气 ACK；工具还会检查 UI 最终退出状态和 SPI 错误。

## 当前产物与身份

- 工具：`tools/a733-camera-vision-v201.py`，Python 标准库，无 pip 依赖。
- 主机测试：`tools/test-a733-camera-vision-v201.py`。
- 证据收集：`tools/check-a733-camera-vision-v201.py`。
- v200 镜像：`../openvela-a733-cubie-a7z-sd-v200-swout1-raw-candidate.img`。
- 镜像 SHA256：`a729b7442ffc764063b191d8ac3ebb38c4a2812c18f21cc9dacf2cd02fbaaa33`。
- 内核 SHA256：`19e2e0772816a44ee9ec671c3eac92f7978b2587a8a8c502c7abd60919cf96c0`，仍是 v199 内核。
- YOLO 包：`/data/npu/yolov8n-pcq-v3.a7pm`，不是任意模型加载器。
- 参考模型包 SHA256：`3c2891ef77ce9a44dd02cd32bdd7db5ec92d14185014e6de8db5803a6cbecce2`。

工具不会从 `ls` 推断密码学身份，报告明确保留“板上固件和模型 SHA 未独立验证”。
本地准备/烧录时仍须按 v200 封存清单验证镜像和模型。

## 暂不接板：检查计划及回归

在仓库根目录，Windows 或 Linux 均可：

```text
python tools/a733-camera-vision-v201.py
python tools/test-a733-camera-vision-v201.py
```

默认只输出计划，不连接网络。Windows 跳过实际 PTY 和 gcc 两个测试；WSL/Linux 执行全部14组。
包含：成功/零检测/32目标截取七框、所有阶段中断不重试、缺文件/重复元数据/错误 CRC、旧序号、
消息缺失/重复/不绑定输入、SSH输出分块及 ANSI、真实 PTY 隐藏认证和命令退出边界。
PTY 测试将 exec 替换为临时模拟程序，**没有 SSH 服务器或网络连接**。
另将实际 C 导出器以 ASan/UBSan 编译，在四种 Bayer 顺序下生成 CPU 夹具，交给新离线校验器。
这些夹具不是摄像头实拍，也不能证明声学/摄像头硬件可用。

可重复的证据收集（目录必须全新）：

```text
python3 tools/check-a733-camera-vision-v201.py --output out/camera-vision-v201-host-new
```

Linux 还运行既有导出、生产/消费 MQ 和 RAW 帧故障回归。Windows 可加 `--verify-image` 独立重算旧镜像 SHA。
证据收集器需要本地 v200 封存内核及恢复清单（均在 out，普通源码克隆不包含它们）；
普通计划/离线目录校验不需要这些文件。工具将全部287个 `board/` 文件逐字节对比固定 SHA 的
v200 恢复清单，保存源码 SHA、运行时版本、测试退出码/日志 SHA，不仅看 Git 文本 diff。
原因是旧 ORT 来源 JSON 的原有 CRLF 在 WSL 下被报为 diff，而 Windows 原生 Git 不报；
本轮没有改写它，真实字节 SHA 与 v200 恢复档一致。

## 之后有板时：一次运行

前提：V1.11 的 J5 接 13M214，ST7735 接线保持已通过方案；使用 v200 启动流程；
不要同时运行其他 camera、display、npu 或自动视觉任务。不要在无 handoff 时绕过控制门禁。
先保持串口可用，联网并启动已配置认证的 SSH；在串口 `ifconfig` 查看**当次 IP**。
SSH 若未配置，请按既有 SSH 文档配置，不要使用 `sshd start`；正常入口是 `sshd &`。

主机进入 **WSL/Linux 交互终端**，仓库根目录下执行。将 BOARD_IP 替换为当前地址：

```bash
python3 tools/a733-camera-vision-v201.py --run \
  --host BOARD_IP --user openvela \
  --known-hosts /mnt/c/Users/Lenovo/.ssh/known_hosts \
  --new-directory /data/camera201a \
  --result out/camera-vision-v201-board-first
```

仅 `--run` 才连接；密码隐藏询问一次，两路连接复用，不存 argv、环境变量或日志。
要求现有可信 known_hosts，**绝不自动信任新主机密钥或忽略密钥变化**。
若密钥未收录/变化，先通过串口核对指纹后单独建立信任，再运行工具。
WSL 路径示例针对本机 Windows 信任文件；其他 Linux 请给实际可信文件。

远端目录必须是全新的 `/data/camera201NAME` 或 `/ufs/camera201NAME`，后者须已正常挂载；
工具不初始化、挂载、格式化 UFS，也不搬模型。
本地结果目录也必须不存在。默认 UI 运行60秒，可用 `--display-seconds 45..120`。
执行中自动保留 `producer.log`、`viewer.log`、`summary.json`；日志为 UTF-8 文本，不是原始 UART 字节流。
等待超时仅拒绝验收，**不会重试 NPU、强杀远端 DMA 任务、复位、改 PMIC 或擦存储**。
失败后给予待执行命令一次有界自然结束等待；网络丢失不能保证远端已停止，必须串口检查
`camera control status` / `npu report`，有残留所有权/清理错误时按现有恢复说明处理，不能盲目重跑。
摄像头仍是每次 handoff 后的一次实验；同一开机不能靠换目录无限重采。

## 下载后离线核验文件

把完整单帧目录复制到电脑后，Windows/Linux 可只读检查：

```text
python tools/a733-camera-vision-v201.py --verify-bundle /path/to/copied-camera201a
```

检查普通文件/尺寸、COMPLETE、模型 NuttX seed0/no-xor CRC、114填充上下边界、PPM与RGB相等，
输出每个文件 SHA256。不改文件，不联网；RAW/预览未有板上独立 SHA，不能据此保证物理来源或色彩。

## 下一次联合验收与恢复

先验 v200 冷启动 SWOUT1 读回，再验 RAW 发布/清理/文件和实拍内容，最后运行本工具。
保留彩色实物、遮挡/不同场景、零目标和有目标样例，核对原图与框；成功后才考虑连续采集、
帧所有权与丢帧策略、ISP/AE/AWB、真实端到端 FPS。**本轮没有放开任何这些硬件阶段。**
之前 v128/v196/v199/v200、失败日志和开发文档全部保留；v201 是新增工具/文档的独立 Git 恢复点。
