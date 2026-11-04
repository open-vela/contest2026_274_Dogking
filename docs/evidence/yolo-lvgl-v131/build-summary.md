# v131 NPU/LVGL 联动修复构建证据

日期：2026-10-11

## 修复原因

v130 的源码包含 `npu yolo-ui`，但构建包装器的显式暂存清单漏掉
`apps/system/a733npu/`，导致最终镜像仍使用旧 NPU CLI。v131 在不修改官方工作区
长期状态的前提下，把下列文件纳入暂存/恢复事务：

- `apps/system/a733npu/CMakeLists.txt`
- `apps/system/a733npu/Kconfig`
- `apps/system/a733npu/a733npu_main.c`

## 构建与二进制证据

```text
build directory: quickly-openvela/cmake_out/cubie-a7z_nsh_v131_yolo_ui_fix
targets: 2730/2730
result: passed
linked library: apps/system/a733npu/libapps_npu.a
kernel bytes: 2199488
kernel SHA-256: eff2afa0a20c7e956b47dc0bdfb1b493a4bed636cdd46211021d066fa35cd816
```

最终 `nuttx.bin` 与 ELF 均由 `strings` 直接确认包含：

```text
npu yolo-ui <640x640-rgb.bin>
YOLO LVGL overlay active; run 'npu yolo-ui <rgb.bin>' from another shell.
```

## 镜像证据

```text
base: openvela-a733-cubie-a7z-sd-lvgl-fps-v129-candidate.img
output: openvela-a733-cubie-a7z-sd-yolo-lvgl-v131-candidate.img
image bytes: 2147483648
image SHA-256: a8dba939ae52f94890abc0f5665ef294386b42af87aae89f6f4558b7ee5e7389
embedded kernel bytes: 2199488
embedded kernel SHA-256: eff2afa0a20c7e956b47dc0bdfb1b493a4bed636cdd46211021d066fa35cd816
ext4: clean
GPT: no problems found
```

状态：**image-verified，等待实机**。本记录不把软件构建证据冒充硬件通过证据。
