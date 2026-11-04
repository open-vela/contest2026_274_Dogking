# A733 LVGL 桌宠界面

## 状态

v127 在 v126 已通过实机验收的 ST7735 direct-8-bit/RGB565 通道上加入正式桌宠
界面。SPI1、片选、D/C、RESET、面板初始化、颜色顺序和 LVGL flush 均未改写；本轮
只增加产品层场景、动画和 Agent 事件接口。

v127 实机确认首帧、`demo` 状态机和跨命令消息队列均工作，但物理屏幕始终停留在
第一张普通表情。日志中的情绪状态持续变化，且没有 SPI、异常或内存错误，因此问题
定位为应用自管 LVGL 循环没有推进 `lv_tick`：首个无效区可以刷新，后续刷新计时器
永远不会到期。v128 仅在每次调度休眠后调用 `lv_tick_inc(delay)`，未改动已通过的
SPI1、面板初始化、RGB565 或 flush 路径。v128 已完成全量构建、镜像校验和实机
验收：七种 demo 表情可以连续切换，手动情绪消息生效，AIPET 请求开始进入“思考”、
回复结束回到解析出的情绪。产品 UI 状态提升为 **hardware-passed**。

## 架构边界

```text
官方 packages_ai_agent / AIPET 路由
                  |
                  | 非阻塞 POSIX 消息队列 aipet_ui
                  v
display pet（唯一 LVGL 所有者）
                  |
                  v
官方 LVGL -> A733 direct RGB565 flush -> SPI1 -> ST7735
```

- `display pet` 是唯一允许调用 LVGL API 的任务。
- Agent、LLM 和测试命令只发送固定 24 字节的有界消息，不直接操作 LVGL。
- 队列发送为非阻塞；界面未启动或队列已满时不会阻塞 Agent 对话。
- 非普通表情保持 10 秒后自动回到普通表情；普通表情约每 3.2 秒眨眼。
- 面板初始化后先清黑整块未定义 GRAM，避免 v126 首次全帧覆盖前出现随机像素。

## 表情映射

桌宠保留 Linux 原型的中文情绪名称，并收敛为七种低开销 LVGL 场景：

| LVGL 场景 | 接受的情绪名 |
| --- | --- |
| 普通 | 普通及未知名称 |
| 开心 | 开心、兴奋、得意、调皮、赞、happy |
| 难过 | 难过、委屈、sad |
| 惊讶 | 惊讶、害怕、surprise |
| 生气 | 生气、鄙视、angry |
| 思考 | 思考、疑惑、困惑、好奇、thinking |
| 安慰 | 安慰、害羞、期待、comfort |

图形全部由 LVGL 对象即时绘制，不依赖字体、GIF、PNG 或外部资源文件，因此适合
当前 128×160 屏幕，也为以后替换动画资源保留单一 UI 入口。

## v128 制品

```text
build: quickly-openvela/cmake_out/cubie-a7z_nsh_v128_lvgl_tick
nuttx.bin bytes: 2195392
nuttx.bin SHA-256: 9f176e6ffed714bd7b530de70046f35579f6a17807c12377c11a61bab63cef3b
image: openvela-a733-cubie-a7z-sd-lvgl-pet-v128-tick-fix-candidate.img
image bytes: 2147483648
image SHA-256: 5e7c1f6044957cbc38881619bc082e738bfc180f1753b367e42c2cb09454a830
embedded kernel SHA-256: 9f176e6ffed714bd7b530de70046f35579f6a17807c12377c11a61bab63cef3b
ext4: clean
GPT: no problems found
```

## 实机测试

先复测已知稳定的底层：

```text
display status
display raw8
display test 10
```

随后测试产品界面和全部表情：

```text
display demo 30
```

`demo` 应每两秒切换一种表情，并在 30 秒后自动返回 NSH。若要测试事件接口，可
让 UI 在后台运行：

```text
display pet 60 &
sleep 2
display emotion 开心
sleep 2
display emotion 思考
sleep 2
display emotion 难过
sleep 12
```

最后测试真实 Agent 联动：

```text
display pet 120 &
aipet ask "请只回答：屏幕联动成功"
```

提问开始时应进入“思考”，回复完成后切换为 Agent 解析出的情绪；约 10 秒无新事件
后回到普通表情。完成后保存屏幕照片以及：

```text
dmesg
free
```

验收门禁：所有命令可自动返回或被 Ctrl+C 中止，画面无竖条/随机噪点，`dmesg`
无 SPI timeout、FIFO overflow、Data Abort、堆损坏或消息队列错误。

v128 的关键验收点不是控制台文字，而是实体屏必须随 `demo` 每两秒改变眼睛、眉毛、
嘴形或颜色；手动发送“开心 → 思考 → 难过”时也必须出现三种可辨认画面。若日志
变化但画面再次静止，需保存照片与 `dmesg`，不要继续修改已经通过的 SPI 层。

2026-09-27 实机已通过上述门禁。AIPET 联动日志显示请求开始发送“思考”，云端回复
完成后发送“普通”，实体屏同步变化。云模型回复中关于“没有启用屏幕控制”的文本只是
模型不了解板端外设状态，不是驱动或事件链路失败；验收依据是板端事件日志与实体屏
变化。完整串口记录见 `docs/evidence/st7735-v128/board-test.txt`。30 秒 demo 和单轮
AIPET 联动仍不能替代数小时压力测试，因此 `stress-passed` 保持为否。

## v129 全屏帧率基准

v129 不修改 v128 已通过的面板初始化、SPI1、RGB565、表情或 Agent 事件路径，只新增
可重复的全屏 LVGL 刷新基准：

```text
display fps 10
display fps 30
```

基准在四种全屏纯色间循环，强制 LVGL 同步刷新，并从实际 flush 回调统计完整帧、
flush 次数、像素数、单调时钟耗时和 RGB565 有效吞吐。输出示例格式为：

```text
display: lvgl-fps frames=N elapsed=M ms fps=X.YYY flushes=F pixels=P throughput=K KiB/s
```

这不是空循环或 UI 定时器帧数。当前 SPI 时钟为 12 MHz，忽略命令、窗口设置、片选
和软件绘制开销时，128×160×16-bit 全帧的总线理论上限约 36.6 fps；实测值应低于
该数值。烧录后先运行 10 秒基准，再运行 30 秒基准并保存输出、`dmesg` 和 `free`。
最后执行 `display demo 30`，确认性能测试没有破坏 v128 的七种表情回归。

```text
build: quickly-openvela/cmake_out/cubie-a7z_nsh_v129_lvgl_fps
nuttx.bin bytes: 2195392
nuttx.bin SHA-256: 8fe4c531b12223e0dd785f8cd009426fcb51de2f90c51992c81a06c08043d2b1
image: openvela-a733-cubie-a7z-sd-lvgl-fps-v129-candidate.img
image bytes: 2147483648
image SHA-256: 406697b0da50ba18809ed016fb215f7933e023a32a08fdac04ad05aca5719f5c
embedded kernel SHA-256: 8fe4c531b12223e0dd785f8cd009426fcb51de2f90c51992c81a06c08043d2b1
ext4: clean
GPT: no problems found
hardware FPS: passed, 29.563 fps sustained for 30 seconds
```

2026-09-27 实机结果：10 秒完成 296 帧、`29.564 fps`；30 秒完成 887 帧、
`29.563 fps`。两次均为 `1182 KiB/s`，每帧 16 个 10 行 flush，说明结果稳定且统计
覆盖了完整 128×160 帧。随后 `display demo 30` 完整切换七种表情；`dmesg` 无显示
错误，测试后尚余 `4239806816` 字节内存。v129 显示性能检查点提升为
**hardware-passed**，但尚不能标记为数小时 `stress-passed`。
