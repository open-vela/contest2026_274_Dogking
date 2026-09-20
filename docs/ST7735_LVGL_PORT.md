# Cubie A7Z ST7735 与 LVGL 适配

本适配复用 openvela/NuttX 官方 `drivers/lcd/st7735.c`，板级代码只负责
A733 SPI1、D/C 和 RESET 接线。应用通过标准 `/dev/lcd0` 和 LVGL NuttX LCD
后端访问屏幕，没有引入 Linux `spidev` 或 Python 运行时依赖。

## 已验证硬件与接线

| ST7735 | Cubie A7Z | 物理引脚 |
| --- | --- | --- |
| VCC | 3.3 V | 1 或 17 |
| GND | GND | 任一 GND |
| SCK | PD11 / SPI1_CLK | 23 |
| MOSI | PD12 / SPI1_MOSI | 19 |
| CS | PD10 / SPI1_CS0 | 24 |
| DC | PL5 / R_PIO GPIO | 22 |
| RST | PB0 / GPIO | 7 |
| BL | 3.3 V | 1 或 17 |

MISO 不连接。屏幕为 1.8 英寸 128×160 ST7735，SPI Mode 0，RGB565。
当前 A733 polling lower-half 将频率限制为 12 MHz。

## 软件层次

```text
A733 SPI1 lower-half + SPI_CMDDATA(PL5)
  -> 官方 st7735.c
  -> lcd_dev_s 和 /dev/lcd0
  -> 官方 LVGL NuttX LCD backend
  -> 桌宠 UI
```

`a7z_st7735.c` 是板级 glue，通用控制器逻辑仍由官方驱动维护。SPI1
lower-half 同时支持 8-bit 命令和高字节优先的 16-bit RGB565 像素传输。

## 构建

在完整 openvela 工作区根目录对应的比赛仓库中执行：

```bash
bash contest2026_274_Dogking/tools/build-a733.sh
```

构建脚本只在构建期间暂存 overlay，并在退出时恢复官方工作区。

本阶段已完成 WSL 全量构建、最终 ELF 符号检查和镜像内核回读校验。构建结果：

```text
build: quickly-openvela/cmake_out/cubie-a7z_nsh_v120_st7735
nuttx.bin: 2183008 bytes
nuttx.bin SHA-256: f3871b2422ba000e14df016197fe2a09217a58f03f873e62b4e5084c73700eb7
```

可烧录候选镜像（不提交到 Git）：

```text
A733-A7Z-ALL-Files/openvela-a733-cubie-a7z-sd-st7735-lvgl-v120-candidate.img
size: 2147483648 bytes
SHA-256: 6bececd77790eba146de0ef5762a2ec11b296a9665fb08f7f9fe24e67024941c
```

镜像已经通过 ext4、GPT、内嵌内核大小及 SHA-256 回读校验。编译和封装成功不等于
面板实机验收完成；首次烧录后仍需执行下一节命令确认接线、方向和颜色。

## 首次上板检查

上电前再次核对 3.3 V、GND、DC 和 RESET，禁止把屏幕接到 5 V 信号。
启动后检查：

```text
dmesg
ls /dev/lcd0
display status
display test 30
display run
```

预期日志包含：

```text
A733: ST7735 LCD ready at /dev/lcd0 (0)
```

`display test` 使用精简的官方 LVGL 核心绘制 RGB 色条和桌宠脸，不会把完整
LVGL demo 素材集合装入产品固件。`display run` 持续运行，按 `Ctrl+C` 退出。

若画面方向正确但红蓝互换，切换 `CONFIG_LCD_ST7735_BGR` 后重新构建；
若屏幕整体镜像，调整 `CONFIG_LCD_RPORTRAIT`。这两个选项只描述面板差异，
不应在应用层交换颜色或坐标。

## 桌宠接入约束

- 所有 LVGL API 由单一 UI 线程调用。
- Agent、ASR、TTS 和本地 LLM 只向 UI 消息队列发送状态事件。
- 情绪状态应保持到下一次明确更新，避免每句文本完成后闪回 idle。
- 首版使用局部刷新与约 10～15 FPS 动画；DMA/FIFO 优化应保持为后续独立改动。

## 当前验收边界

- 已完成：A733 SPI1、`SPI_CMDDATA`、板级复位、官方 ST7735、`/dev/lcd0`、
  官方 LVGL NuttX LCD backend、显示测试应用、全量编译和镜像封装。
- 待真机：面板点亮、RGB/BGR、旋转方向、连续刷新稳定性和实际帧率。
- 待产品层：将桌宠 Agent 的情绪/动作事件接入单一 LVGL UI 线程。

## v121：把面板初始化移出开机关键路径

### 现象

烧录 v120 后串口停在 C runtime 初始化末尾，**NSH 不出现**，前面只有 8 核并发
启动造成的交错乱码：

```text
- Ready to Boot Primary CPU
- Boot from EL1
- Boot to C runtime for OS Initialize
  A7Z0 / A7Z1 / N0 / N1 / H0 ... （多核并发输出，字符交错）
```

没有 CPU Fault，也没有 panic，符合“在某个初始化函数里死等”。乱码本身是 SMP
早期并发打印，不是根因。

### 根因

`a7z_boardinit.c` 在板级 bring-up 里**无条件**执行：

```c
ret = board_lcd_initialize();
if (ret >= 0)
  {
    ret = lcddev_register(0);      /* -> board_lcd_getdev(0) */
  }
```

`lcddev_register()` 会调到 `board_lcd_getdev()` → `st7735_lcdinitialize()`，
后者除了下发整张面板命令表，还会做一次**全屏清屏**：128×160 = 20480 像素。
在 A733 的 polling SPI1 lower-half 上，这些像素以单点传输下发，是整个 bring-up
里最慢、也最容易被 SPI/CS/DC/RESET/时钟或 FIFO 状态异常拖住的一步。它发生在
C runtime 结束之前、NSH 起来之前，所以一旦卡住，控制台什么都看不到。

结论：**不是 LVGL 的问题，也不是内核崩溃，而是面板初始化被放进了开机关键路径，
且该路径上有大量同步 SPI 发送。**

### 修复

1. **把面板初始化移出开机路径。** 新增 `CONFIG_BOARD_A7Z_EARLY_ST7735`
   （默认 `n`）。`lcddev_register(0)` 只在显式选中该选项时才在 bring-up 中执行。
   默认配置下，无论屏幕是否接上，板子都能走到 NSH。
2. **改为按需初始化。** `a7z_st7735.c` 新增 `a7z_lcd_ensure_registered()`：
   首次调用时完成 `board_lcd_initialize()` + `lcddev_register(0)`，幂等。
   `a733display` 的 `status` 与 `test`/`run` 都先调用它，因此
   `display status` 反映的是“面板能否真的拉起来”，而不是“之前有没有人拉过”。
3. **补齐 bring-up 日志。** SPI1 总线初始化、RESET 脉冲、`st7735_lcdinitialize`
   进入/返回、`/dev/lcd0` 注册各自打印一行。这样即使面板再次卡住，也能从一份
   串口日志直接定位到是哪一步。

### 验证边界

本轮只验证到“构建 + 镜像 + 内核回读”，**没有实机验证**。验收顺序固定为：

```text
1) 不接屏开机          -> 必须进入 NSH（这是本次修复的主目标）
2) 接屏后 display status -> 观察是否打印 bring-up 日志并返回 ready
3) display test 30      -> 观察画面、方向与色序
```

若第 2 步仍卡住，日志会停在具体某一行，据此外查 SPI FSR/BUSY 状态寄存器、
SPI1 时钟与复位、CS/DC/RESET 电平和引脚复用；下一步才是把逐像素 `SPI_SEND()`
改成逐行 `SPI_SNDBLOCK()`（约 20480 次 → 约 160 次）并给所有轮询加超时。

### 本轮构建与镜像

```text
nuttx.bin            2183008 bytes
nuttx.bin SHA-256    310c00f72709d7860d96496e8160100df8f423b1457cb837f31f7353802de235
镜像                 openvela-a733-cubie-a7z-minimal.img（536887808 bytes / 512.0 MiB）
镜像 SHA-256         73dccbb033e16be0983cf4e6cc39fde3030e1981d7ee6e6df53f6952839b2204
```

镜像经 `make_flashable_image.py --all`、`reverify_image.py`、`verify-minimal.sh`
三条独立路径校验通过；镜像内 `/extlinux/nuttx.bin` 与
`/boot/openvela/a733/nuttx.bin` 回读均与本轮 `nuttx.bin` 逐字节一致。

### 同轮附带修正

`a733_header_peripherals.c`、`a733_sdmmc.c`、`a733_i2s0_audio.c` 的
`A733_CCU_BASE` 曾被回退成 `0x02001000`。该地址属于 sun50iw10/sun55iw3/sun8iw21，
在 A733 上是解码空洞，写入不会编程模块时钟/总线门控/复位。已统一改回
`0x02002000`（与 `a733_npu.c`、`a733_trng.c`、`a733_tsadc.c`、`a733_wifi_usb.c`、
`a733_usb_camera.c`、`a733_uart4.c` 一致），并把 `a733_sdmmc.c` 加入构建脚本的
overlay 暂存列表，否则该修正不会进入编译。

### v121 补充：与 Linux 已验证驱动的逐条对照

同一块屏在 Linux 上已经跑通，参考实现在
`D:\My-Program-Files\old-program\AI-Linux-Pet\vision-4\hardware\st7735.py`。
两者接线完全一致（SCK PD11/pin23、MOSI PD12/pin19、CS PD10/pin24、DC PL5/pin22、
RST PB0/pin7、MISO 不接、SPI Mode 0、RGB565、BGR），差异在两处：

| 项目 | Linux（已跑通） | 本适配 v120 | 处理 |
| --- | --- | --- | --- |
| 全屏填充 | 组好 RGB565 缓冲，按 2048 像素分块 `writebytes()` | `st7735_fill()` 逐像素 `SPI_SEND()`，共 20480 次 | **已改为按行 `SPI_SNDBLOCK()`，约 160 次** |
| 初始化命令表 | 仅硬件复位 + 面板默认值 | 同左 | 保持不动，不照搬 Adafruit 的 FRMCTR/PWCTR/VMCTR/Gamma |
| SPI 频率 | 40 MHz | 12 MHz（polling lower-half 限制） | 保持 12 MHz，先保证正确性 |

关键在第一条。本板的 SPI1 lower-half 中，每一次 `SPI_SEND()` 都会走到
`a733_spi_transfer_bytes()`：复位收/发 FIFO、重写 BC/TC/BCC、置 `XCH`、再轮询
`FSR` 直到完成（带 100 ms 超时）。因此 v120 的清屏 = **20480 次完整的控制器
事务**，每一次都有机会命中超时分支。这不是“有点慢”，而是把整个 bring-up 压在
一条极其低效的路径上，正是开机卡死的直接来源。

`st7735_fill()` 现在分配一条行缓冲（`ST7735_XRES` 个 `uint16_t`），填色后按行
`SPI_SNDBLOCK()`。`sndblock` 在本 lower-half 中直接映射到
`a733_spi_exchange()`，也就是官方 `st7735_wrram()` 一直在用的路径。分配失败时
回退到原逐像素路径，不会留下黑屏。

验证方式（不依赖上板）：`drivers/lcd/st7735.c.o` 在改动前**没有**任何
`malloc`/`free` 未定义符号，改动后同时出现 `U malloc` 与 `U free`，而
`kmm_malloc` 只出现在 `st7735_fill()` 中，可确认新路径已编入。

### v121 最终构建与镜像

```text
nuttx.bin            2183008 bytes
nuttx.bin SHA-256    7e1d0947e766ef0f5eeac2e47e9a5f85c8b2bde9e8e079f86f133e4a2588af0f
镜像                 openvela-a733-cubie-a7z-minimal.img（536887808 bytes / 512.0 MiB）
镜像 SHA-256         c9d89304fbdc573abe38d41739e9c61eb00f78a5ec25aec8cabd227211d60068
```

镜像经 `make_flashable_image.py --all`、`reverify_image.py`、`verify-minimal.sh`
三条独立路径校验通过；镜像内 `/extlinux/nuttx.bin` 与
`/boot/openvela/a733/nuttx.bin` 回读均为上表内核哈希。

## v121 崩溃根因：LVGL 释放路径对内部指针调用 free()

### 定位过程

`display test` 崩在分配器里，两次故障点随堆布局变化而移动，说明不是被释放的
指针本身有问题，而是**堆被提前写坏**：

| 版本 | ELR | FAR | ESR | 含义 |
| --- | --- | --- | --- | --- |
| v121（无保护带） | `mm_forcefree+0xb0` | `0x0000a000000110` | `0x96000044` | 合并空闲块时向垃圾地址写入 |
| v121（`MM_NODE_GUARDSIZE=16`） | `memset+0xa8` | `0x48000000` | `0x96000046` | `memset(mem, 0x55, nodesize)` 长度字段被写坏 |

两次都是 `WnR=1` 的写入异常，且 `mm_free()` 在本配置下**直接**调用
`mm_forcefree()`（`mm_free.c:317-322`，因为 `CONFIG_MM_FREE_DELAYCOUNT_MAX=0`），
所以每一次 `free()` 都会走到这里，不需要事先存在被覆盖的节点头。

### 根因

`apps/graphics/lvgl/lvgl/src/drivers/nuttx/lv_nuttx_lcd.c` 的
`display_release_cb()` 原来这样释放绘制缓冲：

```c
if(disp->buf_1) { lv_free(disp->buf_1); disp->buf_1 = NULL; }
if(disp->buf_2) { lv_free(disp->buf_2); disp->buf_2 = NULL; }
```

而 `disp->buf_1` 指向的是**内嵌在 `lv_display_t` 里的结构体**，不是独立的堆块：

- `src/display/lv_display.c:448-450`
  `lv_draw_buf_init(&disp->_static_buf1, …); lv_display_set_draw_buffers(disp, &disp->_static_buf1, …);`
- `src/display/lv_display_private.h:104`
  `lv_draw_buf_t _static_buf1;  /* embedded in lv_display_t */`

所以 `lv_free(disp->buf_1)` 是在对**分配块内部的指针**调用 `free()`。
`mm_forcefree()` 随即执行 `node = mem - MM_SIZEOF_ALLOCNODE`，这个 `node` 落在
`lv_display_t` 内部，`nodesize` 是从结构体字段里读出来的垃圾值，接着
`next->blink->flink = next->flink` 就是一个无界写入——正是观测到的异常形态。

这与「`display status` 正常、`display test` 立刻崩」完全吻合：前者只走
`st7735_lcdinitialize()` + `lcddev_register()`，从不进入 LVGL 的释放路径。

### 修复

改为**只解除引用，不释放**：

```c
disp->buf_1 = NULL;
disp->buf_2 = NULL;
```

`_static_buf1/2` 是 `lv_display_t` 的成员，随 display 一起释放；它们包裹的像素
内存属于调用方。

**注意：这里不能用 `lv_draw_buf_destroy()`。** 本树里 `lv_draw_buf_init()`
（`lv_draw_buf.c:327`）会无条件置上 `LV_IMAGE_FLAGS_ALLOCATED`，而
`lv_display_set_buffers()` 调用它时**没有传 handlers**，于是
`draw_buf->handlers` 为 `NULL`；`lv_draw_buf_destroy()` 会先
`LV_ASSERT_NULL(draw_buf->handlers)`，断言关闭时直接解引用 NULL，并且还会通过
`buf_free_cb` 释放调用方的像素缓冲（本应用自己也会释放）——双重释放。对静态缓冲
这两种做法都是错的。

### 同时做的一处配置修正

`CONFIG_LV_NUTTX_LCD_BUFFER_SIZE=16` 是**无效配置**：在
`apps/graphics/lvgl/lvgl/Kconfig:1843` 它依赖 `LV_NUTTX_LCD_CUSTOM_BUFFER`，
而该 choice 从未选中，Kconfig 直接丢弃这个值（生成的 `.config` 里连
"is not set" 都没有）。代码因此回退到 `lv_conf_internal.h:3463` 的默认 60 行；
反汇编 `lv_nuttx_lcd_create` 可见折叠后的 `mul w23, w23, w24`（w23=60、
w24=hor_res），证实实际用的是 60 行。现在改为显式
`CONFIG_LV_NUTTX_LCD_BUFFER_COUNT=1`，即单块全屏缓冲并进入
`LV_DISPLAY_RENDER_MODE_FULL`，同时消除了 PARTIAL 分带路径。

### 验证边界

本轮仍未上板。合并后的镜像已通过构建、ELF 反汇编核对（`display_release_cb`
中已不存在对 `disp->buf_1` 的 `lv_free`，只剩 `close(fd)` → `lv_free(dsc)` →
`puts`）、以及 `make_flashable_image.py --all` / `reverify_image.py` /
`verify-minimal.sh` 三条独立校验。

另外在释放回调里加了一行**无条件** `puts("display: released (lv_nuttx_deinit done)")`：
原 `LV_LOG_USER()` 在 `LV_USE_LOG` 关闭时会被整体编掉，导致上一轮日志无法判断
崩溃发生在测试刚开始还是 30 秒超时之后。这一行可以让下一次板测直接判定。

## v121 实机确认与第三层问题：面板初始化序列缺失

### 崩溃已修复（实机确认）

```
nsh> display test 5
display: initializing ST7735 (/dev/lcd0) ...
ST7735/LVGL test active; press Ctrl+C to stop.
display: released (lv_nuttx_deinit done)
nsh> display test 30
display: initializing ST7735 (/dev/lcd0) ...
ST7735/LVGL test active; press Ctrl+C to stop.
display: released (lv_nuttx_deinit done)
nsh>
```

连续两次都走完释放路径并干净返回提示符，**数据异常完全消失**。同时 dmesg 证明
面板初始化真的执行了且不再拖住开机：

```
[37.533] display: bring-up: SPI1 bus init
[37.708] display: bring-up: RESET pulse done (PB0 high, SPI1 ready)
[37.708] display: bring-up: st7735_lcdinitialize start
[37.866] display: bring-up: st7735_lcdinitialize done (0x42470b10)
[37.866] display: /dev/lcd0 registered on demand
```

`st7735_lcdinitialize` 到 done 只用了 **158 ms**（含全屏清屏），v120 时代那
20480 次逐像素发送确实是死等来源。

### 第三层：屏幕仍无显示 —— 缺整整一段面板初始化序列

拿 Linux 上已验证的参考驱动逐条对照命令表：

| 命令 | Linux（已点亮） | NuttX 官方驱动 |
| --- | --- | --- |
| SWRESET | ✅ | ❌（但板级做了硬件复位，可省） |
| SLPOUT | ✅ 500 ms | ✅ 120 ms |
| FRMCTR1/2/3 | ✅ | ❌ |
| INVCTR | ✅ | ❌ |
| PWCTR1/2/3/4/5 | ✅ | ❌ |
| VMCTR1 | ✅ | ❌ |
| COLMOD | ✅ | ✅ |
| GMCTRP1/GMCTRN1 | ✅ | ❌ |
| NORON | ✅ | ❌ |
| MADCTL | ✅（rotation） | ✅（orientation 选项） |
| DISPON | ✅ | ✅ |

也就是说 NuttX 官方驱动**只发了 5 条命令**，把电源、升压、VCOM、gamma 和帧率
整段省掉了。面板能收命令、背光亮、但升压器和 VCOM 从没被编程——这与「控制器
有响应、屏幕全黑」完全吻合。

### 修复

在 `st7735_lcdinitialize()` 里补上这段序列，参数直接取自已验证驱动：

```
SLPOUT  ->  再等 380 ms（凑足参考驱动的 500 ms 稳定时间）
COLMOD / MADCTL（沿用 NuttX 原有处理，保留 orientation 配置）
FRMCTR1 = 01 2C 2D
FRMCTR2 = 01 2C 2D
FRMCTR3 = 01 2C 2D 01 2C 2D
INVCTR  = 07
PWCTR1  = A2 02 84
PWCTR2  = C5
PWCTR3  = 0A 00
PWCTR4  = 8A 2A
PWCTR5  = 8A EE
VMCTR1  = 0E
GMCTRP1 = 02 1C 07 12 37 32 29 2D 29 25 2B 39 00 01 03 10
GMCTRN1 = 03 1D 07 06 2E 2C 29 2D 2E 2E 37 3F 00 00 02 10
DISPON
```

命令常量在 `st7735.c` 内本地声明，因为私有头 `drivers/lcd/st7735.h` 没有定义
它们，而私有头不在本项目的补丁覆盖范围内。

**不重发 SWRESET**：板级 `board_lcd_initialize()` 已经做了硬件复位（150 ms
稳定），参考驱动里 SWRESET 的作用就是替代一次软复位。

### 编译期验证

`drivers/lcd/st7735.c.o` 由 **6064 字节增长到 8664 字节**；反汇编中出现了
`#0xffffffc0`…`#0xffffffc5`（即 PWCTR1..VMCTR1 的 0xC0–0xC5）、`#0xffffffda`、
`#0xffffffe0`、`#0xffffffe1` 以及 `0x2c/0x2d/0x0a/0x8a/0x37/0x2a/0x2e/0x29`
等参数值，确认整段序列已编入。

### 本轮构建与镜像

```text
nuttx.bin            2183008+ bytes
nuttx.bin SHA-256    f73454c42d26708dacd23be004799bbca2357d6b019d48c214cdf24d89d94f73
镜像 SHA-256         52ae5594606021c7b048c3b594b3a9f25b9dcce30b8ed3a52f4840a629f43cf1
```

三条独立校验全部通过，镜像内 `/extlinux/nuttx.bin` 回读与本轮内核逐字节一致。
屏幕是否点亮仍需上板确认。

## v121 屏幕全白根因：A733 pinctrl 布局用错（HW type 4 vs 旧版）

### 诊断输出把问题一次指出来

新增的 `/dev/a733-lcd` 回读到（无条件复现，与 dcon/dcoff 无关）：

```text
pio-pd:  cfg0=00000000 cfg1=00000000 drv0=00000000 pul0=00000000 data=00000000
rpio-pl: cfg0=ff1ff122 drv0=11111110 pul0=00000025 data=00000024 pl5=1
spi1:    gcr=00000083 tcr=000000c4 ccr=0000100b ...
ccu:     spi1bgr=00010001 spi1clk=87000000
```

`gcr=0x83` 说明 SPI1 已 EN|MASTER，`ccr=0x100b` 说明时钟已配好——控制器没问题。
但 **PD bank 的配置寄存器读回全 0**：一个被正确 mux 过的引脚不可能是 0，所以那些
写操作落到了别的地方。

用户用万用表实测 D/C：`dcon` 时只有 **0.49 V**，`dcoff` 时 0 V——不是真正的输出电平。

### 根因

同一个仓库里的 `a733_uart4.c`（已实机验证）注释写得很清楚：

```c
/* sun60iw2 uses pinctrl HW type 4, not the legacy Allwinner layout:
 * bank A starts at +0x80, each bank occupies 0x80 bytes, drive registers
 * start at +0x20 and pull registers at +0x30. */
uintptr_t base = A733_PIO_BASE + A733_PIO_FIRST_BANK + bank * A733_PIO_STRIDE;
uintptr_t drv  = base + 0x20 + (pin / 8) * 4;
uintptr_t pul  = base + 0x30 + (pin / 16) * 4;
```

而 `a733_header_peripherals.c` 仍在使用**旧版 Allwinner 布局**：

```c
#define A733_PIO_STRIDE   UINT64_C(0x30)          /* 缺 FIRST_BANK，步长也错 */
uintptr_t base = A733_PIO_BASE + bank * A733_PIO_STRIDE;
uintptr_t drv  = base + 0x14 + ...;               /* 应为 +0x20 */
uintptr_t pul  = base + 0x24 + ...;               /* 应为 +0x30 */
```

后果：

| 引脚 | 应写地址 | 实际写地址 | 结果 |
| --- | --- | --- | --- |
| PD10（SPI1_CS0） | `0x02000290` | `0x02000194` | **从未 mux 成 SPI1** |
| PD11..PD13（CLK/MOSI） | `0x02000290`+ | 同类偏移错误 | 同上，**没有波形送出** |
| PL5（D/C） | `0x07025090` | `0x07025090`（cfg 恰好巧合相同） | cfg 生效但 **drv 写到了错位置**，驱动能力不足 → 实测 0.49 V |

也就是说 SPI1 的三根线根本没有被复用成 SPI 功能，屏幕收不到任何数据或时钟；
D/C 虽然 mux 上了，但驱动寄存器写错位置，电平被拉不起来。这两点合起来正好解释
「初始化流程全部执行完、耗时也变长了，但屏幕一片白」。

### 修复

```c
#define A733_PIO_FIRST_BANK    UINT64_C(0x80)
#define A733_PIO_STRIDE        UINT64_C(0x80)
#define A733_PIO_DRV_OFF       UINT64_C(0x20)
#define A733_PIO_PUL_OFF       UINT64_C(0x30)
#define A733_PIO_DATA_OFF      UINT64_C(0x10)

static uintptr_t a733_bank_base(uintptr_t pio_base, unsigned int bank)
{
  return pio_base + A733_PIO_FIRST_BANK + bank * A733_PIO_STRIDE;
}
```

并新增 bank 编号常量（`A733_BANK_PA..PL`，PL=11），让 `a733_pinmux()`、
`a733_gpio_write()`、`a733_gpio_bank_output()` 三个 helper 共用同一套地址计算，
不再各自硬编码偏移。PL5 的四处调用点全部改为
`a733_bank_base(A733_R_PIO_BASE, A733_BANK_PL)`，不再直接传控制器基地址。

注意这个修正同时影响本文件里的 **I2C2/I2C7（PD16/PD17、PJ22/PJ23）和风扇 PWM
（PJ27）** 的引脚复用——它们此前同样写错了地址，只是各自的诊断还没暴露出来。

### 顺带修掉诊断自身的两个问题

1. `read()` 原来不检查 `f_pos`，`cat /dev/a733-lcd` 会**无限循环**。改为照
   `a733_uart4.c` 的写法：`offset >= length` 时返回 0 表示 EOF；
2. 增加 `dcon` / `dcoff` / `selon` / `seloff` 写命令，可以直接从 NSH 拨动
   D/C 和片选并用万用表核对，不必重新编译。

### 本轮构建与镜像

```text
nuttx.bin SHA-256    e8b816914aa3b439541208c7628c3f9033adb64fcd9335130a5b2294055f7903
镜像 SHA-256         c19e0c6c0aee69d50b104b0ba3174947197813c19a76a07caa29eb8a9e176ce4
```

三条独立校验通过，镜像内 `/extlinux/nuttx.bin` 回读与本轮内核逐字节一致。屏幕是否
点亮仍需上板确认；上板后 `pio-pd: cfg0` 应显示 PD10..13 的 4 位为 6，且 `dcon`
时 PL5 应为 3.3 V。

## v122 屏幕仍全白：R_PIO bank L 的基址算错，PL5 从来没被复用成 GPIO

### 现象

上一轮修好主 PIO 布局后，重新烧写：**面板残影消失**（说明 SPI 真的在发数据了），
但依然没有色块，D/C 仍为 **2.13 V**。

2.13 V ≈ 2/3 VCC，这是"被弱驱动但没到位"的典型电平，不是悬空读数。

### 根因：把 bank 索引乘进了 bank 基址

`a733_rpio_bank_base()` 当时写成：

```c
#define A733_RPIO_FIRST_BANK   0x210
#define A733_RPIO_STRIDE       0x104
return A733_R_PIO_BASE + A733_RPIO_FIRST_BANK + (bank - A733_BANK_PL) * A733_RPIO_STRIDE;
```

`0x210` 的来源是调试脚本里 `BANK_L * BANK_STRIDE = 11 * 0x30`——**把 bank 的字母
序号当成了偏移量**。查官方 BSP 驱动可以确认 R_PIO 根本不需要这个乘法：

```text
drivers/pinctrl/pinctrl-sun60iw2-r.c
    .bank_base = sun60iw2_r_bank_base[] = { SUNXI_BANK_OFFSET('L','L'),
                                            SUNXI_BANK_OFFSET('M','L') };
    .hw_type   = SUNXI_PCTL_HW_TYPE_1;

drivers/pinctrl/pinctrl-sunxi.h
    SUNXI_BANK_OFFSET(bank, bankbase) = (bank) - (bankbase)      /* L → 0 */
    sunxi_pinctrl_recalc_offset() = bank * bank_mem_size + initial_bank_offset

drivers/pinctrl/pinctrl-sunxi.c  sunxi_pinctrl_hw_info[1]
    mux 0x00, data 0x10, dlevel 0x14, bank_mem_size 0x30, pull 0x24
```

所以 **bank L 就是 R_PIO 的起始 bank，偏移 +0x000**，bank 间距 0x30。控制器
基址 `0x07025000` 与 `sun60iw2p1.dtsi: r_pio: pinctrl@7025000` 一致。

第二个、也是更致命的一处：**PL5 的 mux 寄存器在 +0x04，不在 +0x00**。
hw_type 1 每个 bank 只有 0x30 字节，mux 占 +0x00..+0x0c，每个字管 8 个引脚，
所以 pin 5 落在 `base + (5/8)*4 = base + 0x04`，字段是 bit20..23。
旧代码把 mux 写到 `base + 0x00`，改的是 PL0..PL3；PL5 始终停留在复位后的
输入态，于是往数据位写 1 只能通过上拉把电平拽到 2.13 V——现象完全吻合。

### 修复

```c
#define A733_RPIO_FIRST_BANK   0x000   /* bank L 是起始 bank */
#define A733_RPIO_STRIDE       0x30    /* hw_type 1 的 bank_mem_size */
#define A733_RPIO_MUX_OFF      0x00    /* + (pin / 8) * 4 */
#define A733_RPIO_DATA_OFF     0x10
#define A733_RPIO_DRV_OFF      0x14    /* + (pin / 8) * 4 */
#define A733_RPIO_PUL_OFF      0x24    /* + (pin / 16) * 4 */
```

`a733_rpio_output()` 里 `cfg` 补上 `A733_RPIO_MUX_OFF`，`drv`/`pul` 由"相对 cfg"
改成"相对 bank 基址"，与主 PIO 的写法对齐；PL5 的四个 4 位字段位置（mux/dlevel
各 bit20..23、pull bit10..11）不变。

驱动强度编码也核对过：`drive-strength = <10>` 经
`sunxi_pctrl_parse_drive_prop()` 的 `rounddown(val,10)` 再除以 10，落到寄存器就是
`1`，所以原来的 `1u << drvshift` 是对的。

### 编译期验证（本次做了三层）

1. 反汇编确认 SPI 的命令/数据切换点已经指向新地址：

   ```text
   <a733_spi_cmddata>:
       mov  x1, #0x5010          ; 0x07025010 = R_PIO + 0x10（data）
       movk x1, #0x702, lsl #16
       ldr  w0, [x1]
       orr  w3, w0, #0x20        ; bit5 = PL5
       and  w0, w0, #0xffffffdf
   ```

   旧地址的高位模式 `0x5220` 在整个镜像里出现 **0 次**（修复前是它）。

2. 全镜像扫描 `movk #0x702, lsl #16`，命中 17 处，分布在
   `a733_spi_cmddata` / `a733_lcd_diag_*` / `a733_spibus_initialize` /
   `a7z_gpio_initialize` / `a7z_led_*` 等，全部走同一套修正后的地址计算。

3. 三条独立镜像校验（生成脚本 + `reverify_image.py` + `verify-minimal.sh`）全部
   通过，镜像内 `/extlinux/nuttx.bin` 与 `/boot/openvela/a733/nuttx.bin` 回读都与
   本轮内核逐字节一致。

**仍未上板确认**：PL5 是否真的变成 3.3 V、屏幕是否出色块。上板后请用
`cat /dev/a733-lcd`（读寄存器）或直接量 Pin 22 电压。

### 顺带修掉构建脚本的一个真实缺陷

`tools/build-a733-wapi.sh` 在 `set -euo pipefail` 下直接跑 `patch --forward`。
当某个补丁**已经在树里**时，`patch` 会打印
`Reversed (or previously applied) patch detected!  Skipping patch.` 并**返回 1**，
脚本当场退出——表现是"打完补丁就结束，根本没调用编译器"，而且不报任何错。

修复方式是把补丁应用收敛成一个 `apply_patch()`，用 `--dry-run` 探测方向后再决定
是否回退。**绝不能做无条件 `patch -R`**：`ai-agent-provisioning.patch` 里存在
`@@ -114,1 +115,5 @@` 这类计数写错的 hunk（实际新增 6 行），盲目反向应用会**复制**
而不是删除 `read_done:` 标签，把 `config_store.c` 改成重复标签 + 未声明变量
（本次就踩到了，已从 git 恢复）。

> **v123 更正**：这一版 `apply_patch()` 的探测条件仍然是错的，见下一节。当时的
> 判断依据是"反向 dry-run 文本里有 FAILED/fuzz 就不许回退"，而真实输出里既没有
> `FAILED` 也没有 `fuzz`，于是它照样回退了已经打好的补丁——`nuttx-st7735-werror.patch`
> 因此被反复"撤掉又打不上"，内核连续两个版本都没有面板初始化序列。

### 本轮构建与镜像

```text
nuttx.bin SHA-256    28824db5a6a1432ec2a0a55d9dc3cc21ea33c5dc04ee732684d6ce8514041f89
镜像 SHA-256         9b2e549ad07238039cb4f5e3513e09f161fbe73469c9b38feacc069d888baa4b
镜像路径             D:\Program-files\openvela\openvela-a733-cubie-a7z-minimal.img
镜像大小             536887808 字节（512.0 MiB）
```

## v123 屏幕出色块但全是竖条纹：面板初始化序列**根本没编进内核**

### 现象（实机）

R_PIO 修好之后屏幕终于有反应：残影被刷掉，出现"眼睛/嘴巴"形状正确的画面，说明
图形管线是通的。但整个画面被切成细密**竖条纹**，颜色也不对（左蓝、中白、右黄绿）。

同时 `cat /dev/a733-lcd` 显示 PL5 已经完全正常：

```text
rpio-pl: mux1=02000007 drv1=11100000 pul1=00000000 data=00000024 pl5=1
```

即 `mux` bit20..23 = 1（gpio_out）、`data` bit5 = 1。**D/C 问题已经彻底解决**，
2.13 V 那个电平不再出现。

### 根因一：内核里没有面板初始化序列

在二进制里搜初始化特征串，结果是 **0 次**：

```text
b1 01 2c 2d  (FRMCTR1)  : 0
c0 a2 02 84  (PWCTR1)   : 0
```

也就是说 ST7735 从来没有收到过 FRMCTR / PWCTR / VMCTR / gamma 这四组寄存器配置，
面板一直跑在上电默认状态。而构建日志（`/tmp/build2.log` 第 4-7 行）写得很清楚：

```text
patching file nuttx/drivers/lcd/st7735.c
Reversed (or previously applied) patch detected!  Skipping patch.
10 out of 10 hunks ignored -- saving rejects to file nuttx/drivers/lcd/st7735.c.rej
```

**10 个 hunk 全部被忽略，补丁等于没打**——但 `patch` 在这种情况返回的是 1，不是
`>= 2`，所以 `apply_patch()` 判定"成功"，构建照常继续，串口日志一切正常。
这是一个**静默失败**：编译成功、镜像校验通过，唯独功能是空的。

### 根因二：`patch` 的退出码无法用于状态判断

实测四种组合，**退出码永远是 0**：

| 状态 | `patch -R --dry-run` 输出 | rc |
|---|---|---|
| 补丁已应用 | `checking file ...`（干净） | 0 |
| 补丁未应用 | `Unreversed patch detected!  Ignoring -R.` | 0 |

所以只能**按文本判断**，不能看退出码：

```bash
if [[ "$probe" == *"Unreversed patch detected"* ]]; then
  : # 未应用，无需回退
elif [[ "$probe" == *"FAILED"* || *"offset"* || *"fuzz"* || *"hunk"* ]]; then
  echo "无法判定状态，拒绝回退" >&2; exit 1
else
  patch -R ...   # 只有能证明"已应用"才回退
fi
```

### 修复

1. **`apply_patch()` 改成按文本判定**（上表），只有确证已应用才回退；
2. **补丁用脚本重新生成**。原补丁是手写的，hunk 头与正文行数不一致
   （例如 `@@ -335,6 +355,147 @@` 实际有 163 行），`patch` 读的时候能容忍，
   但反向应用时这种形状会被"复制"而非删除。新增
   `tools/` 之外的生成脚本：从 `git show HEAD:drivers/lcd/st7735.c` 取原始文件，
   逐条做文本替换，再用 `difflib.unified_diff` 输出——行数由机器保证。
   注意 `#endif` 必须以**函数头**为锚点插入，用 `}` 作锚点会落到
   `st7735_lcdinitialize` 后面，导致 `#endif without #if`；
3. **加入构建断言**，把静默失败变成硬失败：

   ```bash
   require_contains nuttx/drivers/lcd/st7735.c st7735_initseq "面板初始化序列"
   require_contains nuttx/drivers/lcd/st7735.c st7735_cmd1   "单参数命令 helper"
   ```

4. **强制设置 COLMOD**。`st7735_bpp()` 只在 `dev->bpp != bpp` 时才发 COLMOD，
   颜色深度因此依赖一个缓存值。ST7735 上电默认是 **18 位**模式，用 16 位
   RGB565 去喂它会把字节交错，画面形状正确但每行碎成竖条纹——和观察到的现象
   一致。现在在 `st7735_initseq()` 里无条件补一条
   `st7735_cmd1(dev, ST7735_COLMOD, (ST7735_BPP >> 2) | 1)`（16bpp → `0x05`）。

### 编译期验证

`st7735_initseq` 被内联进 `st7735_lcdinitialize`，所以直接反汇编该函数看立即数：

```text
#0xffffffb1 ×1   #0xffffffb2 ×1   #0xffffffb3 ×1   #0xffffffb4 ×1
#0xffffffc0 ×1   #0xffffffc1 ×1   #0xffffffc2 ×1   #0xffffffc3 ×1
#0xffffffc4 ×1   #0xffffffc5 ×1
#0x2c ×4  #0x2d ×4  #0xa2  #0x84  #0x8a  #0x2a
#0x17c (380 ms 延时)   #0x3a 0x05 (COLMOD)
```

12 条面板命令 + 参数 + 延时全部就位，`0xffffffXX` 是 32 位立即数的符号扩展形式。
另外 `apply_patch()` 连打三次的结果稳定为 `lines=990 initseq=4`，幂等性得到验证。

### 本轮构建与镜像

```text
nuttx.bin SHA-256    495e84f9133e87e3cee2099b4b57cb80b52ea35a467047d213bf64b6a8d9563a
镜像 SHA-256         6cc5e6023f0e81afee91d222fa5442d79ec613101d4ea72b4347fb2b1e6a77fb
镜像路径             D:\Program-files\openvela\openvela-a733-cubie-a7z-minimal.img
镜像大小             536887808 字节（512.0 MiB）
```

三条独立校验全部通过，镜像内两处 `nuttx.bin` 回读都与本轮内核逐字节一致。

**仍未上板确认**：竖条纹是否消失、颜色是否正确。


