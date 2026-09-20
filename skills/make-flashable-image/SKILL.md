---
name: make-flashable-image
description: 制作 openvela / A733 Cubie A7Z 的可烧写最小镜像：官方镜像 + nuttx.bin + Wi-Fi 固件 + NPU 模型 → 512 MiB 镜像，含 GPT 校验、独立复核与 SHA-256，并给出烧写与回滚步骤。
whenToUse: 需要重新生成、注入内核/固件/模型、或校验可烧写镜像时；内核或镜像生成器改动后必须重跑。
---

# 制作可烧写镜像（openvela / A733 Cubie A7Z）

把当前内核、Wi-Fi 固件、NPU 模型打包成一张可直接 `balenaEtcher`/`dd` 烧写的
512 MiB 镜像。**权威脚本都在 git 仓库 `a733-fix-20260812/tools/`**，本 skill 固化
调用顺序、校验点和不可再犯的坑。

## 前置条件（缺一不可）

| 输入 | 路径（Windows / WSL 同一盘符） | 说明 |
|---|---|---|
| 官方镜像 | `D:\Program-files\openvela\radxa-a733_bullseye_kde_r6.output_512.img` | 6.31 GiB，SHA `D15A292A…`（见 FLASHING_GUIDE） |
| 构建树 | WSL `~/openvela`（`/home/hxy299/openvela`） | 含 nuttx 源码 + cmake_out |
| 内核产物 | `a733-fix-20260812/artifacts/nuttx.bin` | 由 WSL 构建后拷入 |
| 官方 DTB | `a733-fix-20260812/artifacts/official-a733.dtb` | 必须官方，193,692 B |
| Wi-Fi 固件 | `a733-fix-20260812/firmware/` | 5 个 AIC8800D80 文件，大小精确匹配 |
| NPU 模型（可选） | `a733-fix-20260812/npu/` | 存在才打包进 /data/npu/ |

## 制作流程（按顺序，全程在 WSL 里跑 python3）

### 1. 改过板级/芯片源码 → 先同步并重建内核

```bash
# 把 fixed-overlay 里的改动同步到 WSL 树（示例：a7z_storage.c）
cp /mnt/d/Program-files/openvela/a733-fix-20260812/fixed-overlay/vendor/allwinnertech/chips/a733/*.c \
   /home/hxy299/openvela/vendor/allwinnertech/chips/a733/ 2>/dev/null
cp /mnt/d/Program-files/openvela/a733-fix-20260812/fixed-overlay/vendor/allwinnertech/boards/a733/cubie-a7z/src/*.c \
   /home/hxy299/openvela/vendor/allwinnertech/boards/a733/cubie-a7z/src/ 2>/dev/null

cd /home/hxy299/openvela
./build-a733.sh          # 增量 ninja，产物 cmake_out/cubie-a7z_nsh/nuttx.bin
```

> 编译错误常见于 `-Werror=format`：格式串占位符个数必须与参数一一对账，
> `%x/%u` 配 `(unsigned int)` 强转，`UINT64_C()` 只能接字面量不能接变量。

### 2. 内核产物拷回 artifacts

```bash
ART=/mnt/d/Program-files/openvela/a733-fix-20260812/artifacts
cp /home/hxy299/openvela/cmake_out/cubie-a7z_nsh/nuttx.bin "$ART/nuttx.bin"
cp /home/hxy299/openvela/cmake_out/cubie-a7z_nsh/nuttx      "$ART/nuttx.elf"
```

### 3. 一键生成 + 校验

```bash
cd /mnt/d/Program-files/openvela/a733-fix-20260812
python3 tools/make_flashable_image.py --all
```

`--all` = check → copy → apply → minimal → verify，末尾打印新镜像 SHA-256。
产物：`D:\Program-files\openvela\openvela-a733-cubie-a7z-minimal.img`（536,887,808 B）。

### 4. 独立复核（防"自证循环"，绝不省略）

```bash
# 按 NuttX 规则独立重读：GPT 不重叠 + FAT16 + LFN 逆序/校验和 + 长名重构
python3 tools/reverify_image.py /mnt/d/Program-files/openvela/openvela-a733-cubie-a7z-minimal.img

# 独立校验脚本（公式/地址全部重写，不引用生成脚本）
bash tools/verify-minimal.sh
```

## 必须 PASS 的校验点

- GPT 分区 3（ext4 `679936..917503`）与分区 4（/data `917504..1048575`）**首尾相接、不重叠**；
- 分区 1/2 与官方镜像逐字节一致；Boot0(eGON.BT0)/U-Boot/part1/part2 就位；
- `text_offset = 0x00200000`，重定位地址 = `DRAM基址(0x40000000) + 0x200000 = 0x40200000` == 链接地址；
- /data FAT16：VFAT 长名槽位**逆序**（seq=N 带 0x40 在前）、每槽校验和 vs 8.3 短名 PASS；
- /data/aic 下 5 个固件、/data/npu 下模型（若存在）回读齐全。

## 烧写与回滚

```powershell
# Windows：烧写前核对 SHA
Get-FileHash .\openvela-a733-cubie-a7z-minimal.img -Algorithm SHA256
# balenaEtcher / Rufus 选该 img 烧到 SD 卡（≥512 MiB）；Linux 用 dd bs=4M conv=fsync
```

回滚：直接重烧官方镜像 `radxa-a733_bullseye_kde_r6.output_512.img`（最小镜像里已无 Debian）。

## 血泪教训（任何人不许再犯）

1. **text_offset 公式** = `DRAM基址 + text_offset`（**不是** `kernel_addr_r + text_offset`）。
   正确值 `0x00200000`。曾误改 `0x180000` → BL31 跳 `0x40180000` 满屏乱码。
2. **GPT 分区不得重叠**：ext4 与 /data 曾重叠 12 MiB → /data 挂载 `-22(-EINVAL)`。
3. **VFAT LFN 槽位磁盘顺序是逆序**（N→1，最后一槽带 0x40 排最前）。正序会导致
   长文件名解析失败 → Wi-Fi 固件 open `-ENOENT`。
4. **DTB 必须用官方那份**（U-Boot arch fixup 会写 /memory /chosen，自造小 DTB 被打穿）。
5. **内核/extlinux.conf 放 ext4 不放 FAT**（本板 U-Boot 的 FAT 读路径不可靠）。
6. **镜像 SHA 改动后**：同步 FLASHING_GUIDE / SUMMARY / REGRESSION_CHECKLIST /
   DEVELOPMENT_ROADMAP 里的 SHA，并在 WORKLOG 追加版本条目，最后 `git commit`。
7. **NuttX 的 ferr/fwarn 在 CONFIG_DEBUG_WARN/ERROR 关闭时被整体编掉**——底层排障
   必须靠板级自己的 `syslog`（`/dev/a733-hw`、`DATADIAG`），不能指望上游日志。

## 关联文档（同一仓库）

- `FLASHING_GUIDE.md`（布局/原理/烧写）、`WORKLOG.md`（版本历史 v1→…）、
  `DEVELOPMENT_ROADMAP.md`（阶段规划）、`SUMMARY.md`（补丁清单）、
  `board-tests/STAGE0_ACCEPTANCE.md` / `board-tests/NPU_YOLO.md`（板端命令卡）、
  `npu/README.md`（A7PM 模型容器格式）。

## 完成后

- 每成功完成一个阶段 → 在 `WORKLOG.md` 追加条目 → `git add -A && git commit`。
- 把新镜像 SHA 与烧写方法告诉用户，并提示用 `Get-FileHash` 核对后再烧。
