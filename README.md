# Dogking A733：面向端侧 AI 的 openvela 新硬件适配

## 一、作品简介

本项目将 openvela/NuttX 原生适配到瑞莎 Radxa Cubie A7Z 开发板。该板搭载
Allwinner A733（2× Cortex-A76 + 6× Cortex-A55）、4 GiB LPDDR4/4X 和
3 TOPS VIP2 NPU。本项目不在 Linux 用户空间模拟 openvela，而是由板载
Boot0/SCP/BL31/U-Boot 直接引导 ARM64 openvela 内核进入 EL1。

当前已经完成可交互 NSH、8 核 SMP、4 GiB 内存、microSD/GPT/FAT、基础板级
外设、FCU760K 双频 Wi-Fi、WPA2/DHCP/IPv4、SSH/FTP/curl/wget，以及 VIP2 NPU
上的 LeNet、YOLOv5s、YOLOv8n 真实硬件推理。基于 openvela 官方
`packages_ai_agent` 的联网桌宠已完成首次加密配置、自启动、DeepSeek/MiMo
后端和连续中文对话；本地 Qwen2.5-1.5B CPU 推理也已达到基础生成检查点。
外接 USB 音频/UVC、I2S PCM 和桌宠硬件表现层仍在开发，不把候选代码声明为
实机完成。

作品面向离线 AI 助手和视觉识别终端：先把 openvela、联网、文件服务和 NPU
执行环境建立在 A733 上，再打通摄像头输入、YOLOv8 前后处理和结果输出。

## 二、选题方向

主赛道：**新硬件适配**。

扩展方向：**AI 硬件产品创新**。A733 VIP2 NPU 已在 openvela 下执行真实模型，
YOLOv8n 六输出和动态输入均已通过真机验证，而不是固定输出回放。

## 二之二、三个 SoC 平台

本作品是**一次适配三颗国产 SoC**，全部为全新移植（非配置预设）：

| 平台 | SoC | 架构 | 定位 | 详细文档 |
| --- | --- | --- | --- | --- |
| Radxa Cubie A7Z | Allwinner A733 | ARM64，8 核 | 主平台：AI 推理 + 网络 + 显示 + 语音 | 本 README 与 `docs/` |
| Luckfox Lyra Zero W | Rockchip RK3506 | ARMv7-A，单核 Cortex-A7 | 低成本节点 | [RK3506 移植](docs/rockchip/ROCKCHIP_RK3506_LYRA_ZERO.md) |
| Luckfox Pico Mini | Rockchip RV1103 | ARMv7-A，单核 Cortex-A7 | 极简节点：SPI-NAND 启动 + 相机 | [RV1103 移植](docs/rockchip/ROCKCHIP_RV1103_PICO_MINI.md) |

三套移植的源码分别位于 `board/a733-cubie-a7z/` 与 `board/rockchip/`，
由根 manifest `contest2026_274_Dogking.xml` 的 `linkfile` 映射到
openvela 工作树的 `vendor/` 对应位置。

## 二之三、参赛材料

- **技术报告**：[docs/SUBMISSION_TECHNICAL_REPORT.md](docs/SUBMISSION_TECHNICAL_REPORT.md)
- **AI Coding 日志**：`logs/hxy299/`（107 个会话 / 28 个自然日 / 23,933 条事件，
  已通过官方 `validate-log.py` 校验）
- **自定义 Skill**：`skills/`（3 个：`embedded-soc-porting`、
  `embedded-npu-porting`、`make-flashable-image`）
- **提交核对清单**：[docs/SUBMISSION_CHECKLIST.md](docs/SUBMISSION_CHECKLIST.md)

## 三、主要成果

| 子系统 | 状态 | 真机证据摘要 |
| --- | --- | --- |
| ARM64 启动 | 完成 | U-Boot `booti` → BL31 → EL1 → NSH |
| SMP | 8 核基本验证通过 | 6×A55 + 2×A76，逐核身份/亲和性验证；保留栈页对齐修复 |
| 本地 Qwen2.5-1.5B | 基础生成实机通过 | RAM 常驻、6 核工作池、28 层前向、KV cache、中文流式输出和有限多轮已实测；尚未接入桌宠本地回退接口 |
| 控制台 | 完成 | UART0 115200 8N1，输入、termios、Ctrl+C |
| 内存/系统 | 完成 | 4 GiB split-region heap、procfs、`free/ps/uptime` |
| microSD | 完成 | SDMMC0 PIO，12 MHz/4-bit，多块读取 |
| 文件系统 | 完成 | GPT 分区节点，FAT 数据分区读写挂载到 `/data` |
| 板级外设 | 完成 | LED、WDT、THS、I2C2/I2C7、SPI1、风扇 PWM |
| Wi-Fi | 完成，v71 长稳通过 | FCU760K 驱动、标准 WEXT/openvela WAPI、2.4/5 GHz 扫描、WPA2、DHCP、DNS |
| 网络服务 | 完成 | SSH、SCP、FTP、curl、wget、NTP、iperf、可选自启动 |
| NPU | 完成基线 | VIP2 ABI、MMU/DMA/IRQ，LeNet/YOLOv5/YOLOv8 真机运行 |
| 官方 AI Agent | 联网对话实机通过 | `packages_ai_agent`、DeepSeek/MiMo、API Key 板级加密、自动启动和连续中文对话 |
| AI 桌宠路由 | 混合文字链路完成 | 快速规则、官方 Agent、本地 Qwen 回退、回复/表情/动作解析和 UART4 播报 |
| UART4 TTS | v118 实机通过 | `/dev/ttyS4`、TW-TTS UTF-8、sun60iw2 CCU/pinctrl、FIFO 发送和重复播报均通过 |
| I2S 音频 | 诊断阶段 | MAX98357A/INMP441 引脚与时钟诊断已加入，尚无 PCM lower-half |
| ST7735/LVGL | v120 编译与镜像完成，待实机 | 官方 ST7735 驱动、`/dev/lcd0`、官方 LVGL NuttX LCD backend 和 `display` 测试应用 |
| UVC 摄像头 | 进行中 | Type-C/PHY/xHCI 检查点完成，设备枚举尚未完成 |
| Bluetooth/GPU/UFS | 待完成 | 不计入当前完成项 |

详细状态、测试值与限制见 [实现状态](docs/IMPLEMENTATION_STATUS.md) 和
[测试证据](docs/TEST_EVIDENCE.md)。

本地 LLM 最新合并开发说明和测试命令见 [v94 生成链路](docs/a733/LLM_V94_MERGED_GENERATION.md)。
后续 attention 数值对照和单轮对话候选见 [v95 ChatML](docs/a733/LLM_V95_ATTENTION_AUDIT_CHAT.md)。
合并开发 v96 加入流式输出、有限多轮历史和协作停止，原候选说明及板端验收步骤见 [v96 合并开发](docs/a733/LLM_V96_STREAM_HISTORY_STOP.md)。
后续用户日志已验证 v96 中文流式回答与两轮记忆；运行中停止及 Ctrl+C 清理仍待验收/修复。正规 `llm` 应用入口及已知边界见 [v97 应用封装](docs/a733/LLM_V97_APP_FRONTEND.md)。
v97 用户中文多轮测试也已通过；桌宠开始按 Linux 原代码移植，首轮业务核心与 69 个解析行为对照已完成，完整进度和后续接口见 [桌宠移植阶段 1](docs/a733/AIPET_LINUX_PORT_STAGE1.md)。
本地 Qwen 与桌宠路由的正式库级接入、配置命令和板测流程见
[本地 LLM Agent 接入](docs/a733/AIPET_LOCAL_LLM_AGENT.md)。

## 四、仓库结构

```text
contest2026_274_Dogking/
├── board/
│   ├── a733-cubie-a7z/
│   │   ├── openvela-overlay/
│   │   │   ├── vendor/allwinnertech/chips/a733/        # A733 芯片与驱动
│   │   │   ├── vendor/allwinnertech/boards/a733/       # Cubie A7Z 板级代码
│   │   │   ├── apps/system/a733wifi/                    # Wi-Fi 管理命令
│   │   │   ├── apps/system/a733services/                # 自启动服务管理
│   │   │   ├── apps/system/a733ftpd/                    # FTP 服务封装
│   │   │   ├── apps/system/a733npu/                     # NPU 用户命令
│   │   │   ├── apps/system/aipet/                       # 官方 Agent 桌宠路由
│   │   │   ├── apps/system/aipetllm/                    # 本地 Qwen2.5 推理
│   │   │   ├── apps/system/aipetasr/                    # 本地 ASR 接口骨架
│   │   │   ├── apps/system/a733display/                 # ST7735/LVGL 测试与 UI 入口
│   │   │   └── apps、nuttx 的必要兼容文件                # 逐文件 manifest 映射
│   │   └── README.md                                    # 板级说明
│   └── rockchip/                                        # RK3506 / RV1103 移植
│       ├── chips/rk3506/          chips/rv1103/
│       └── boards/rk3506/luckfox-lyra-zero-w/
│           boards/rv1103/luckfox-pico-mini/
├── docs/
│   ├── SUBMISSION_TECHNICAL_REPORT.md               # 参赛技术报告（按官方模板）
│   ├── IMPLEMENTATION_STATUS.md                     # 功能矩阵和边界
│   ├── TEST_EVIDENCE.md                             # 真机验收证据
│   ├── ARTIFACTS.md                                 # 镜像/模型校验与获取规则
│   ├── BUILD_AND_IMAGE.md                            # 可复现构建与镜像
│   ├── OPENVELA_COMPONENT_BOUNDARIES.md              # openvela/NuttX/板级边界
│   ├── AI_PET_OPENVELA_FIRST_ARCHITECTURE.md         # 官方能力优先约束
│   ├── ST7735_LVGL_PORT.md                           # 显示接线与 v121–v123 排障记录
│   ├── SUBMISSION_CHECKLIST.md                       # 提交规则核对清单
│   ├── PROJECT_HANDOFF_20260920.md                   # 交接文档
│   ├── a733/                                         # A733 阶段文档
│   │   └── version-archive/                          # v35–v65 历史版本记录
│   └── rockchip/                                     # RK3506 / RV1103 移植说明
├── patches/                                         # 公共仓可审查兼容补丁
├── skills/                                          # 自定义 Skill（3 个）
├── logs/hxy299/                                     # AI Coding 日志（107 会话）
├── tools/build-a733.sh                              # 完整、可恢复的构建入口
├── contest2026_274_Dogking.xml                      # 仓库 manifest 与 linkfile
└── openvela.xml                                     # 大赛官方基线 manifest
```

Wi-Fi 和其他组件的 NuttX 内核层、openvela 公共组件层及板级私有层边界，见
[`docs/OPENVELA_COMPONENT_BOUNDARIES.md`](docs/OPENVELA_COMPONENT_BOUNDARIES.md)。
AI 桌宠的产品层强制采用“openvela 官方能力优先”架构，具体 API 选择、Linux 原型
迁移映射和自动边界检查见
[`docs/AI_PET_OPENVELA_FIRST_ARCHITECTURE.md`](docs/AI_PET_OPENVELA_FIRST_ARCHITECTURE.md)。
当前端到端状态和各分支完成边界见
[`docs/a733/AIPET_CHAIN_OVERVIEW_20260920.md`](docs/a733/AIPET_CHAIN_OVERVIEW_20260920.md)。
ST7735 接线、官方驱动边界、构建产物和首轮板测步骤见
[`docs/ST7735_LVGL_PORT.md`](docs/ST7735_LVGL_PORT.md)。
后续开发者或 Agent 接手前，请先阅读
[`docs/PROJECT_HANDOFF_20260920.md`](docs/PROJECT_HANDOFF_20260920.md)，其中包含
当前恢复点、完整构建/测试流程、失败方案、未完成任务和安全边界。

模板中的 hello app、quickapp 和示例日志已删除。本作品只使用板级适配形态。

## 五、获取完整 openvela 工作区

推荐 Ubuntu 22.04/24.04 或 Windows 11 + WSL2 Ubuntu。以下操作均在 Linux/WSL
终端执行；不要把 Linux 构建工具直接换成 Windows 工具。整个工作区要放在 WSL 的
ext4 文件系统里（例如 `~/`），不要放在 `/mnt/c` 或 `/mnt/d` 下：跨文件系统访问
会让同步和编译慢一个数量级。

### 1. 先解决 repo 工具本身（国内网络必做）

`repo` 启动器默认从 `gerrit.googlesource.com` 下载自身源码，该域名在国内不可达，
直接执行 `repo init` 会以
`Downloading Repo source from https://gerrit.googlesource.com/git-repo` 开头，并以
`fatal: error [Errno 110] Connection timed out` 失败。先指定可达的镜像：

```bash
export REPO_URL=https://gitee.com/oschina/repo.git
export REPO_REV=stable
```

如果 `~/.repoconfig` 下已有可用的 repo 源码（例如以前成功同步过 openvela），把
`REPO_URL` 指向它即可完全离线：`export REPO_URL=<已有工作区>/.repo/repo`。

### 2. 获取工作区

openvela 基线托管在 gitee。本仓 `openvela.xml` 的 remote 使用**相对路径**
（`../open-vela/`、`../`），repo 会以 manifest 的 URL 为基准解析它们。因此
`repo init` 的 `-u` 必须落在 gitee 的 `open-vela` 命名空间下，否则 264 个项目会
全部解析到 `https://github.com/open-vela/`（不存在）或本地 `/home/open-vela/`，
逐个报 `Cannot fetch ...`。

本仓的 `contest2026_274_Dogking.xml` 只在 GitHub 上提供，所以做法是先取得本仓
manifest、以本地目录作为 manifest 源，再把相对 remote 改写为绝对地址：

> **正式提交版（评委请用这条）**：manifest 已指向组委会仓库与比赛分支。
>
> ```bash
> repo init -u https://github.com/open-vela/contest2026_274_Dogking.git \
>   -b dev-ai-contest-2026 -m contest2026_274_Dogking.xml
> repo sync -c -j8
> ```
>
> 下面这段是**联合开发期**使用个人协作仓库的取法，正式提交不需要。

```bash
mkdir -p ~/openvela-contest && cd ~/openvela-contest

# 取本仓 manifest（GitHub 偶发连接失败，失败时重试）
git clone -b a733-cubie-a7z-share \
  https://github.com/hxy299/hxy299-a733-share-hub.git ~/manifest-cache

# 相对 remote 改绝对地址，否则 repo 无法定位 openvela 基线
sed -i \
  -e 's#<remote fetch="\.\./open-vela/" name="openvela"/>#<remote fetch="https://gitee.com/open-vela/" name="openvela"/>#' \
  -e 's#<remote fetch="\.\./" name="git"/>#<remote fetch="https://gitee.com/" name="git"/>#' \
  ~/manifest-cache/openvela.xml

repo init -u ~/manifest-cache -b a733-cubie-a7z-share \
  -m contest2026_274_Dogking.xml
repo sync -c -j8
```

`repo` 的 linkfile 默认拒绝跨越软链接，而基线把 `apps/external` 做成指向
`../external` 的软链接，本仓又需要在 `apps/external/` 下放兼容文件。这会让
`repo sync` 整体中止，`packages/ai_agent` 等剩余项目不会被检出。给工作区内的
`.repo/repo/project.py` 打一个最小补丁即可（只额外允许解析后仍落在工作区内的
软链接）：

```bash
python3 - <<'PY'
import os
p = os.path.expanduser("~/openvela-contest/.repo/repo/project.py")
s = open(p, encoding="utf-8").read()
old = '''        if platform_utils.islink(path):
            raise ManifestInvalidPathError(
                f"{path}: traversing symlinks not allow"
            )
'''
new = '''        if platform_utils.islink(path):
            _resolved = os.path.realpath(path)
            _base = os.path.realpath(base)
            if _resolved != _base and not _resolved.startswith(_base + os.sep):
                raise ManifestInvalidPathError(
                    f"{path}: symlink escapes workspace ({_resolved})"
                )
'''
assert old in s, "project.py already patched or version differs"
open(p, "w", encoding="utf-8").write(s.replace(old, new, 1))
print("patched")
PY
```

`repo sync` 结束后确认基线版本与 manifest 一致：

```bash
for r in nuttx apps frameworks build tests packages/ai_agent vendor/allwinnertech; do
  printf '%-26s %s\n' "$r" "$(git -C "$r" rev-parse --short HEAD)"
done
```

### 3. 修正 `apps/external` 下的兼容文件

`apps/external` 是指向 `../external` 的软链接，repo 为其中的兼容文件建立相对
软链接后，凡先按字面折叠 `..` 再跟随软链接的工具（CMake、Python `realpath`）
都会把路径解析少一级，文件不可读，CMake 报 `Cannot find source file`。这些文件
必须落成真实文件：

```bash
cd ~/openvela-contest
OV=contest2026_274_Dogking/board/a733-cubie-a7z/openvela-overlay
for f in apps/external/curl/curl_config.h \
         apps/external/libssh/libssh/src/init.c \
         apps/external/libssh/libssh/examples/ssh_server.c \
         apps/external/libssh/libssh/examples/ssh_client.c \
         apps/external/libssh/libssh/examples/libssh_scp.c; do
  rm -f "$f" && cp -f "$OV/$f" "$f"
done
```

同步后，公开协作仓库在 `contest2026_274_Dogking/`，其代码由 manifest 的
`linkfile` 自动映射到外层 `vendor/`、`apps/` 和 `nuttx/`。开发者只应提交本
协作仓库；公共仓修改的长期合入方式见 `docs/UPSTREAM_PLAN.md`。正式向组委会
提交前，需要再切换回官方比赛仓库及其指定分支。

## 六、构建

在完整工作区根目录执行：

```bash
cd ~/openvela-contest
bash contest2026_274_Dogking/tools/build-a733.sh
```

该入口还会临时应用官方 Agent、readline、ST7735 和 ARM64 兼容补丁，并在退出时
恢复公共仓文件；不能用一条裸 `nuttx/tools/build.sh` 等价替代完整产品构建。

构建脚本内含三处本适配必不可少的处理，缺一不可：

1. 用 `patches/ai-agent-a733-pet-and-http.patch` 一次性应用桌宠通道与 HTTP
   收尾补丁。该补丁按 manifest 钉住的版本（`packages_ai_agent` `beabb12`）重新
   生成；原先分两步的 `ai-agent-a733-pet-channel.patch` 与
   `ai-agent-http-completion.patch` 上下文已过期（`http-completion` 的 hunk 6
   引用了 `beabb12` 中不存在的 `pet_tls_read`/`pet_chunked_complete`），会在
   hunk 失败处中止整个构建，因此已删除。
2. 把 `apps/netutils/ftpd/{ftpd.c,ftpd.h,Kconfig}` 与
   `apps/netutils/ntpclient/{ntpclient.c,Kconfig}` 从 overlay 复制进公共仓。
   这些文件提供 `ftpd_set_auth_callback` 等本适配新增接口，manifest 无法为它们
   建立 linkfile；缺失会在链接期报
   `undefined reference to 'ftpd_set_auth_callback'`。
3. 构建后校验 WAPI/SMP 与 ST7735/LVGL 配置并输出 `nuttx.bin` 的 SHA-256。

增量构建：

```bash
bash contest2026_274_Dogking/tools/build-a733.sh --incremental
```

主要产物：

```text
cmake_out/cubie-a7z_nsh/nuttx.bin
cmake_out/cubie-a7z_nsh/nuttx
cmake_out/cubie-a7z_nsh/System.map
```

`nuttx.bin` 带 Linux ARM64 Image header，必须由 U-Boot `booti` 启动；不能用
`go 0x40200000` 进入 AArch64 内核。

完整的依赖、非标准目录变量、镜像封装与独立校验命令见
[`docs/BUILD_AND_IMAGE.md`](docs/BUILD_AND_IMAGE.md)。

## 七、SD 卡启动与部署

当前开发板是 4 GiB、未安装 UFS 的 Cubie A7Z，优先从 microSD 启动。Boot0、
SCP、BL31 和 U-Boot 复用瑞莎官方可工作的启动固件，openvela 已不依赖 Debian
rootfs。比赛仓库不提交 2 GiB/4 GiB 镜像，避免 GitHub 大文件限制；镜像校验值
和发布规则见 `docs/ARTIFACTS.md`。

已经具备 A733 启动固件的 SD 卡可直接替换 openvela 分区中的：

```text
/boot/openvela/a733/Image
```

替换内容为构建出的 `nuttx.bin`。extlinux 条目参考：

```text
LABEL openvela A733
  KERNEL /boot/openvela/a733/Image
  FDT /boot/dtb/sun60i-a733-cubie-a7z.dtb
```

串口：115200 8N1，无硬件流控。预期最终出现：

```text
NuttShell (NSH)
nsh>
```

已有基础镜像时，可以使用 `tools/package-a733-kernel-image.sh` 生成一个不覆盖
原镜像的新候选文件，再用 `tools/verify-a733-image.sh` 检查 ext4、GPT 和内嵌
内核哈希。具体命令见 `docs/BUILD_AND_IMAGE.md`。

## 八、上板验收

基础系统：

```text
uname -a
free
ps
mount
ls /dev
cat /dev/a733-hw
sensortest -n 3 temp0
```

存储：

```text
dd if=/dev/mmcsd0 of=/dev/null bs=512 count=1
dd if=/dev/mmcsd0 of=/dev/null bs=16384 count=64
ls /data
```

Wi-Fi 与网络（密码只在板端交互输入，禁止写入源码）：

```text
wifi scan
wifi connect <SSID>
ifconfig
ping -c 4 <LAN_GATEWAY>
nslookup example.com
curl http://example.com/
```

NPU（A7PM 文件需按 `docs/ARTIFACTS.md` 合法准备并复制到 `/data/npu`）：

```text
cat /dev/npu0
echo selftest > /dev/npu0
echo apitest > /dev/npu0
echo prepared=/data/npu/lenet.a7pm > /dev/npu0
cat /dev/npu0
```

联网桌宠：

```text
aipet init                     # 仅首次运行
aipet status
aipet route "你好"
aipet ask "请只回答：启动测试成功"
```

初始化时选择 DeepSeek/MiMo、模型并隐藏输入 API Key。完整桌宠链路与 UART/I2S
候选测试分别见 `docs/a733/AIPET_CHAIN_OVERVIEW_20260920.md` 和
`docs/a733/AUDIO_UART_I2S_STAGE.md`。

USB 摄像头当前只能用于诊断，不能作为已完成验收项：

```text
cat /dev/a733-uvc
echo probe > /dev/a733-uvc
dmesg
```

## 九、AI Coding 使用说明

本适配使用 AI 完成资料归纳、启动阶段拆解、寄存器对照、驱动骨架、故障假设、
构建脚本、真机日志分析、回归清单和文档整理。所有硬件结论都以串口日志和真机
命令结果验证；“能编译”不被当作“适配完成”。

AI 辅助的典型闭环：

1. 从原理图、A733 BSP、Linux/U-Boot 和手册中提取可验证假设；
2. 一次只增加一个硬件检查点，并为轮询设置超时；
3. WSL 构建、生成新版本镜像，不覆盖已验证恢复点；
4. 用户在真机执行命令并提供完整串口日志；
5. AI 根据寄存器、错误码和前后版本差异定位；
6. 通过回归后再合并下一批功能。

官方归集工具导出的原始会话位于 `logs/`。这些 JSONL 不应手工修改；不提交的
会话应整段删除，而不是删改单条事件。

## 十、安全、许可与隐私

- 项目源码使用 Apache License 2.0；第三方代码继续遵循其原许可证。
- 不提交 Wi-Fi 密码、SSH/FTP 密码、私钥、API key、个人路径或账号令牌。
- 不重新分发许可不明的 Allwinner VIPLite/NPU 编译器、闭源库或固件镜像。
- 模型和大镜像使用校验和、来源和可重复生成步骤描述，不直接塞入 Git 历史。
- 所有未完成子系统均在文档中明确标注，不以检查点冒充完整功能。

## 十一、已知限制和后续计划

1. 外部 USB 设备尚未枚举；v114 Combo PHY/xHCI ring 是未实机验收候选，后续再
   打通 UAC/UVC → PCM/frame → ASR/YOLOv8。
2. Bluetooth HCI、Imagination GPU、UFS 尚未适配。
3. Wi-Fi 已可用但仍需长稳、断线重连、并发吞吐和 DFS 压测。
4. NPU 当前采用官方 Linux VIPLite golden trace → A7PM → openvela 执行路线；
   通用 NBG 编译/链接仍依赖官方授权工具。
5. 公共 `apps`/`nuttx` 兼容修改需要按官方流程拆分并提交对应仓库的
   `dev-ai-contest-2026` PR。
6. 本地 Qwen 已注册到桌宠 `LocalRouteBackend`，但首次从 SD 载入约 1.1 GiB
   模型耗时较长，且当前为 CPU 贪心解码；MAX98357A、INMP441、屏幕表情和
   动作执行仍需实机与产品层验收。

## 十二、官方大赛资料

- [大赛总览](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/contest_overview.md)
- [参赛代码提交指南](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/code_submission_guide.md)
- [AI Coding 日志手册](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/ai_coding_log_guide.md)
- [新硬件适配指引](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/hardware_porting/hardware_porting_track_guide.md)

## 许可证

Apache-2.0。详见 [LICENSE](LICENSE)。
