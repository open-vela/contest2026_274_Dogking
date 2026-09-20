# A733 完整构建、镜像生成与校验

## 0. 最近一次已验证的完整复现

在 WSL2 Ubuntu 24.04、x86_64 宿主上按本文从零执行，得到：

```text
nuttx.bin            2183008 bytes
nuttx.bin SHA-256    54b2c6d972749985f935327f984ceb45364c4f80e6624457659cdc28b4021ef7
文件类型             Linux kernel ARM64 boot executable Image, little-endian
Image header magic   0x644d5241（偏移 56）
```

该版本含 ST7735 + LVGL 显示链路（`CONFIG_LCD_ST7735=y`、
`CONFIG_GRAPHICS_LVGL=y`、`CONFIG_SYSTEM_A733DISPLAY=y`）与桌宠本地 Qwen 回退
（`local_llm.cxx` 已通过 `install_local_llm_backend()` 注册到
`set_local_route_backend`）。以 `openvela-a733-cubie-a7z-minimal.img` 为基础
镜像时，package 与 verify 两个脚本均返回 0，内嵌内核哈希与 `nuttx.bin` 一致。
这只证明构建与镜像封装链路可复现，不等于已上板验收；串口启动、显示、音频与
NPU 仍需按 `docs/TEST_EVIDENCE.md` 在真机回归。

本文给出从公开协作仓库复现当前 openvela A733 固件的方法。命令在 Ubuntu
22.04/24.04 或 WSL2 Ubuntu 中执行。Windows PowerShell 只用于 Git 和文件管理，
不要直接代替 Linux 构建环境。工作区必须放在 Linux 文件系统内（`~/`），放在
`/mnt/c`、`/mnt/d` 下会因跨文件系统访问显著变慢。

## 1. 获取工作区

### 1.1 让 repo 工具可达

`repo` 启动器默认从 `gerrit.googlesource.com` 下载自身源码，国内网络不可达，
`repo init` 会以 `fatal: error [Errno 110] Connection timed out` 失败。先指定
镜像；若本机已有可用 repo 源码，把它作为 `REPO_URL` 可完全离线：

```bash
export REPO_URL=https://gitee.com/oschina/repo.git
export REPO_REV=stable
```

### 1.2 manifest 与基线的关系

openvela 基线在 gitee。`openvela.xml` 的 remote 是**相对路径**
（`../open-vela/`、`../`），repo 以 manifest 的 URL 为基准解析它们，所以 `-u`
必须落在 gitee 的 `open-vela` 命名空间下。本仓 manifest 只在 GitHub 提供，因此
先用本地目录作为 manifest 源，并把相对 remote 改写为绝对地址：

```bash
mkdir -p ~/openvela-contest && cd ~/openvela-contest

git clone -b a733-cubie-a7z-share \
  https://github.com/hxy299/hxy299-a733-share-hub.git ~/manifest-cache

sed -i \
  -e 's#<remote fetch="\.\./open-vela/" name="openvela"/>#<remote fetch="https://gitee.com/open-vela/" name="openvela"/>#' \
  -e 's#<remote fetch="\.\./" name="git"/>#<remote fetch="https://gitee.com/" name="git"/>#' \
  ~/manifest-cache/openvela.xml

repo init -u ~/manifest-cache \
  -b a733-cubie-a7z-share \
  -m contest2026_274_Dogking.xml
repo sync -c -j8
```

不做上述改写时，264 个项目会全部解析到 `https://github.com/open-vela/`
（不存在）或本地 `/home/open-vela/`，逐个报 `Cannot fetch ...`。

### 1.3 允许 linkfile 跨越工作区内软链接

基线把 `apps/external` 做成指向 `../external` 的软链接，而本仓需要在
`apps/external/` 下放兼容文件。repo 的 `_SafeExpandPath()` 无条件拒绝中间软链接
且没有开关，会让整个 `repo sync` 中止，`packages/ai_agent` 等后续项目不会被
检出。给 `.repo/repo/project.py` 打一个只放宽到“解析后仍在工作区内”的补丁：

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

### 1.4 校验基线版本

```bash
cd ~/openvela-contest
for r in nuttx apps frameworks build tests packages/ai_agent vendor/allwinnertech; do
  printf '%-26s %s\n' "$r" "$(git -C "$r" rev-parse --short HEAD)"
done
```

应分别对应 manifest 中 `dev-ai-contest-2026` 的提交：`76354c637858`、
`550cd3ba60a0`、`1e690984f234`、`8493db46d183`、`67029fe1c85b`、
`beabb12f72dd`、`9d34b3f0d02a`。

### 1.5 修正 `apps/external` 下的兼容文件

`apps/external` 是软链接，repo 为其中的兼容文件建立相对软链接后，凡先按字面
折叠 `..` 再跟随软链接的工具（CMake、Python `realpath`）都会把路径解析少一级，
文件不可读并报 `Cannot find source file`。这些文件必须落成真实文件：

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

manifest 将比赛仓库检出为 `contest2026_274_Dogking/`，并把板级源码、应用和必要
兼容文件映射到 openvela 工作树。`packages_ai_agent` 等公共仓修改以仓内 patch
保存，构建脚本临时应用并在退出时恢复，避免污染公共仓源码。

## 2. 完整构建

```bash
cd ~/openvela-contest
bash contest2026_274_Dogking/tools/build-a733.sh
```

可选参数和环境变量：

```bash
bash contest2026_274_Dogking/tools/build-a733.sh --incremental
JOBS=12 bash contest2026_274_Dogking/tools/build-a733.sh

OPENVELA_WORKSPACE=/path/to/openvela \
A733_BUILD_DIR=/path/to/openvela/cmake_out/cubie-a7z_nsh \
bash contest2026_274_Dogking/tools/build-a733.sh
```

脚本会检查 openvela-first 架构边界，临时应用公共 Agent、readline 和 ARM64
兼容 patch，完成 Cubie A7Z 构建，核对 SMP/WAPI 配置并输出 SHA-256。无论成功、
失败或中断，都会恢复临时修改的公共仓文件。

主要产物：

```text
cmake_out/cubie-a7z_nsh/nuttx.bin
cmake_out/cubie-a7z_nsh/nuttx
cmake_out/cubie-a7z_nsh/System.map
```

`nuttx.bin` 带 ARM64 Linux Image header，必须通过 U-Boot `booti` 启动。

## 3. 生成可烧写 SD 镜像

本仓不重新分发 Boot0/SCP/BL31/U-Boot。以一份已经验证能启动 Cubie A7Z 的
基础 SD 镜像为输入，只替换 openvela 分区中的内核：

```bash
cd ~/openvela-contest
BASE=/path/to/known-good-a733-base.img
OUT=/path/to/openvela-a733-current-candidate.img
KERNEL=cmake_out/cubie-a7z_nsh/nuttx.bin

bash contest2026_274_Dogking/tools/package-a733-kernel-image.sh \
  "$BASE" "$OUT" "$KERNEL"
```

脚本拒绝覆盖基础镜像或已存在的输出文件，提取 GPT 第 3 分区，通过 `debugfs`
替换 `/boot/openvela/a733/Image`，回读比较内核，再把完整 ext4 分区写入新镜像。

## 4. 独立校验

```bash
KERNEL_SHA=$(sha256sum cmake_out/cubie-a7z_nsh/nuttx.bin | cut -d' ' -f1)
bash contest2026_274_Dogking/tools/verify-a733-image.sh \
  /path/to/openvela-a733-current-candidate.img "$KERNEL_SHA"
```

校验包括 `e2fsck -fn`、回读内嵌内核、`sgdisk -v`、镜像大小和 SHA-256。
依赖通常由 `coreutils`、`e2fsprogs` 和 `gdisk` 提供。

## 5. 发布边界

- `.img`、ELF、模型、A7PM、构建日志和本地恢复 bundle 不进入 Git。
- 可公开分发的最终镜像应上传 GitHub Release，并在 `docs/ARTIFACTS.md` 写明
  来源、大小、SHA-256 和实机验收状态。
- v120 是当前最新可恢复候选镜像；它在已通过 UART4 TTS、Wi-Fi、官方 Agent
  和本地 Qwen 接口的 v119 基线上加入 ST7735/LVGL。v120 已完成构建和镜像校验，
  但显示硬件仍等待实机测试，不应标记为正式发布版。
- 烧录会覆盖目标 TF 卡。先备份 `/data/models`、Wi-Fi、SSH/FTP、Agent 密钥
  和私人配置。凭据禁止进入 Git 历史或公开镜像。
