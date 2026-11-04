# v192 单次板测证据（2026-10-28）

用户串口附件原样保存在 `serial.txt`，SHA256：
`1f0845d7ed69e4e7486159a09cfcc4ed7c620897e8a4488cc0c1ab3958a8798b`。
本记录追加到历史发布之后，不修改原始“未板测”的构建证据。

- 配套脚本加载内核 7028576 字节，CRC `fb37cbc6`；六字节交接 `03/00/ff/17/c1/0d`。
- 进入 NSH，CPU 合成图像自测通过。
- prepare 13ms，配置匹配 6/6；1MHz/24MHz 名义配置切换成功。
- park 后 owned=0、configured=0、error=cleanup=0。
- 仅一次控制面实验；未证明物理电压/MCLK、传感器 ID、帧、冷启动循环或长稳。

先前 `No partition table - mmc 0` 在 `mmc dev 0`、`mmc rescan` 后能列出 GPT 和脚本，
是当次设备/分区探测恢复，不代表整卡健康、UFS 残留引导或所有启动警告均已修复。
