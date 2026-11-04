# A733 I2S0 v132 构建与镜像证据

日期：2026-10-11

## 目标

本候选版本在不覆盖 v131 实机通过镜像的前提下，集中验证 A733 I2S0 与
MAX98357A、INMP441 的 FIFO 样本宽度和启动时序。硬件实测前，本页只证明源码、
构建、文件系统、GPT 和镜像内核一致性，不声明扬声器或麦克风已经通过。

## 主要增量

- 默认 `tone=` 使用 16-bit sample resolution 与 16-bit FIFO 写，匹配厂商 Linux
  `S16_LE` 路径；旧 32-bit 写法保留为 `tone32=` 对照。
- INMP441 使用 24-bit sample resolution，并丢弃启动后的前 100 ms 数据。
- 新增 `capture32=`，把原始 RX FIFO 字保存为 `/data/inmp441-raw32.bin`。
- 新增 PB5..PB8 mux 回读、warmup、raw-nonzero、first/last raw 等诊断。
- 摄像头后续主线固定为 Radxa Camera 13M 214（Sony IMX214）MIPI CSI-2，
  本版本不混入摄像头驱动改动。

## 构建

```text
build: quickly-openvela/cmake_out/cubie-a7z_nsh_v132_i2s_fifo_diag
result: 2730/2730, build completed successfully
nuttx bytes: 3011968
nuttx.bin bytes: 2199512
nuttx.bin SHA-256: 3875d699853878c4a99e4b7e4e532fabe23de0405247332ed462fd69ad30599f
```

第一次构建仅因新增状态文本可能超过 1024 字节触发
`-Werror=format-truncation`；将只读报告缓冲区扩大到 1536 字节后，全量构建通过。

## 镜像

只读底图：

```text
openvela-a733-cubie-a7z-sd-yolo-lvgl-v131-candidate.img
```

新候选镜像：

```text
openvela-a733-cubie-a7z-sd-i2s-v132-fifo-diag-candidate.img
bytes: 2147483648
SHA-256: e3212a5f107cd842b7090b7f8f2afdcc53da5023a08721cc18861e67471b958a
embedded kernel bytes: 2199512
embedded kernel SHA-256: 3875d699853878c4a99e4b7e4e532fabe23de0405247332ed462fd69ad30599f
```

独立回读结果：ext4 `e2fsck -fn` 通过，嵌入内核与构建产物 SHA-256 完全一致，
`sgdisk -v` 报 `No problems found`。分区 4 末尾非 2048 扇区对齐是从已通过底图
继承的提示，不是本轮产生的新错误。

## 待上板门禁

先单独接 MAX98357A，对比 `tone=` 与 `tone32=`；再单独接 INMP441，对比
`capture32=` 原始字与 `capture=` WAV。只有观察到实际发声和非零、随声音变化的
PCM 后，才能把 v132 从 build-passed 提升为 hardware-passed。
