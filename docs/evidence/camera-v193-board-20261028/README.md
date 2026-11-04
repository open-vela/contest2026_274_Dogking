# v193 实际 ID 识别通过（单次，2026-10-28）

用户原始串口附件保存在 `serial.txt`，SHA256：
`6e4333505424e62dd6e1dd995ca48411c03b913c539ece95838fcab50aa86e87`。
对应候选镜像内核 7036768 字节、CRC32 `6ddb4047`。

- 配套脚本六字节实时交接 `03/00/ff/17/c1/0d`，进入 NSH。
- prepare 13ms；ID 实验 31ms，读到 `0214 match=1`。
- 681 次 MMIO 读、31 次写，ID 轮询 616 次；无 bus quarantine。
- 实验后 state=2、owned=0、configured=0、error=cleanup=command-error=0。
- 一次传感器控制通信通过，不是电气测量、重复冷启动、模式配置、CSI 帧或 ISP 验收。

通过源码对应 `14e96fd987a93c30397bc8c9a06d338dc444db08`，另加不覆盖旧标签的
`a733-v193-id-board-pass-20261028`。历史候选生成时的“未板测”证据保持原样。
日志从 UFS 事件尾部开始，未含完整 BOOT0/version，不单靠 `[ufs]err` 判定实际引导来源。
